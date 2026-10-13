// WebView2 桌面控制面板实现 —— UI 层全部交给 React + Tailwind（pc/web/）。
//
// 分工：
//   ① CreateCoreWebView2Environment → ICoreWebView2 → Navigate(dist/index.html)
//   ② 400ms WM_TIMER → 构建状态 JSON → PostWebMessageAsJson → 前端渲染
//   ③ 前端 postMessage → parse JSON → act/query/open-file 分发 → 宿主对象 → 回传
//
// 所有 COM 指针用 wil::com_ptr 管理（自动 Release），避免手动 AddRef/Release 漏写。
// 构建期：CMake FetchContent 拉 Microsoft.Web.WebView2 nuget 包，WebView2Loader.dll
// 在 build 目录，post-build 拷贝到 apxdesktop.exe 同目录。
//
// 崩溃取证、防火墙放行、9511 受控端注册 仍由 desktop_main.cpp 负责 —— 本文件不越界。

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <objbase.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

#include "apxpc/ui/web_panel.hpp"
#include "apxpc/ui/webview2_runtime.hpp"
#include "apxpc/log.hpp"
#include "apxpc/net/json.hpp"
#include "apxpc/wireless/wireless_session.hpp"
#include "apxpc/wireless/file_receiver.hpp"
#include "apxpc/media/media_session.hpp"
#include "apxpc/media/screen_push.hpp"
#include "apxpc/media/audio_capture.hpp"
#include "apxpc/media/mic_bridge.hpp"
#include "apxpc/autostart.hpp"

#include <wil/com.h>
#include <WebView2.h>

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace apxpc::ui {
namespace {

using apxpc::net::Json;
using apxpc::wireless::LinkPhase;
using apxpc::wireless::SessionSnapshot;

#define COM_CALLBACK_BASE(IFACE, UUID) \
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refs); } \
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refs); if (r == 0) delete this; return r; } \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { \
        if (riid == __uuidof(IFACE) || riid == IID_IUnknown) { *ppv = static_cast<IFACE*>(this); AddRef(); return S_OK; } \
        return E_NOINTERFACE; \
    }

template <typename Fn>
class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
public:
    explicit EnvCompletedHandler(Fn fn) : m_fn(std::move(fn)) {}
    COM_CALLBACK_BASE(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, 0)
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env) override { return m_fn(hr, env); }
protected: Fn m_fn; long m_refs{1};
};

template <typename Fn>
class CtrlCompletedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:
    explicit CtrlCompletedHandler(Fn fn) : m_fn(std::move(fn)) {}
    COM_CALLBACK_BASE(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, 0)
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* ctrl) override { return m_fn(hr, ctrl); }
protected: Fn m_fn; long m_refs{1};
};

template <typename Fn>
class WebMsgReceivedHandler : public ICoreWebView2WebMessageReceivedEventHandler {
public:
    explicit WebMsgReceivedHandler(Fn fn) : m_fn(std::move(fn)) {}
    COM_CALLBACK_BASE(ICoreWebView2WebMessageReceivedEventHandler, 0)
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) override { return m_fn(sender, args); }
protected: Fn m_fn; long m_refs{1};
};

template <typename Fn> ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* mkEnvCb(Fn fn) { return new EnvCompletedHandler<Fn>(std::move(fn)); }
template <typename Fn> ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*    mkCtrlCb(Fn fn) { return new CtrlCompletedHandler<Fn>(std::move(fn)); }
template <typename Fn> ICoreWebView2WebMessageReceivedEventHandler*                 mkMsgCb(Fn fn) { return new WebMsgReceivedHandler<Fn>(std::move(fn)); }

constexpr UINT_PTR kStateTimer = 1;
constexpr UINT kMsgPostQuitCustom = WM_APP + 10;
constexpr UINT kMsgWv2Ready = WM_APP + 11;

LPCWSTR kClassName = L"AllPeriphWebPanel";
LPCWSTR kWindowTitle = L"全能外设";

struct Panel {
    HWND hwnd = nullptr;
    HostBundle bundle;
    wil::com_ptr<ICoreWebView2Environment> env;
    wil::com_ptr<ICoreWebView2Controller> controller;
    wil::com_ptr<ICoreWebView2> webview;
    std::atomic<bool> running{false};
    std::wstring lastRepaintKey;
};
Panel* g_panel = nullptr;

Json okResp(const Json& data = {}) {
    Json r = Json::makeObject(); r["ok"] = true; r["data"] = data; return r;
}
Json failResp(const std::string& msg, int code = 1) {
    Json r = Json::makeObject(); r["ok"] = false; r["code"] = code; r["message"] = msg; return r;
}

std::string phaseName(LinkPhase p) {
    switch (p) {
        case LinkPhase::Connected:   return "connected";
        case LinkPhase::Connecting:  return "connecting";
        case LinkPhase::Discovering: return "discovering";
        case LinkPhase::Failed:      return "failed";
        default:                     return "idle";
    }
}

void buildState(const HostBundle& b, Json& out) {
    out = Json::makeObject();
    Json link = Json::makeObject();
    Json media = Json::makeObject();
    Json transports = Json::makeObject();
    Json counts = Json::makeObject();
    Json cfg = Json::makeObject();

    if (b.session) {
        const auto s = b.session->snapshot();
        link["phase"] = std::to_string(static_cast<int>(s.phase));
        link["phaseName"] = phaseName(s.phase);
        link["peer"] = s.peer;
        link["rttMs"] = s.rttMs;
        link["upMs"] = static_cast<double>(s.upMs);
        link["error"] = s.error;
        link["autoMode"] = s.autoMode;
        const auto& c = s.counters;
        counts["mouse"]    = static_cast<double>(c.mouse);
        counts["keyboard"] = static_cast<double>(c.keyboard);
        counts["consumer"] = static_cast<double>(c.consumer);
        counts["dropped"]  = static_cast<double>(c.dropped);
    } else {
        link["phase"] = static_cast<int>(LinkPhase::Idle);
        link["phaseName"] = L"idle";
    }

    if (b.media) {
        const auto st = b.media->status();
        const auto cc = b.media->counters();
        media["connected"] = st.connected;
        media["peer"] = st.peer;
        media["error"] = st.error;
        media["videoFramesSent"] = static_cast<double>(cc.videoFrames);
        media["audioFramesSent"] = static_cast<double>(cc.audioFrames);
        media["micFrames"] = static_cast<double>(cc.micFrames);
        media["dropped"] = static_cast<double>(cc.dropped);
    }

    if (b.screenPush) {
        auto s = b.screenPush->status();
        Json v = Json::makeObject();
        v["running"] = s.running; v["fps"] = s.fps; v["deviceName"] = s.deviceName;
        v["width"] = static_cast<double>(s.width); v["height"] = static_cast<double>(s.height);
        v["framesSent"] = static_cast<double>(s.framesSent); v["encodeMs"] = s.encodeMs;
        v["dropped"] = static_cast<double>(s.framesDropped); v["error"] = s.error;
        media["screen"] = v;
    }

    if (b.audio) {
        auto s = b.audio->status();
        Json a = Json::makeObject();
        a["running"] = s.running; a["peak"] = s.peak; a["device"] = s.device;
        a["sampleRate"] = static_cast<double>(s.sampleRate); a["channels"] = static_cast<double>(s.channels);
        a["framesSent"] = static_cast<double>(s.framesSent); a["error"] = s.error;
        media["audio"] = a;
    }

    if (b.micBridge) {
        auto s = b.micBridge->status();
        Json m = Json::makeObject(); m["running"] = s.running; m["device"] = s.device;
        media["mic"] = m;
    }

    if (b.cfg) {
        cfg["autostart"] = b.cfg->autostart;
        cfg["mode"] = b.cfg->mode;
        cfg["logLevel"] = b.cfg->logLevel;
        cfg["adaptiveBitrate"] = b.cfg->displayAdaptive;
        cfg["bitrateMbps"] = static_cast<double>(b.cfg->displayBitrateMbps);
        cfg["screenMode"] = static_cast<double>(b.cfg->displayEnabled ? 1 : 0);
        transports["btSupported"] = true;
    }

    out["link"] = link; out["media"] = media; out["transports"] = transports;
    out["counts"] = counts; out["config"] = cfg;
}

void tickState(Panel* p) {
    Json state;
    buildState(p->bundle, state);
    std::string json = state.stringify();
    std::wstring key;
    key.reserve(json.size());
    for (char c : json) key.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    if (key == p->lastRepaintKey) return;
    p->lastRepaintKey = key;
    Json push = Json::makeObject();
    push["kind"] = std::string("state"); push["t"] = static_cast<double>(GetTickCount64());
    push["data"] = state;
    std::string s = push.stringify();
    std::wstring w(s.begin(), s.end());
    p->webview->PostWebMessageAsJson(w.c_str());
}

void postResponse(Panel* p, const std::string& id, Json resp) {
    resp["id"] = id;
    std::string s = resp.stringify();
    std::wstring w(s.begin(), s.end());
    p->webview->PostWebMessageAsJson(w.c_str());
}

void postEvent(Panel* p, const std::string& name, Json data = Json::makeObject()) {
    Json push = Json::makeObject();
    push["kind"] = std::string("event"); push["name"] = name; push["data"] = data;
    std::string s = push.stringify();
    std::wstring w(s.begin(), s.end());
    p->webview->PostWebMessageAsJson(w.c_str());
}

void handleAct(Panel* p, const std::string& id, const std::string& name, const Json& payload) {
    const HostBundle& b = p->bundle;

    if (name == "session.startAuto" && b.session) {
        b.session->startAuto();
        return postResponse(p, id, okResp());
    }
    if (name == "session.connectManual" && b.session) {
        std::string host = payload.find("host") ? payload.find("host")->asString() : "";
        uint16_t port = static_cast<uint16_t>(payload.find("port") ? payload.find("port")->asInt(9511) : 9511);
        b.session->connectManual(host, port);
        return postResponse(p, id, okResp());
    }
    if (name == "session.disconnect" && b.session) {
        b.session->disconnect();
        return postResponse(p, id, okResp());
    }
    if (name == "session.sendInputText" && b.session) {
        std::string text = payload.find("text") ? payload.find("text")->asString() : "";
        uint8_t flags = static_cast<uint8_t>(payload.find("flags") ? payload.find("flags")->asInt(0x04) : 0x04);
        const bool ok = b.session->sendInputText(text, flags);
        return postResponse(p, id, ok ? okResp() : failResp("发送输入失败（未连接）"));
    }
    if (name == "session.sendInputDone" && b.session) {
        const bool ok = b.session->sendInputDone();
        return postResponse(p, id, ok ? okResp() : failResp("发送完成通知失败"));
    }

    if (name == "screen.toggle" && b.screenPush) {
        if (b.screenPush->running()) { b.screenPush->stop(); return postResponse(p, id, okResp()); }
        if (!b.media || !b.media->status().connected)
            return postResponse(p, id, failResp("媒体通道未连接（需与手机同一 Wi‑Fi）"));
        apxpc::media::ScreenPushOptions opt;
        opt.mirrorMode = false; opt.bitrateKbps = 8000;
        std::string err;
        const bool ok = b.screenPush->start(b.media, opt, &err);
        return postResponse(p, id, ok ? okResp() : failResp(err));
    }

    if (name == "speaker.toggle" && b.audio) {
        if (b.audio->running()) { b.audio->stop(); return postResponse(p, id, okResp()); }
        if (!b.media || !b.media->status().connected)
            return postResponse(p, id, failResp("媒体通道未连接"));
        apxpc::media::AudioCaptureOptions opt;
        auto* devId = payload.find("deviceId");
        if (devId) opt.deviceId = devId->asString();
        std::string err;
        const bool ok = b.audio->start(b.media, opt, &err);
        return postResponse(p, id, ok ? okResp() : failResp(err));
    }

    if (name == "mic.toggle" && b.micBridge) {
        if (b.micBridge->running()) { b.micBridge->stop(); return postResponse(p, id, okResp()); }
        std::string devId = payload.find("deviceId") ? payload.find("deviceId")->asString() : "";
        std::string err;
        const bool ok = b.micBridge->start(devId, &err);
        return postResponse(p, id, ok ? okResp() : failResp(err));
    }

    if (name == "config.setAdaptive" && b.cfg) {
        auto* v = payload.find("on");
        b.cfg->displayAdaptive = v ? v->asBool(b.cfg->displayAdaptive) : !b.cfg->displayAdaptive;
        if (!b.configPath.empty()) config::saveConfig(b.configPath, *b.cfg);
        Json d1 = Json::makeObject(); d1["adaptive"] = b.cfg->displayAdaptive;
        return postResponse(p, id, okResp(d1));
    }
    if (name == "config.setBitrate" && b.cfg) {
        auto* v = payload.find("mbps");
        if (v) b.cfg->displayBitrateMbps = static_cast<uint32_t>(v->asInt(b.cfg->displayBitrateMbps));
        if (!b.configPath.empty()) config::saveConfig(b.configPath, *b.cfg);
        Json d2 = Json::makeObject(); d2["bitrateMbps"] = static_cast<double>(b.cfg->displayBitrateMbps);
        return postResponse(p, id, okResp(d2));
    }

    if (name == "window.hide") {
        ::ShowWindow(p->hwnd, SW_HIDE);
        return postResponse(p, id, okResp());
    }
    if (name == "window.quit") {
        p->running.store(false);
        ::PostMessageW(p->hwnd, WM_CLOSE, 0, 0);
        return postResponse(p, id, okResp());
    }
    if (name == "tray.setAutostart") {
        bool enable = payload.find("enable") ? payload.find("enable")->asBool(false) : false;
        // ★ 已实现：通过注册表写入/删除开机自启项
        const bool ok = apxpc::autostart::setAutostart(enable);
        if (ok && b.cfg) {
            b.cfg->autostart = enable;
            if (!b.configPath.empty()) config::saveConfig(b.configPath, *b.cfg);
        }
        APX_LOGI("tray.setAutostart(%d) → %s", enable, ok ? "success" : "failed");
        Json d3 = Json::makeObject(); d3["ok"] = ok; d3["autostart"] = enable;
        return postResponse(p, id, okResp(d3));
    }
    if (name == "config.setLogLevel" && b.cfg) {
        auto* l = payload.find("level");
        if (l) b.cfg->logLevel = l->asString();
        if (!b.configPath.empty()) config::saveConfig(b.configPath, *b.cfg);
        return postResponse(p, id, okResp());
    }

    return postResponse(p, id, failResp("未知动作: " + name));
}

void handleQuery(Panel* p, const std::string& id, const std::string& name) {
    if (name == "state") {
        Json s; buildState(p->bundle, s);
        Json r; r["kind"] = std::string("query-response");
        return postResponse(p, id, okResp(s));
    }
    postResponse(p, id, failResp("未知查询: " + name));
}

void handleOpenFile(Panel* p, const std::string& id, const std::string& filter) {
    OPENFILENAMEW of{};
    wchar_t file[MAX_PATH] = {};
    wchar_t wfilter[MAX_PATH] = {};
    std::wstring wf = filter.empty() ? L"全部文件\0*.*\0" : std::wstring(filter.begin(), filter.end());
    std::replace(wf.begin(), wf.end(), L'|', L'\0');
    std::wcscpy(wfilter, wf.c_str());
    of.lStructSize = sizeof(of); of.hwndOwner = p->hwnd; of.nMaxFile = MAX_PATH;
    of.lpstrFile = file; of.lpstrFilter = wfilter;
    of.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    const bool ok = ::GetOpenFileNameW(&of) != FALSE;
    Json r; r["kind"] = std::string("open-file-response");
    if (ok) { std::string s; s.assign(file, file + wcslen(file)); r["ok"] = true; r["path"] = s; }
    else { r["ok"] = false; }
    postResponse(p, id, r);
}

void onWebMessageReceived(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
    LPWSTR rawJson = nullptr;
    if (FAILED(args->TryGetWebMessageAsString(&rawJson)) || !rawJson) return;
    std::wstring w(rawJson);
    ::CoTaskMemFree(rawJson);
    std::string s; s.reserve(w.size()); for (wchar_t c : w) s.push_back(static_cast<char>(c));
    std::string err;
    auto j = Json::parse(s, &err);
    if (j.isNull()) { APX_LOGW("WebView2: JSON 解析失败: {}", err.c_str()); return; }
    auto* kind = j.find("kind");
    if (!kind) return;
    const std::string k = kind->asString();
    auto* id = j.find("id");
    const std::string idStr = id ? id->asString() : "";
    Panel* p = g_panel;
    if (!p) return;
    if (k == "act") {
        auto* name = j.find("name");
        auto* payload = j.find("payload");
        handleAct(p, idStr, name ? name->asString() : "", payload ? *payload : Json::makeObject());
    } else if (k == "query") {
        auto* name = j.find("name");
        handleQuery(p, idStr, name ? name->asString() : "");
    } else if (k == "open-file") {
        auto* filter = j.find("filter");
        handleOpenFile(p, idStr, filter ? filter->asString() : "");
    }
}

void onWebView2EnvFailed(Panel* p, unsigned hresult) {
    APX_LOGE("WebView2 环境创建失败: HRESULT=0x{:08X}", hresult);
    if (isWebView2RuntimeInstalled()) {
        MessageBoxW(p->hwnd, L"WebView2 初始化失败（运行组件已安装仍无法创建环境）。\n详细错误码见日志。", L"全能外设", MB_OK | MB_ICONERROR);
        p->running.store(false);
        ::PostMessageW(p->hwnd, WM_CLOSE, 0, 0);
        return;
    }
    const INT r = MessageBoxW(p->hwnd, L"未检测到 WebView2 运行组件（本界面必需）。\n\n确定后将自动下载安装（来自微软官方源），\n安装完成后本窗口会自动打开界面。", L"全能外设", MB_OKCANCEL | MB_ICONINFORMATION);
    if (r != IDOK) { p->running.store(false); ::PostMessageW(p->hwnd, WM_CLOSE, 0, 0); return; }
    ::SetWindowTextW(p->hwnd, L"全能外设 — 正在下载安装 WebView2 运行组件…");
    const HWND hw = p->hwnd;
    std::thread([hw] {
        std::wstring werr;
        if (ensureWebView2Runtime(werr, false)) { ::PostMessageW(hw, kMsgWv2Ready, 0, 0); }
        else {
            MessageBoxW(nullptr, (L"WebView2 运行组件自动安装失败：\n" + werr + L"\n\n可手动下载安装：https://aka.ms/webviewruntime").c_str(), L"全能外设", MB_OK | MB_ICONERROR);
            ::PostMessageW(hw, WM_CLOSE, 0, 0);
        }
    }).detach();
}

void startWebView2Env(Panel* p) {
    wchar_t exeDir[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, exeDir, MAX_PATH) > 0) {
        wchar_t* slash = wcsrchr(exeDir, L'\\');
        if (slash) *slash = L'\0';
    }
    std::wstring dataDir(exeDir); dataDir += L"\\apxwebview2-data";
    std::wstring wdir(exeDir);    wdir += L"\\web\\index.html";
    if (*exeDir == L'\0') { wdir = L".\\web\\index.html"; }

        auto* envCb = mkEnvCb(
            [p, exeDir, dataDir, wdir](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr)) { onWebView2EnvFailed(p, static_cast<unsigned>(hr)); return S_OK; }
                p->env = env;
                auto* ctrlCb = mkCtrlCb(
                    [p, wdir](HRESULT hr3, ICoreWebView2Controller* controller) -> HRESULT {
                        if (FAILED(hr3) || !controller) { APX_LOGE("CreateCoreWebView2Controller 失败: 0x{:08X}", static_cast<unsigned>(hr3)); return S_OK; }
                        p->controller = controller;
                        wil::com_ptr<ICoreWebView2> wv;
                        if (!SUCCEEDED(controller->get_CoreWebView2(&wv))) { APX_LOGE("controller->get_CoreWebView2 失败"); return S_OK; }
                        p->webview = wv;
                        wil::com_ptr<ICoreWebView2Settings> settings;
                        if (SUCCEEDED(wv->get_Settings(&settings))) {
                            settings->put_AreDevToolsEnabled(TRUE);
                            settings->put_AreDefaultContextMenusEnabled(FALSE);
                        }
                        wv->add_WebMessageReceived(
                            mkMsgCb([](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                onWebMessageReceived(nullptr, args); return S_OK;
                            }), nullptr);
                        { std::string wdirA; wdirA.reserve(wdir.size()); for (wchar_t c : wdir) wdirA.push_back(static_cast<char>(c));
                        APX_LOGI("WebView2 加载: {}", wdirA.c_str()); }
                        wv->Navigate(wdir.c_str());
                        if (p->bundle.session) {
                            p->bundle.session->setOnRequestInput([p](const std::string& hint) {
                                Json d = Json::makeObject(); d["hint"] = hint;
                                postEvent(p, std::string("on-remote-input-request"), d);
                            });
                        }
                        ::SetTimer(p->hwnd, kStateTimer, 400, nullptr);
                        APX_LOGI("WebView2 面板就绪");
                        return S_OK;
                    });
                env->CreateCoreWebView2Controller(p->hwnd, ctrlCb);
                return S_OK;
            });
        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.empty() ? nullptr : dataDir.c_str(), nullptr, envCb);
        if (FAILED(hr)) onWebView2EnvFailed(p, static_cast<unsigned>(hr));
    }

LRESULT CALLBACK webWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        Panel* p = reinterpret_cast<Panel*>(cs->lpCreateParams);
        p->hwnd = hwnd; ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
        startWebView2Env(p); return 0;
    }
    if (msg == kMsgWv2Ready) {
        ::SetWindowTextW(hwnd, kWindowTitle);
        Panel* p = reinterpret_cast<Panel*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (p) startWebView2Env(p); return 0;
    }
    if (msg == WM_TIMER && wp == kStateTimer) {
        Panel* p = reinterpret_cast<Panel*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (p && p->webview) tickState(p); return 0;
    }
    if (msg == WM_SIZE) {
        Panel* p = reinterpret_cast<Panel*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (p && p->controller) { RECT rc; ::GetClientRect(hwnd, &rc); p->controller->put_Bounds({0, 0, rc.right, rc.bottom}); }
        return 0;
    }
    if (msg == WM_DESTROY) { ::KillTimer(hwnd, kStateTimer); Panel* p = reinterpret_cast<Panel*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA)); if (p) p->running.store(false); return 0; }
    if (msg == WM_CLOSE) { ::DestroyWindow(hwnd); return 0; }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

bool webPanelAvailable() {
#if defined(_WIN32)
    wil::com_ptr<ICoreWebView2Environment> env;
    HANDLE doneEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!doneEvent) return false;
    HRESULT waitHResult = E_FAIL;
    auto* cb = mkEnvCb([&](HRESULT h, ICoreWebView2Environment* e) -> HRESULT {
        waitHResult = h; if (SUCCEEDED(h) && e) env = e; ::SetEvent(doneEvent); return S_OK;
    });
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr, cb);
    bool ok = false;
    if (SUCCEEDED(hr)) { ok = (::WaitForSingleObject(doneEvent, 15000) == WAIT_OBJECT_0) && SUCCEEDED(waitHResult) && env; }
    ::CloseHandle(doneEvent); return ok;
#else
    return false;
#endif
}

int runWebPanel(const HostBundle& bundle, const std::string& /*webDir*/) {
#if !defined(_WIN32)
    (void)bundle; return -1;
#else
    Panel panel;
    panel.bundle = bundle;
    g_panel = &panel;
    panel.running.store(true);

    // ★ 启动时同步 autostart 状态：如果 config 里 autostart=true 但注册表没有，补写
    if (bundle.cfg && bundle.cfg->autostart) {
        if (!apxpc::autostart::isAutostartEnabled()) {
            APX_LOGI("启动同步: config autostart=true 但注册表无，补写");
            apxpc::autostart::setAutostart(true);
        }
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = webWndProc;
    wc.hInstance = GetModuleHandleW(nullptr); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    const int kDefaultCssW = 960; const int kDefaultCssH = 808;
    const int sysDpi = ::GetDpiForSystem();
    const int cx = sysDpi * kDefaultCssW / 96; const int cy = sysDpi * kDefaultCssH / 96;
    RECT frame{0, 0, cx, cy};
    ::AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW | WS_SIZEBOX, FALSE, 0);

    HWND hwnd = ::CreateWindowExW(0, kClassName, kWindowTitle, WS_OVERLAPPEDWINDOW | WS_SIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, frame.right - frame.left, frame.bottom - frame.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), &panel);
    if (!hwnd) { APX_LOGE("Web 窗口创建失败: GetLastError={}", ::GetLastError()); return -1; }
    panel.hwnd = hwnd;
    ::ShowWindow(hwnd, SW_SHOW); ::UpdateWindow(hwnd);

    MSG msg;
    while (panel.running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    g_panel = nullptr; return 0;
#endif
}

}  // namespace apxpc::ui