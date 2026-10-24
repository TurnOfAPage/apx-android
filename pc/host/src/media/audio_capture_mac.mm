// audio_capture_mac.mm —— macOS 系统声音采集（CoreAudio Process Tap，macOS 14.2+）。
//
// 架构与实现依据见 audio_capture_mac.h 头注释。蓝本：
//   · karlstav/cava input/coreaudio_tap.m（SDK guard、聚合设备字段、拷贝防御）
//   · unmde/weaver host/src/macos_audio.m（NSDictionary 字典版聚合配置、
//     kAudioTapPropertyUID/Format 读取、kAudioDevicePermissionsError 判定、
//     AudioDeviceCreateIOProcIDWithBlock + 显式 Start/Stop）
// 差异：本实现是**立体声**全局 tap；不排除自身进程（本进程不发声，无自激）；
// 重采样在 IOProc 内完成（与 audio_capture.cpp Win 版 convertBlock 同款算法，
// 注释已标注对应关系）——送出的永远是 48k/s16/立体声。

#import <Foundation/Foundation.h>

#include "apxpc/media/audio_capture_mac.h"
#include "apxpc/log.hpp"

// SDK 门槛：AudioHardwareTapping.h / CATapDescription.h 自 macOS 14.2 SDK 起存在。
// SDK 过旧时整段降级（编译过、运行时报“需新 SDK”），对齐 cava 的做法。
#ifndef MAC_OS_VERSION_14_2
#define MAC_OS_VERSION_14_2 140200
#endif

#include <CoreAudio/AudioHardware.h>
#if defined(__has_include)
#if __has_include(<CoreAudio/AudioHardwareTapping.h>)
#define APXPC_HAVE_TAPPING_HEADER 1
#include <CoreAudio/AudioHardwareTapping.h>
#include <CoreAudio/CATapDescription.h>
#endif
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#if !defined(APXPC_HAVE_TAPPING_HEADER) || __MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_VERSION_14_2

namespace apxpc::media::mac_capture {
bool start(PcmSink, void*, std::string* err, std::string*) {
    if (err)
        *err = "当前构建 SDK 过旧（需 macOS 14.2+ SDK 才有 CoreAudio Process Tap），无法启用系统声音采集";
    return false;
}
void stop() noexcept {}
bool active() noexcept { return false; }
}  // namespace apxpc::media::mac_capture

#else  // 有 Tapping 头（本仓 CI = Xcode 26 SDK，必进此分支）

namespace apxpc::media::mac_capture {
namespace {

constexpr double kTargetRate = 48000.0;   // 两端约定（media_session.hpp kAudioSampleRate）
constexpr int    kTargetCh   = 2;

struct MacState {
    AudioObjectID tap = kAudioObjectUnknown;
    AudioObjectID aggregate = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProc = nullptr;
    AudioStreamBasicDescription asbd{};   // 起流时读一次（输出设备切模不实时跟随，见头注释）

    PcmSink sink = nullptr;
    void* sinkCtx = nullptr;

    // 重采样状态（仅 IOProc 线程触碰：单生产者，无锁前提成立）
    double srcPos = 0.0;
    std::vector<float> stage;    // 源速率下的交错立体声 float 中间量
    std::vector<uint8_t> conv;   // 转成 48k/s16 后送往 sink 的块
    std::vector<uint8_t> interleaveBuf;  // N-buffer 输入的交错化缓存（cava 同款防御）

    bool running = false;        // 仅 start/stop 调用方（持 AudioCapture::mu）触碰
};

MacState* g_state = nullptr;   // 单实例语义（面板同时只有一路音箱采集）

std::string osStatusText(OSStatus s) {
    char b[32];
    std::snprintf(b, sizeof b, "%d", static_cast<int>(s));
    return b;
}

/// 需要 TCC「系统音频录制」授权时的指引（multimon-ng 同款文案经验：权限名就叫这个）
std::string permHint() {
    return "需要授权「系统音频录制」：系统设置 → 隐私与安全性 → 系统音频录制，"
           "勾选运行本程序的应用（终端/iTerm 等）后重试";
}

/// 取 tap 的 ASBD（float32/采样率/声道一次拿到）
OSStatus getTapFormat(AudioObjectID tap, AudioStreamBasicDescription* asbd) {
    AudioObjectPropertyAddress addr{
        kAudioTapPropertyFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(*asbd);
    return AudioObjectGetPropertyData(tap, &addr, 0, nullptr, &size, asbd);
}

OSStatus getTapUID(AudioObjectID tap, CFStringRef* uid) {
    AudioObjectPropertyAddress addr{
        kAudioTapPropertyUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(*uid);
    return AudioObjectGetPropertyData(tap, &addr, 0, nullptr, &size, uid);
}

/// 把 AudioBufferList 整理成交错样本（1-buffer 直取；N-buffer 逐帧交织）——cava 同款防御。
/// 返回可读交错 float 指针（*outFrames = 帧数），失败返回 false。
bool interleaveFloat(const AudioBufferList* in, MacState* st, UInt32* outFrames,
                     const float** outData) {
    if (!in || in->mNumberBuffers == 0) return false;
    if (in->mNumberBuffers == 1) {
        const AudioBuffer& b = in->mBuffers[0];
        if (!b.mData || b.mDataByteSize == 0) return false;
        // 交错立体声：每帧 2 样本
        *outFrames = b.mDataByteSize / (sizeof(float) * kTargetCh);
        *outData = static_cast<const float*>(b.mData);
        return *outFrames > 0;
    }
    if (in->mNumberBuffers < static_cast<UInt32>(kTargetCh)) return false;
    const AudioBuffer& b0 = in->mBuffers[0];
    if (!b0.mData || b0.mDataByteSize == 0) return false;
    const UInt32 frames = b0.mDataByteSize / sizeof(float);
    const size_t need = static_cast<size_t>(frames) * kTargetCh * sizeof(float);
    if (st->interleaveBuf.size() < need) st->interleaveBuf.resize(need);
    auto* dst = reinterpret_cast<float*>(st->interleaveBuf.data());
    for (UInt32 f = 0; f < frames; ++f) {
        for (int c = 0; c < kTargetCh; ++c) {
            const AudioBuffer& b = in->mBuffers[c];
            const auto* src = static_cast<const float*>(b.mData);
            if (!src || b.mDataByteSize < (f + 1) * sizeof(float)) return false;
            dst[f * kTargetCh + c] = src[f];
        }
    }
    *outFrames = frames;
    *outData = dst;
    return true;
}

/// float32(源速率/立体声) → 48k/s16 —— 与 audio_capture.cpp Win 版 convertBlock
/// **同款算法**（线性插值 + srcPos 跨块保留小数相位），仅操作数类型为这里的交错 float。
void resampleAndEmit(MacState* st, const float* interleaved, UInt32 frames) {
    st->stage.clear();
    st->stage.reserve(static_cast<size_t>(frames) * kTargetCh);
    for (UInt32 i = 0; i < frames; ++i) {
        st->stage.push_back(interleaved[i * kTargetCh]);
        st->stage.push_back(interleaved[i * kTargetCh + 1]);
    }
    const size_t n = st->stage.size() / kTargetCh;
    const double step = st->asbd.mSampleRate / kTargetRate;
    if (step <= 0.0) return;
    st->conv.clear();
    st->conv.reserve(static_cast<size_t>(static_cast<double>(n) / step) * 4 + 8);
    auto putS16 = [&](int16_t v) {
        st->conv.push_back(static_cast<uint8_t>(v & 0xFF));
        st->conv.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    };
    auto clampS16 = [](double v) -> int16_t {
        if (v > 32767.0) return 32767;
        if (v < -32768.0) return -32768;
        return static_cast<int16_t>(v);
    };
    while (st->srcPos + 1.0 < static_cast<double>(n)) {
        const size_t i = static_cast<size_t>(st->srcPos);
        const double f = st->srcPos - static_cast<double>(i);
        for (int c = 0; c < kTargetCh; ++c) {
            const double v = st->stage[i * kTargetCh + c] * (1.0 - f) +
                             st->stage[(i + 1) * kTargetCh + c] * f;
            putS16(clampS16(v * 32767.0));
        }
        st->srcPos += step;
    }
    // 整块消费掉，只留小数偏移（保持相位连续，同 Win 版）
    st->srcPos = std::max(0.0, st->srcPos - static_cast<double>(n));
    if (!st->conv.empty() && st->sink) st->sink(st->sinkCtx, st->conv.data(), st->conv.size());
}

void cleanup(MacState* st) {
    if (!st) return;
    // 顺序与蓝本一致：先停 IOProc（返回后回调不再触发）→ 销 IOProc → 聚合 → tap
    if (st->ioProc && st->aggregate != kAudioObjectUnknown) {
        AudioDeviceStop(st->aggregate, st->ioProc);
        AudioDeviceDestroyIOProcID(st->aggregate, st->ioProc);
        st->ioProc = nullptr;
    }
    if (st->aggregate != kAudioObjectUnknown) {
        AudioHardwareDestroyAggregateDevice(st->aggregate);
        st->aggregate = kAudioObjectUnknown;
    }
    if (st->tap != kAudioObjectUnknown) {
        AudioHardwareDestroyProcessTap(st->tap);
        st->tap = kAudioObjectUnknown;
    }
}

}  // namespace

bool start(PcmSink sink, void* ctx, std::string* err, std::string* deviceNameOut) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (!sink) return fail("sink 为空");
    if (g_state && g_state->running) return fail("系统声音采集已在运行（重复启动）");

    // 版本门（ObjC++ 里用 @available；调用点已守护，老系统不会碰新符号）
    if (@available(macOS 14.2, *)) {
        // 降级剩余：SDK 门槛已在文件头编译期处理
    } else {
        return fail("系统声音采集需要 macOS 14.2 或更新（当前系统过旧；Windows 用 WASAPI loopback 无此限制）");
    }

    @autoreleasepool {
        auto* st = new MacState();
        st->sink = sink;
        st->sinkCtx = ctx;

        // 全局立体声 tap：排除空数组 = 全部进程；不静音系统；私有（仅本进程可见）
        CATapDescription* desc =
            [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
        desc.name = @"AllPeriph 系统声音";
        desc.privateTap = YES;
        desc.muteBehavior = CATapUnmuted;

        OSStatus status = AudioHardwareCreateProcessTap(desc, &st->tap);
        if (status != noErr) {
            const std::string m =
                (status == kAudioDevicePermissionsError)
                    ? permHint()
                    : ("CoreAudio Process Tap 创建失败：OSStatus=" + osStatusText(status));
            delete st;
            APX_LOGW("{}", m);
            return fail(m);
        }

        // tap 的唯一标识与格式
        CFStringRef tapUID = nullptr;
        status = getTapUID(st->tap, &tapUID);
        if (status == noErr && tapUID)
            status = getTapFormat(st->tap, &st->asbd);
        if (status != noErr || !tapUID || st->asbd.mFormatID != kAudioFormatLinearPCM ||
            (st->asbd.mFormatFlags & kAudioFormatFlagIsFloat) == 0 ||
            st->asbd.mBitsPerChannel != 32) {
            if (tapUID) CFRelease(tapUID);
            const std::string m = "tap 输入不是 float32 PCM 或属性读取失败：OSStatus=" +
                                  osStatusText(status);
            cleanup(st);
            delete st;
            return fail(m);
        }

        // 聚合设备（weaver 同款 NSDictionary 版；TapAutoStart=NO —— 由下方显式 Start，
        // 与本类 start/stop 生命周期对齐）
        NSDictionary* aggDesc = @{
            @kAudioAggregateDeviceNameKey : @"AllPeriph Tap Aggregate",
            @kAudioAggregateDeviceUIDKey :
                [NSString stringWithFormat:@"apx.audio.tap.%d", static_cast<int>(getpid())],
            @kAudioAggregateDeviceIsPrivateKey : @YES,
            @kAudioAggregateDeviceTapAutoStartKey : @NO,
            @kAudioAggregateDeviceTapListKey : @[ @{
                @kAudioSubTapUIDKey : (__bridge NSString*)tapUID,
                @kAudioSubTapDriftCompensationKey : @YES,
            } ],
        };
        status = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggDesc, &st->aggregate);
        CFRelease(tapUID);
        if (status != noErr) {
            const std::string m = "聚合设备创建失败：OSStatus=" + osStatusText(status);
            cleanup(st);
            delete st;
            return fail(m);
        }

        // IOProc（block 版；回调在 HAL 线程）。格式快路径：48k/立体声 直通重采样。
        status = AudioDeviceCreateIOProcIDWithBlock(
            &st->ioProc, st->aggregate, nullptr,
            ^(const AudioTimeStamp* /*now*/, const AudioBufferList* in,
              const AudioTimeStamp* /*inTime*/, AudioBufferList* /*out*/,
              const AudioTimeStamp* /*outTime*/) {
                if (!g_state || !g_state->running) return;
                UInt32 frames = 0;
                const float* data = nullptr;
                if (!interleaveFloat(in, g_state, &frames, &data)) return;
                resampleAndEmit(g_state, data, frames);
            });
        if (status != noErr) {
            const std::string m = "IOProc 创建失败：OSStatus=" + osStatusText(status);
            cleanup(st);
            delete st;
            return fail(m);
        }

        st->running = true;
        g_state = st;   // IOProc 判 g_state->running —— 先置 running 再发布指针
        status = AudioDeviceStart(st->aggregate, st->ioProc);
        if (status != noErr) {
            st->running = false;
            g_state = nullptr;
            const std::string m =
                (status == kAudioDevicePermissionsError)
                    ? permHint()
                    : ("tap 流启动失败（AudioDeviceStart）：OSStatus=" + osStatusText(status));
            cleanup(st);
            delete st;
            return fail(m);
        }

        if (deviceNameOut) *deviceNameOut = "系统混音（CoreAudio Tap）";
        APX_LOGI("系统声音采集已启动：tap {}Hz {}ch float → 48k/s16",
                 static_cast<unsigned>(st->asbd.mSampleRate),
                 static_cast<unsigned>(st->asbd.mChannelsPerFrame));
        return true;
    }
}

void stop() noexcept {
    MacState* st = g_state;
    if (!st) return;
    st->running = false;
    g_state = nullptr;     // IOProc 判空后直接返回（双保险：停前标志 + 停后指针）
    cleanup(st);           // AudioDeviceStop 返回后回调不再触发，此处 free 安全
    delete st;
    APX_LOGI("系统声音采集已停止");
}

bool active() noexcept {
    return g_state && g_state->running;
}

}  // namespace apxpc::media::mac_capture

#endif  // APXPC_HAVE_TAPPING_HEADER && SDK >= 14.2
