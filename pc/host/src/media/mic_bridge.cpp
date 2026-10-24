#include "apxpc/media/mic_bridge.hpp"

#include "apxpc/log.hpp"

#include <mutex>
#include <deque>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

#if defined(_WIN32)

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

namespace apxpc::media {
namespace {

// 手机上行 PCM 的固定格式（与 WirelessAudioModule 上行一致）
constexpr WORD      kSrcChannels = 2;
constexpr DWORD     kSrcRate     = 48000;
constexpr WORD      kSrcBits     = 16;

constexpr uint32_t kBufferMs  = 300;   // WASAPI 缓冲
constexpr uint32_t kMaxQueueMs = 400;  // 环形上限，超出丢最旧（保时效）

}  // namespace

struct MicBridge::Impl {
    std::mutex mu;
    std::deque<int16_t> q;             // interleaved int16 样本
    std::atomic<bool> running{false};
    std::thread th;

    // WASAPI（仅渲染线程触碰）
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    UINT32 bufFrames = 0;

    // 状态（mu 保护；running 用原子）
    std::string device;
    std::string error;
    uint64_t fed = 0, played = 0, dropped = 0;
    std::string renderDeviceId;
};

bool MicBridge::start(const std::string& renderDeviceId, std::string* err) {
    stop();
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->renderDeviceId = renderDeviceId;
    impl_->running.store(true);
    impl_->th = std::thread([this] { renderLoop(); });
    (void)err;   // 真正的失败在渲染线程里探明，进 status().error
    return true;
}

void MicBridge::stop() {
    if (!impl_) { impl_ = new Impl(); return; }
    // v184：**无论 running 是否已为 false 都要 join 渲染线程**。
    // 渲染线程初始化失败时会自己 running.store(false) 再返回（见 renderLoop），
    // 此时 th 仍为 joinable —— 漏 join 会让析构（或下次 start 的重新赋值）
    // 直接触发 std::terminate（进程闪退）。
    impl_->running.store(false);
    if (impl_->th.joinable() && impl_->th.get_id() != std::this_thread::get_id()) {
        impl_->th.join();
    }
}

bool MicBridge::running() const {
    return impl_ && impl_->running.load();
}

void MicBridge::feed(const void* data, size_t bytes) {
    if (!impl_ || !impl_->running.load() || !data || bytes == 0) return;
    const int16_t* s = static_cast<const int16_t*>(data);
    const size_t n = bytes / sizeof(int16_t);
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->fed += n;
    const size_t maxQ = static_cast<size_t>(kSrcRate) * kSrcChannels * kMaxQueueMs / 1000;
    if (impl_->q.size() + n > maxQ) {
        const size_t over = impl_->q.size() + n - maxQ;
        if (over >= impl_->q.size()) impl_->q.clear();
        else impl_->q.erase(impl_->q.begin(), impl_->q.begin() + static_cast<long>(over));
        impl_->dropped += over;
    }
    impl_->q.insert(impl_->q.end(), s, s + n);
}

MicBridge::Status MicBridge::status() const {
    Status s;
    if (!impl_) return s;
    std::lock_guard<std::mutex> lk(impl_->mu);
    s.running = impl_->running.load();
    s.device = impl_->device;
    s.error = impl_->error;
    s.fed = impl_->fed;
    s.played = impl_->played;
    s.dropped = impl_->dropped;
    return s;
}

void MicBridge::shutdownLocked() {
    if (impl_->render) { impl_->render->Release(); impl_->render = nullptr; }
    if (impl_->client) { impl_->client->Stop(); impl_->client->Release(); impl_->client = nullptr; }
}

void MicBridge::renderLoop() {
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto coGuard = [](void*) { ::CoUninitialize(); };
    std::unique_ptr<void, decltype(coGuard)> coGuardian(nullptr, coGuard);

    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->error.clear();
        impl_->device.clear();
        impl_->played = 0;
        impl_->dropped = 0;
        impl_->q.clear();
    }

    IMMDeviceEnumerator* enumr = nullptr;
    IMMDevice* dev = nullptr;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumr));
    if (SUCCEEDED(hr)) {
        if (!impl_->renderDeviceId.empty()) {
            wchar_t idW[256]{};
            ::MultiByteToWideChar(CP_UTF8, 0, impl_->renderDeviceId.c_str(), -1, idW, 256);
            hr = enumr->GetDevice(idW, &dev);
            if (FAILED(hr)) {
                // 设备没了（拔了虚拟声卡）：退回默认，别让开关看起来坏了
                hr = enumr->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev);
            }
        } else {
            hr = enumr->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev);
        }
    }
    if (SUCCEEDED(hr) && dev) {
        hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&impl_->client));
    }
    if (SUCCEEDED(hr) && impl_->client) {
        // 源格式固定 48k/16/2，AUTOCONVERTPCM 让系统负责转到混音格式（Win8.1+）
        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = kSrcChannels;
        wf.nSamplesPerSec = kSrcRate;
        wf.wBitsPerSample = kSrcBits;
        wf.nBlockAlign = kSrcChannels * kSrcBits / 8;
        wf.nAvgBytesPerSec = kSrcRate * wf.nBlockAlign;
        const REFERENCE_TIME buf = static_cast<REFERENCE_TIME>(kBufferMs) * 10000;
        hr = impl_->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                       AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                           AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                       buf, 0, &wf, nullptr);
    }
    if (SUCCEEDED(hr) && impl_->client) {
        hr = impl_->client->GetBufferSize(&impl_->bufFrames);
        if (SUCCEEDED(hr))
            hr = impl_->client->GetService(__uuidof(IAudioRenderClient),
                                           reinterpret_cast<void**>(&impl_->render));
    }
    if (SUCCEEDED(hr) && impl_->render) {
        hr = impl_->client->Start();
    }

    // 设备名（给面板显示）
    if (SUCCEEDED(hr) && dev) {
        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
            PROPVARIANT v{};
            PropVariantInit(&v);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) {
                char name[256]{};
                ::WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, name, 256, nullptr, nullptr);
                std::lock_guard<std::mutex> lk(impl_->mu);
                impl_->device = name;
            }
            PropVariantClear(&v);
            ps->Release();
        }
    }
    if (dev) dev->Release();
    if (enumr) enumr->Release();

    if (FAILED(hr)) {
        std::lock_guard<std::mutex> lk(impl_->mu);
        char msg[128];
        std::snprintf(msg, sizeof(msg), "WASAPI 渲染初始化失败：0x%08lX", static_cast<unsigned long>(hr));
        impl_->error = msg;
        impl_->running.store(false);
        shutdownLocked();
        return;
    }

    // 渲染循环：轮询填充；缓冲空闲帧按混音通道折算成源样本数
    WAVEFORMATEX* mix = nullptr;
    UINT32 mixCh = 2;
    if (SUCCEEDED(impl_->client->GetMixFormat(&mix)) && mix) {
        mixCh = mix->nChannels;
        ::CoTaskMemFree(mix);
    }

    while (impl_->running.load()) {
        ::Sleep(5);
        UINT32 padding = 0;
        if (FAILED(impl_->client->GetCurrentPadding(&padding))) break;
        const UINT32 availFrames = impl_->bufFrames - padding;
        if (availFrames == 0) continue;

        BYTE* dst = nullptr;
        if (FAILED(impl_->render->GetBuffer(availFrames, &dst))) break;

        // 取出可用的源帧（每帧 2 样本）；不足补静音
        size_t srcFrames = 0;
        std::vector<int16_t> src;
        {
            std::lock_guard<std::mutex> lk(impl_->mu);
            srcFrames = std::min<size_t>(impl_->q.size() / kSrcChannels, availFrames);
            src.assign(impl_->q.begin(), impl_->q.begin() + static_cast<long>(srcFrames * kSrcChannels));
            impl_->q.erase(impl_->q.begin(), impl_->q.begin() + static_cast<long>(srcFrames * kSrcChannels));
            impl_->played += srcFrames * kSrcChannels;
        }
        int16_t* d = reinterpret_cast<int16_t*>(dst);
        if (mixCh == kSrcChannels) {
            std::memcpy(d, src.data(), src.size() * sizeof(int16_t));
        } else {
            for (size_t f = 0; f < srcFrames; ++f) {
                const int16_t l = src[f * 2], r = src[f * 2 + 1];
                for (UINT32 c = 0; c < mixCh; ++c) d[f * mixCh + c] = (c % 2 == 0) ? l : r;
            }
        }
        const size_t need = static_cast<size_t>(availFrames) * mixCh;
        const size_t filled = srcFrames * mixCh;
        if (filled < need) std::memset(d + filled, 0, (need - filled) * sizeof(int16_t));
        impl_->render->ReleaseBuffer(availFrames, 0);
    }

    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        shutdownLocked();
        impl_->running.store(false);
    }
}

}  // namespace apxpc::media

#elif defined(__APPLE__)
// ---------------------------------------------------------------- macOS ----
// 手机上行 PCM（48k/16bit/立体声）→ CoreAudio AudioQueue 渲染到默认输出设备。
// 与 Win 版（WASAPI）同一套语义：
//   · feed() 任意线程（媒体收流线程）入队；渲染在自建线程，缺数据填静音保时钟
//   · 400ms 环形上限，超出丢最旧（保时效）
//   · 失败不在 start() 同步抛 —— 进 status().error，running 自动回落 false
// 设备选择：macOS 暂只支持默认输出（renderDeviceId 忽略；面板在 mac 的下拉列表
// 本来就是空的 → 传空 → 默认设备，自洽）。要指定设备需要 AudioObject 按 UID
// 枚举，留下次。
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <cstring>

namespace apxpc::media {
namespace {

constexpr int      kSrcChannels = 2;
constexpr int      kSrcRate     = 48000;
constexpr int      kSrcBits     = 16;
constexpr uint32_t kMaxQueueMs  = 400;      // 环形上限，与 Win 版一致
constexpr uint32_t kAqBufFrames = 1024;     // AudioQueue 回调 buffer 帧数（≈21ms @48k）

// 默认输出设备名（给面板显示；查不到就给诚实占位，不假装）
std::string defaultOutputName() {
    AudioObjectPropertyAddress addr{
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioHardwarePropertyScopeGlobal,
        kAudioObjectPropertyElementMain};
    AudioDeviceID dev = kAudioObjectUnknown;
    UInt32 sz = sizeof(dev);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &sz, &dev)
            != noErr || dev == kAudioObjectUnknown) {
        return "默认输出设备";
    }
    AudioObjectPropertyAddress naddr{
        kAudioObjectPropertyName,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain};
    CFStringRef name = nullptr;
    UInt32 nsz = sizeof(name);
    if (AudioObjectGetPropertyData(dev, &naddr, 0, nullptr, &nsz, &name) != noErr || !name)
        return "默认输出设备";
    char buf[256]{};
    const bool ok = CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(name);
    return ok ? std::string(buf) : std::string("默认输出设备");
}

// AudioQueue 回调：从队列取样本拷进 buffer，不足补静音（与 Win renderLoop 同语义）。
// 每回调消耗一份、末尾把同一 buffer enqueue 回去 —— 单 buffer 链式循环即可持续驱动。
void aqOutputCb(void* user, AudioQueueRef q, AudioQueueBufferRef b) {
    auto* self = static_cast<MicBridge::Impl*>(user);
    if (!self || !b) return;
    const size_t wantFrames =
        static_cast<size_t>(b->mAudioDataByteSize) / (kSrcChannels * sizeof(int16_t));
    if (wantFrames == 0) { AudioQueueEnqueueBuffer(q, b, 0, nullptr); return; }
    int16_t* d = static_cast<int16_t*>(b->mAudioData);
    {
        std::lock_guard<std::mutex> lk(self->mu);
        const size_t srcFrames =
            std::min<size_t>(self->q.size() / kSrcChannels, wantFrames);
        if (srcFrames > 0) {
            std::memcpy(d, self->q.data(), srcFrames * kSrcChannels * sizeof(int16_t));
            self->q.erase(self->q.begin(),
                          self->q.begin() + static_cast<std::ptrdiff_t>(srcFrames * kSrcChannels));
            self->played += srcFrames * kSrcChannels;
        }
        if (srcFrames < wantFrames) {
            std::memset(d + srcFrames * kSrcChannels, 0,
                        (wantFrames - srcFrames) * kSrcChannels * sizeof(int16_t));
        }
    }
    b->mAudioDataByteSize =
        static_cast<UInt32>(wantFrames * kSrcChannels * sizeof(int16_t));
    AudioQueueEnqueueBuffer(q, b, 0, nullptr);
}

}  // namespace

struct MicBridge::Impl {
    std::mutex mu;
    std::deque<int16_t> q;             // interleaved int16 样本
    std::atomic<bool> running{false};
    std::thread th;

    // CoreAudio（仅渲染线程触碰；非空 = 队列在跑）
    AudioQueueRef queue = nullptr;

    // 状态（mu 保护；running 用原子）
    std::string device;
    std::string error;
    uint64_t fed = 0, played = 0, dropped = 0;
    std::string renderDeviceId;        // macOS 暂忽略（见上方块注释）
};

bool MicBridge::start(const std::string& renderDeviceId, std::string* err) {
    stop();   // 同 Win：stop 保证 impl_ 懒初始化 + join 旧线程
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->renderDeviceId = renderDeviceId;
    impl_->running.store(true);
    impl_->th = std::thread([this] { renderLoop(); });
    (void)err;   // 真正的失败在渲染线程里探明，进 status().error（同 Win 语义）
    return true;
}

void MicBridge::stop() {
    if (!impl_) { impl_ = new Impl(); return; }
    // 同 Win v184 修复：初始化失败时渲染线程会自行 running=false 再返回，
    // 此时 th 仍 joinable —— 无论 running 状态都必须 join，否则下次 start
    // 对 th 赋值（或析构）会 std::terminate。
    impl_->running.store(false);
    if (impl_->th.joinable() && impl_->th.get_id() != std::this_thread::get_id()) {
        impl_->th.join();
    }
}

bool MicBridge::running() const {
    return impl_ && impl_->running.load();
}

void MicBridge::feed(const void* data, size_t bytes) {
    if (!impl_ || !impl_->running.load() || !data || bytes == 0) return;
    const int16_t* s = static_cast<const int16_t*>(data);
    const size_t n = bytes / sizeof(int16_t);
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->fed += n;
    const size_t maxQ = static_cast<size_t>(kSrcRate) * kSrcChannels * kMaxQueueMs / 1000;
    if (impl_->q.size() + n > maxQ) {
        const size_t over = impl_->q.size() + n - maxQ;
        if (over >= impl_->q.size()) impl_->q.clear();
        else impl_->q.erase(impl_->q.begin(),
                            impl_->q.begin() + static_cast<long>(over));
        impl_->dropped += over;
    }
    impl_->q.insert(impl_->q.end(), s, s + n);
}

MicBridge::Status MicBridge::status() const {
    Status s;
    if (!impl_) return s;
    std::lock_guard<std::mutex> lk(impl_->mu);
    s.running = impl_->running.load();
    s.device = impl_->device;
    s.error = impl_->error;
    s.fed = impl_->fed;
    s.played = impl_->played;
    s.dropped = impl_->dropped;
    return s;
}

void MicBridge::renderLoop() {
    // 状态复位（同 Win renderLoop 起手）
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->error.clear();
        impl_->device.clear();
        impl_->played = 0;
        impl_->dropped = 0;
        impl_->q.clear();
    }

    // 源格式固定 48k/16bit/立体声（与上行一致）；AudioQueue 由系统负责重采样混音
    AudioStreamBasicDescription asbd{};
    asbd.mSampleRate         = kSrcRate;
    asbd.mFormatID           = kAudioFormatLinearPCM;
    asbd.mFormatFlags        = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    asbd.mBytesPerPacket     = kSrcChannels * kSrcBits / 8;
    asbd.mFramesPerPacket    = 1;
    asbd.mBytesPerFrame      = kSrcChannels * kSrcBits / 8;
    asbd.mChannelsPerFrame   = kSrcChannels;
    asbd.mBitsPerChannel     = kSrcBits;

    AudioQueueRef q = nullptr;
    OSStatus st = AudioQueueNewOutput(&asbd, aqOutputCb, impl_.get(),
                                      nullptr, nullptr, 0, &q);
    if (st != noErr) {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->error = "CoreAudio AudioQueue 创建失败：OSStatus=" + std::to_string(st);
        impl_->running.store(false);
        return;
    }
    AudioQueueBufferRef buf = nullptr;
    st = AudioQueueAllocateBuffer(q, kAqBufFrames * sizeof(int16_t) * kSrcChannels, &buf);
    if (st != noErr) {
        AudioQueueDispose(q, true);
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->error = "CoreAudio buffer 分配失败：OSStatus=" + std::to_string(st);
        impl_->running.store(false);
        return;
    }
    // 预填一份静音起转（后续由回调末尾的 enqueue 形成单 buffer 链式循环）
    buf->mAudioDataByteSize =
        static_cast<UInt32>(kAqBufFrames * sizeof(int16_t) * kSrcChannels);
    std::memset(buf->mAudioData, 0, buf->mAudioDataByteSize);
    AudioQueueEnqueueBuffer(q, buf, 0, nullptr);

    st = AudioQueueStart(q, nullptr);
    if (st != noErr) {
        AudioQueueDispose(q, true);
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->error = "CoreAudio 播放启动失败：OSStatus=" + std::to_string(st);
        impl_->running.store(false);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->device = defaultOutputName();
        impl_->queue = q;
    }

    // 回调驱动，主循环只管生命周期（20ms 轮询停止标志）
    while (impl_->running.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    AudioQueueStop(q, true);
    AudioQueueDispose(q, true);
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->queue = nullptr;
    impl_->running.store(false);
}

}  // namespace apxpc::media

#else  // 非 Windows 且非 macOS —— Linux 等：未实现（诚实报错，不再静默空错误）

namespace apxpc::media {
struct MicBridge::Impl {};
bool MicBridge::start(const std::string&, std::string* err) {
    if (err)
        *err = "麦克风桥在 Linux 上暂未实现（macOS 用 CoreAudio、Windows 用 WASAPI）";
    return false;
}
void MicBridge::stop() {}
bool MicBridge::running() const { return false; }
void MicBridge::feed(const void*, size_t) {}
MicBridge::Status MicBridge::status() const { return {}; }
}  // namespace apxpc::media

#endif
