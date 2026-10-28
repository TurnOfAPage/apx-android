// macOS 托盘实现（NSStatusItem + NSMenu）—— 取代此前的「非 Windows 空实现」。
//
// 为什么需要它（ROADMAP §2.4 待办 ②）：
//   mac 的 `.app` 双击起来后**没有任何退出入口** —— 只能 `pkill -x apxdesktop`，
//   或者去面板里点「退出」。托盘一挂，菜单栏上就有图标：打开面板 / 退出，
//   以及各回调对应的功能项（谁注册了才出谁）。
//
// ## 线程与事件循环（这里的选择与代价，务必先读）
//
// AppKit 的规矩是 NSStatusItem / NSApp 要在主线程。但本项目的主线程被
// `runWebPanelHttp()` 占着（它是个阻塞的 accept 循环），把整条启动路径改成
// 「主线程跑 AppKit + 面板挪到后台线程」是**结构性改动**，而且没法在本机验证
// （没有 mac 真机；CI 只能证明编得过）。所以这里走的是保守方案：
//   **托盘自带一条线程**，在那条线程上建 NSApplication + NSStatusItem，并跑
//   `[NSApp run]` 自己的事件循环；退出时用 `performSelector:onThread:` 把
//   「停」这一步 marshalling 回那条线程（跨线程直接碰 AppKit 对象是未定义行为）。
// 代价说清楚：这条路径**只有在真机上才能确认菜单能否正常弹出/点击**。
// 若在真机上发现图标不出现或菜单点不动，正确的修法是把主线程让给 AppKit、
// 面板移入后台线程 —— 那是另一件事，不要在这里继续打补丁。
//
// ## ARC
//
// 本文件由 CMake 以 `-fobjc-arc` 编译（与 update_check_mac.mm 同）。所以
// 全局/静态的 ObjC 指针是 strong，不需要 retain/release；也不要写 dealloc 里的 release。
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "apxpc/log.hpp"
#include "apxpc/tray/tray_win32.hpp"

namespace {

/// 菜单项 → 动作 的注册表。菜单项的 tag 就是这里的下标。
struct MenuAction {
    std::function<void()> fn;
};

std::mutex g_actionsMu;
std::vector<MenuAction> g_actions;

/// 建托盘时用：把回调按固定顺序变成菜单项。
struct MenuPlan {
    std::vector<std::pair<std::string, std::function<void()>>> items;
    std::function<void()> quit;
};

}  // namespace

namespace {
// 真正「贴 tooltip + 发通知」的那一步：只在托盘线程上调（AppKit 规矩）。
// 前置声明：它的实现排在下面那个匿名 namespace 里，晚于这里的 @implementation。
void deliverNotification(NSString* title, NSString* body);
}  // namespace

/// 菜单点击的接收者。唯一的职责：把 tag 翻回 std::function 并调用。
@interface APXTrayTarget : NSObject
- (void)invokeAction:(id)sender;
- (void)requestStop;
- (void)showNotification:(NSArray*)pair;   // [title, body]：notify() 跨线程时用它
@end

@implementation APXTrayTarget

- (void)invokeAction:(id)sender {
    const NSInteger tag = [sender tag];
    std::function<void()> fn;
    {
        std::lock_guard<std::mutex> lk(g_actionsMu);
        if (tag >= 0 && static_cast<size_t>(tag) < g_actions.size()) fn = g_actions[tag].fn;
    }
    if (!fn) {
        APX_LOGW("托盘菜单项 {} 没有对应动作（下标越界？）", static_cast<int>(tag));
        return;
    }
    // 动作里可能开子进程 / 置标志 / 打日志：都在托盘线程上执行，快速返回即可。
    // 若将来出现会阻塞的动作（弹模态框等），要丢回自己的线程或队列，别卡住菜单。
    try {
        fn();
    } catch (const std::exception& e) {
        APX_LOGW("托盘菜单动作抛异常：{}", e.what());
    }
}

- (void)showNotification:(NSArray*)pair {
    if ([pair count] < 2) return;
    deliverNotification([pair objectAtIndex:0], [pair objectAtIndex:1]);
}

- (void)requestStop {
    // 必须在这条线程上做：NSStatusItem 的移除与 [NSApp stop:] 都碰 AppKit。
    [NSApp stop:nil];
    // run loop 里没有事件时 stop 不会立刻生效 —— 塞一个空事件把它叫醒。
    NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                       location:NSZeroPoint
                                  modifierFlags:0
                                      timestamp:0
                                   windowNumber:0
                                        context:nil
                                        subtype:0
                                          data1:0
                                          data2:0];
    [NSApp postEvent:wake atStart:NO];
}

@end

namespace {

/// 托盘线程的共享状态（跨线程只通过这些字段，且都在锁/原子下访问）。
struct MacTrayShared {
    std::atomic<bool> stopped{false};
    std::mutex mu;
    std::condition_variable cv;
    bool ready = false;      // create() 等这个：true=图标已挂上，false=起不来
    bool ok = false;
};

MacTrayShared g_shared;
NSThread* g_trayThread = nil;      // 托盘线程（quit 时用于 marshalling 回去）
APXTrayTarget* g_target = nil;     // 菜单接收者（持有到进程结束）
NSStatusItem* g_statusItem = nil;  // 状态栏项

/// 建菜单。所有项都走 APXTrayTarget 的 invokeAction:（tag 索引 g_actions）。
NSMenu* buildMenu(const MenuPlan& plan) {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"全能外设"];
    [menu setAutoenablesItems:NO];

    NSInteger idx = 0;
    for (const auto& it : plan.items) {
        {
            std::lock_guard<std::mutex> lk(g_actionsMu);
            g_actions.push_back(MenuAction{it.second});
        }
        NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:it.first.c_str()]
                                                    action:@selector(invokeAction:)
                                             keyEquivalent:@""];
        [mi setTarget:g_target];
        [mi setTag:idx];
        [menu addItem:mi];
        ++idx;
    }

    if (plan.items.empty()) {
        // 一个回调都没注册：至少别给个空菜单（空菜单看起来就是「点了没反应」）。
        NSMenuItem* none = [[NSMenuItem alloc] initWithTitle:@"（没有可用操作）" action:nil keyEquivalent:@""];
        [none setEnabled:NO];
        [menu addItem:none];
    }

    if (plan.quit) {
        {
            std::lock_guard<std::mutex> lk(g_actionsMu);
            g_actions.push_back(MenuAction{plan.quit});
        }
        [menu addItem:[NSMenuItem separatorItem]];
        NSMenuItem* q = [[NSMenuItem alloc] initWithTitle:@"退出全能外设"
                                                   action:@selector(invokeAction:)
                                            keyEquivalent:@"q"];
        [q setTarget:g_target];
        [q setTag:idx];
        [menu addItem:q];
    }
    return menu;
}

/// 状态栏图标：优先用 SF Symbol（模板图，深色菜单栏自动反色）；
/// 拿不到就退回一个文字图标 —— 宁可丑，也不要「菜单栏上什么都不出现」。
void applyStatusIcon(NSStatusItem* item) {
    NSStatusBarButton* button = [item button];
    if (@available(macOS 11.0, *)) {
        NSImage* img = [NSImage imageWithSystemSymbolName:@"computermouse"
                                 accessibilityDescription:@"全能外设"];
        if (img) {
            [img setTemplate:YES];
            [button setImage:img];
            return;
        }
    }
    [button setTitle:@"🖱"];
}

/// 贴 tooltip + 发一条用户通知。**调用者必须已在托盘线程上**（见 notify()）。
void deliverNotification(NSString* title, NSString* body) {
    if (g_statusItem) {
        // 不管通知能不能弹出来，先把 tooltip 更新 —— 这是**一定**能生效的那条路。
        [g_statusItem.button setToolTip:[NSString stringWithFormat:@"%@：%@", title, body]];
    }
    // NSUserNotification 自 macOS 11 起被标记废弃（新 API 是 UNUserNotificationCenter，
    // 但它要求 bundle + 用户授权，本宿主未必满足）。这里仍用它：能弹就弹，
    // 弹不出来也已经在日志与 tooltip 里留了痕，不静默。
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSUserNotification* n = [[NSUserNotification alloc] init];
    if (n) {
        [n setTitle:title];
        [n setInformativeText:body];
        [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:n];
    }
#pragma clang diagnostic pop
}

}  // namespace

namespace apxpc::tray {

// 构造/析构**不在本文件**：tray_win32.cpp 里那两个定义在 `#if defined(_WIN32)` 之外
// （共用于所有平台），这里再给一份就是 duplicate symbol —— CI 已经踩过一次。
// 它的 ~TrayIcon() 会调 quit()，正好落到本文件的实现上（幂等，重复调无副作用）。

bool TrayIcon::create(const std::string& tip) {
    // 组装菜单计划：**谁注册了才出谁**。mac 上没有系统文件对话框、也没有 WebView2，
    // 所以文件/剪贴板/远程输入/诊断这些项在 mac 上默认不注册（注册了也做不了事），
    // 只剩「打开面板」与「退出」两项 —— 它们正是这次要补的东西。
    auto plan = std::make_shared<MenuPlan>();
    if (onOpen_)  plan->items.emplace_back("打开面板", onOpen_);
    if (onSendFile_) plan->items.emplace_back("发送文件到手机…", onSendFile_);
    if (onFilePanel_) plan->items.emplace_back("文件传输…", onFilePanel_);
    if (onClipboard_) plan->items.emplace_back("把电脑剪贴板发给手机", onClipboard_);
    if (onInput_) plan->items.emplace_back("让手机帮我输入…", onInput_);
    if (onDiagnose_) plan->items.emplace_back("连接诊断…", onDiagnose_);
    if (onExportDiag_) plan->items.emplace_back("导出诊断包…", onExportDiag_);
    // 捕 onQuit_ 的**副本**，不捕 this：菜单由 NSStatusItem 持有，生命周期可能长过
    // TrayIcon 本身（析构 → quit() 摘图标是异步的），捕 this 就是野指针。
    auto quitFn = onQuit_;
    plan->quit = [quitFn] {
        // 菜单里的「退出」= 把这个宿主请下去。真正的收尾在面板线程上按既有顺序做
        // （停 9511 → 停互传 → join 工作线程），所以这里只回调宿主给的那个入口。
        if (quitFn) quitFn();
    };

    const std::string tipCopy = tip;
    {
        // 清空上一轮的动作表：菜单项 tag 是这张表的下标，重复 create 会错位
        // （点「打开面板」结果执行了「退出」这种事最难查）。
        std::lock_guard<std::mutex> lk(g_actionsMu);
        g_actions.clear();
    }
    {
        std::lock_guard<std::mutex> lk(g_shared.mu);
        g_shared.ready = false;
        g_shared.ok = false;
    }
    g_shared.stopped.store(false);   // 允许再次 quit（重复 create 的场景）

    NSThread* th = [[NSThread alloc] initWithBlock:^{
        @autoreleasepool {
            // NSApplication 必须先有实例，NSStatusItem 才有落脚点。
            // activationPolicy=Accessory：只在菜单栏出现，不进 Dock、不抢焦点。
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

            g_target = [[APXTrayTarget alloc] init];
            g_statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
            if (g_statusItem == nil) {
                APX_LOGW("mac 托盘：statusItemWithLength 返回 nil（无法挂状态栏项）");
                std::lock_guard<std::mutex> lk(g_shared.mu);
                g_shared.ready = true;
                g_shared.ok = false;
                g_shared.cv.notify_all();
                return;
            }
            applyStatusIcon(g_statusItem);
            [g_statusItem setMenu:buildMenu(*plan)];
            [g_statusItem.button setToolTip:[NSString stringWithUTF8String:tipCopy.c_str()]];

            APX_LOGI("mac 托盘已就绪（菜单栏图标）");
            {
                std::lock_guard<std::mutex> lk(g_shared.mu);
                g_shared.ready = true;
                g_shared.ok = true;
                g_shared.cv.notify_all();
            }

            [NSApp run];   // 阻塞到 requestStop

            // —— 停下来了：把状态栏项摘掉，别留一个点了没反应的图标 ——
            if (g_statusItem) {
                [[NSStatusBar systemStatusBar] removeStatusItem:g_statusItem];
                g_statusItem = nil;
            }
            APX_LOGI("mac 托盘已停止");
        }
    }];
    [th setName:@"apx-tray-mac"];
    g_trayThread = th;
    [th start];

    // 等图标真的挂上再返回：调用方（desktop_main）会按返回值决定要不要打日志，
    // 不能先骗他说成功、其实那条线程已经退出了。
    std::unique_lock<std::mutex> lk(g_shared.mu);
    const bool signaled = g_shared.cv.wait_for(lk, std::chrono::milliseconds(2000),
                                              [] { return g_shared.ready; });
    if (!signaled) {
        APX_LOGW("mac 托盘：2 秒内没等到状态栏项就绪（继续跑，不影响面板）");
        return false;
    }
    return g_shared.ok;
}

void TrayIcon::quit() {
    // 幂等：析构也会调到这里。
    if (g_shared.stopped.exchange(true)) return;

    NSThread* th = g_trayThread;
    APXTrayTarget* target = g_target;
    if (th != nil && target != nil && ![th isFinished]) {
        // 跨线程 marshalling：requestStop 必须在托盘线程上执行。
        [target performSelector:@selector(requestStop) onThread:th withObject:nil waitUntilDone:NO];
        // 给一小段时间让 run loop 退出；不 join（NSThread 没有 join 语义，
        // 卡在这里会让「退出」按钮看起来没反应）。
        for (int i = 0; i < 20 && ![th isFinished]; ++i) {
            [NSThread sleepForTimeInterval:0.05];
        }
    }
    g_statusItem = nil;
    g_target = nil;
    g_trayThread = nil;

    // 旧的非 Windows 桩就是这样：quit() 直接回调 onQuit_（apxhost serve 的退出路径
    // 依赖它把主循环放下来）。保持语义不变。
    if (onQuit_) onQuit_();
}

void TrayIcon::setQuitCallback(std::function<void()> cb) { onQuit_ = std::move(cb); }
void TrayIcon::setOpenCallback(std::function<void()> cb) { onOpen_ = std::move(cb); }
void TrayIcon::setSendFileCallback(std::function<void()> cb) { onSendFile_ = std::move(cb); }

void TrayIcon::notify(const std::string& title, const std::string& text) {
    @autoreleasepool {
        NSString* t = [NSString stringWithUTF8String:title.c_str()];
        NSString* b = [NSString stringWithUTF8String:text.c_str()];
        if (!t) t = @"";
        if (!b) b = @"";

        // 调用方主要是**面板线程**（收到文件时 panel_http 直接调 bundle.tray->notify）。
        // AppKit 对象只能在自己线程上碰，跨线程直接动 NSStatusItem / 通知中心是未定义
        // 行为 —— 所以不在托盘线程就 marshalling 回去（NSApp run 的 run loop 会处理）。
        NSThread* tray = g_trayThread;
        if (tray != nil && [NSThread currentThread] != tray && g_target != nil) {
            [g_target performSelector:@selector(showNotification:)
                             onThread:tray
                           withObject:@[t, b]
                        waitUntilDone:NO];
        } else {
            deliverNotification(t, b);
        }
        APX_LOGI("托盘通知：{} — {}", title.c_str(), text.c_str());
    }
}

}  // namespace apxpc::tray
