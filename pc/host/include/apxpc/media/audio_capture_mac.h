#pragma once
// macOS 系统声音采集（方案 A：CoreAudio Process Tap，macOS 14.2+）。
//
// 为何单独一个 .mm：`AudioHardwareCreateProcessTap` 收 `CATapDescription*`（ObjC 类），
// SDK 头 `CoreAudio/AudioHardwareTapping.h` 无 C 形态 —— 纯 C++ TU 无法调用。
// 与 update_check_mac.mm / input_injector_mac.mm 同一模式：ObjC/平台细节封闭在 .mm，
// 暴露的接口是纯 C++（本文件）。
//
// 流程（对齐 karlstav/cava 与 unmde/weaver 的开源实现）：
//   CATapDescription(全局立体声、不静音) → AudioHardwareCreateProcessTap
//   → 聚合设备（TapList 挂入）→ AudioDeviceCreateIOProcIDWithBlock → AudioDeviceStart
//   → IOProc(HAL 线程) 内 float32→48k/s16 重采样 → sink 回调（cpp 侧攒片发送）
//
// 权限：需要 TCC「系统音频录制」授权（System Audio Recording）。无 Info.plist 的 CLI
// 二进制的授权归属需实测（失败时错误文案会带指引，不静默）。
//
// 已知平台限制（不隐藏）：DRM 保护的流媒体目录内容 tap 可能出静音（系统侧行为）；
// 输出设备采样率切换不实时跟随（与蓝本一致，下次迭代）。
#include <cstddef>
#include <cstdint>
#include <string>

namespace apxpc::media::mac_capture {

/// PCM 块回调：数据已是两端约定的 48000Hz/16bit/立体声/小端交错 PCM。
/// 回调发生在 CoreAudio IOProc 线程（单线程）——实现方必须无锁（或自保证单生产者）。
using PcmSink = void (*)(void* ctx, const uint8_t* data, size_t bytes);

/// 启动采集（同步；内部建 tap/聚合设备/IOProc 并 AudioDeviceStart）。
/// 幂等前提：调用方（AudioCapture::start）已持实例锁且先 stop() 过。
/// 失败返回 false 并填 err（含权限/版本/OSStatus 指引）；成功时 *deviceNameOut = 展示名。
bool start(PcmSink sink, void* ctx, std::string* err, std::string* deviceNameOut);

/// 停止并释放全部 CoreAudio 对象（幂等）。AudioDeviceStop 返回后 IOProc 不再触发，
/// 此后 sink 不会再被调用 —— 调用方可安全清理 sink 侧状态。
void stop() noexcept;

/// 当前是否在跑（供上层对账；正常由 AudioCapture::running 的原子位主导）。
bool active() noexcept;

}  // namespace apxpc::media::mac_capture
