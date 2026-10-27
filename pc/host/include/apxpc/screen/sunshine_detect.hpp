#pragma once
// Sunshine（LizardByte，Moonlight 协议宿主端）的**只读检测**。
//
// 为什么只是检测、不是对接：ROADMAP §2.5 的结论 —— mac/Linux 不自研副屏（缺虚拟显示
// 器驱动），改为用户自装 Sunshine + Moonlight；而 Moonlight 客户端是 GPLv3，与本项目
// MIT 冲突，不能内嵌。所以面板能做的是「装没装 / 跑没跑 / 端口是多少 + 指引」。
//
// 语义边界（务必先读）：
//   · 本机端口探测只能说「有人在监听」，**说不清是谁**（apxpc::net::tcp_probe 的语义）。
//     所以「已安装」必须靠配置/应用存在这条独立证据；「在跑」靠默认 https 端口有人听。
//     两条证据分开存，谁缺了面板就照实说哪条缺了，不合并成一个含糊的「已就绪」。
//   · 路径探测用 getenv("HOME") 拼家目录 —— 不猜盘符、不扫全盘。
//   · Linux 侧同样适用（~/.config/sunshine/）。
//   · 本文件只在 mac/Linux 编进面板；Windows 有真副屏（自研已交付），不编译它。
#include <cstdint>
#include <string>

namespace apxpc::screen {

struct SunshineStatus {
    bool installed = false;   // 配置目录 / app bundle 存在
    bool running   = false;   // 默认 https 端口有人监听
    uint16_t httpsPort = 47990;
};

/// 只读检测，不做任何写操作；耗时 ≤ 300ms（几次 stat + 一次端口探测）。
SunshineStatus sunshineStatus();

/// Sunshine 设置页地址；没在跑时返回空串（面板据此给指引，不猜端口）。
std::string sunshineUrl(const SunshineStatus& s);

}  // namespace apxpc::screen
