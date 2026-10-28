#pragma once
// 本机 TCP 端口占用探测（跨平台，零业务依赖）。
//
// 用途：判断「某个本地服务是不是已经在跑了」——典型是 9511 受控端：
// 面板与 `apxhost serve` / `apxdesktop` 都会起它，而端口是独占的，第二个实例
// bind 必然失败。历史做法是失败后打一句告警，看起来像故障；有了探测就能区分
// 「另一个 AllPeriph 实例正在提供该服务」（不该当故障报）与「真起不来」。
//
// 语义边界（重要，别当万能）：
//   · 只能说**有人在监听**，说不了「是谁」——该端口被任何别的程序占了同样返回 true；
//   · 只探 **IPv4 回环**（127.0.0.1）。服务绑 INADDR_ANY 也能探到（对端是回环）；
//   · 超时视为「没人」：宁可让调用方按「没人」去尝试绑定（并拿到真实 errno），
//     也不要因为探测本身挂住而拖慢启动路径。
#include <cstdint>

namespace apxpc::net {

/// 探测 host:port 是否可建立 TCP 连接（默认典型是本机回环）。
/// @param timeoutMs 连接等待上限（毫秒）；超时返回 false
/// @return true = 有人在监听且接受连接
bool tcpPortResponds(const char* host, uint16_t port, int timeoutMs = 200);

/// 便捷：探本机回环 127.0.0.1:port
bool tcpPortRespondsLocal(uint16_t port, int timeoutMs = 200);

}  // namespace apxpc::net
