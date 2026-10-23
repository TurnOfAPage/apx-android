#pragma once
// 浏览器面板后端（方案 B）：本地 HTTP 服务 + React 前端 dist（exe 旁 web/）。
// 协议与 web_panel.cpp（WebView2 壳）同构 —— 同一套状态/动作/事件消息，前端仅换传输层。
// 详见 panel_http.cpp 顶部说明。本实现为 macOS 首选入口；Windows 继续走 web_panel（零交叉）。
#include <string>

#include "apxpc/ui/web_panel.hpp"   // HostBundle

namespace apxpc::ui {

/// 在 127.0.0.1 起本地面板服务，阻塞运行至 SIGINT/SIGTERM（或前端 window.quit 动作）。
/// webDir：前端 dist 目录（exe 旁 web/）；目录不存在时服务仍启动，首页给出获取指引。
/// 就绪后自动用系统默认浏览器打开 http://127.0.0.1:<port>/?host=1
/// （macOS 走 `open`；其他平台仅打印地址）。返回 0 正常退出；-1 服务创建失败。
int runWebPanelHttp(const HostBundle& bundle, const std::string& webDir = {});

}  // namespace apxpc::ui
