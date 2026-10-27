// panel_http.cpp —— 浏览器面板后端（方案 B：本地 HTTP + React 前端）。
//
// 与 web_panel.cpp（WebView2 壳）同构 —— 协议、状态结构、动作分发完全一致：
//   GET  /api/state           → {"kind":"state","data":{…}}（前端 400ms 轮询）
//   GET  /api/events?since=N  → {"events":[{…event…,"seq":N}]}（事件增量轮询）
//   POST /api/act|query       → web_panel 同款 handleAct/query
//   POST /api/upload?ip&port  → 浏览器面板的「互传发送」
//   POST /api/open-file       → {"kind":"open-file-response","ok":false}
//   GET  /*                   → 静态 serve 前端 dist

#include "apxpc/ui/panel_http.hpp"

#include "apxpc/autostart.hpp"
#include "apxpc/config/app_config.hpp"
#include "apxpc/discovery/discovery.hpp"
#include "apxpc/discovery/enumerator.hpp"
#include "apxpc/log.hpp"
#include "apxpc/localsend/local_send_receiver.hpp"
#include "apxpc/localsend/local_send_sender.hpp"
#include "apxpc/net/json.hpp"
#include "apxpc/tray/tray_win32.hpp"
#include "apxpc/wireless/ctrl9511.hpp"
#include "apxpc/wireless/file_receiver.hpp"
#include "apxpc/wireless/wireless_session.hpp"
#include "apxpc/media/media_session.hpp"
#include "apxpc/media/audio_capture.hpp"
#include "apxpc/media/mic_bridge.hpp"

#if !defined(_WIN32)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace apxpc::ui {
namespace {

using apxpc::net::Json;
using apxpc::wireless::LinkPhase;

namespace fs = std::filesystem;

uint64_t steadyMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct EventEntry { long long seq = 0; std::string json; };
std::mutex g_evMu;
std::deque<EventEntry> g_events;
long long g_evSeq = 0;
constexpr size_t kEvCap = 256;

void pushEvent(const std::string& name, Json data = Json::makeObject()) {
    Json e = Json::makeObject();
    std::lock_guard<std::mutex> lk(g_evMu);
    e["seq"] = static_cast<double>(++g_evSeq);
    e["kind"] = std::string("event");
    e["name"] = name;
    e["data"] = data;
    g_events.push_back({g_evSeq, e.stringify()});
    while (g_events.size() > kEvCap) g_events.pop_front();
}

struct HttpPanel {
    HostBundle bundle;
    std::atomic<bool> running{false};
    std::string webDir;
    uint16_t port = 0;

    apxpc::wireless::Ctrl9511Server ctrlSrv;

    bool usbScanned = false;
    bool usbDevice = false, usbNcm = false, usbHid = false;
    std::string usbName, usbSpeed;
    uint64_t usbNextMs = 0;

    std::thread mediaThread;
    std::atomic<bool> mediaBusy{false};
    uint64_t mediaNextMs = 0;

    std::thread sendThread;
    std::atomic<bool> sendBusy{false};

    Json resp;
    bool hasResp = false;

    void resetResp() { resp = Json(); hasResp = false; }
};
HttpPanel* g_hp = nullptr;

Json okResp(const Json& data = Json::makeObject()) {
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

void postResponse(HttpPanel* p, const std::string& id, Json r, const char* kind = "act-response") {
    if (!r.find("kind")) r["kind"] = std::string(kind);
    r["id"] = id;
    p->resp = r;
    p->hasResp = true;
}

void refreshUsbStatus(HttpPanel* p) {
    p->usbDevice = p->usbNcm = p->usbHid = false;
    p->usbName.clear();
    p->usbSpeed.clear();
    try {
        for (const auto& d : apxpc::usb::enumerateDevices()) {
            if (!p->usbDevice) {
                p->usbDevice = true;
                p->usbName = d.displayName.empty() ? d.vidPid() : d.displayName;
                p->usbSpeed = apxpc::discovery::speedName(d.speed);
            }
            if (d.has(apxpc::usb::Role::Ncm)) p->usbNcm = true;
            if (d.has(apxpc::usb::Role::Hid)) p->usbHid = true;
        }
        p->usbScanned = true;
    } catch (...) {
        APX_LOGW("USB 枚举失败，按未检测到设备呈现");
        p->usbScanned = true;
    }
}

void buildState(const HostBundle& b, Json& out, HttpPanel* p) {
    out = Json::makeObject();
    Json link = Json::makeObject();
    Json media = Json::makeObject();
    Json transports = Json::makeObject();
    Json counts = Json::makeObject();
    Json cfg = Json::makeObject();

    if (b.session) {
        const auto s = b.session->snapshot();
        link["phase"] = static_cast<double>(s.phase);
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
        link["phase"] = static_cast<double>(LinkPhase::Idle);
        link["phaseName"] = std::string("idle");
    }

    // 键盘/触摸板诊断 + 实体键盘状态（被控端 Ctrl9511Server 收到的输入）
    {
        auto cst = p->ctrlSrv.status();
        Json ri = Json::makeArray();
        for (const auto& r : cst.recentInputs) ri.push(Json(r));
        out["recentInputs"] = ri;
        Json pkb = Json::makeObject();
        pkb["available"] = cst.padKbAvailable;
        pkb["name"] = cst.padKbName;
        out["padKb"] = pkb;
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

    {
        Json v = Json::makeObject();
        v["running"] = false;
        v["fps"] = 0;
        v["deviceName"] = std::string("");
        v["width"] = 0;
        v["height"] = 0;
        v["framesSent"] = 0;
        v["encodeMs"] = 0;
        v["dropped"] = 0;
        v["error"] = std::string("此平台不支持副屏（虚拟显示器仅 Windows）");
        media["screen"] = v;
    }

    if (b.audio) {
        auto s = b.audio->status();
        Json a = Json::makeObject();
        a["running"] = s.running;
        a["peak"] = s.peak;
        a["device"] = s.device;
        a["sampleRate"] = static_cast<double>(s.sampleRate);
        a["channels"] = static_cast<double>(s.channels);
        a["framesSent"] = static_cast<double>(s.framesSent);
        a["error"] = s.error;
        media["audio"] = a;
    }

    if (b.micBridge) {
        auto s = b.micBridge->status();
        Json m = Json::makeObject();
        m["running"] = s.running;
        m["device"] = s.device;
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

    if (p && p->usbScanned) {
        transports["usbDevice"] = p->usbDevice;
        transports["usbNcm"] = p->usbNcm;
        transports["usbHid"] = p->usbHid;
        transports["usbName"] = p->usbName;
        transports["usbSpeed"] = p->usbSpeed;
    }

    if (b.localSend) {
        std::string lerr;
        out["localsend"] = Json::parse(apxpc::localsend::statusJson(b.localSend->snapshot()), &lerr);
    } else {
        Json ls = Json::makeObject();
        ls["running"] = false;
        ls["port"] = 0;
        ls["dir"] = std::string("");
        ls["received"] = 0;
        ls["error"] = std::string("互传接收端未初始化");
        ls["warn"] = std::string("");
        ls["devices"] = Json::makeArray();
        out["localsend"] = ls;
    }

    out["link"] = link;
    out["media"] = media;
    out["transports"] = transports;
    out["counts"] = counts;
    out["config"] = cfg;
}

void mediaPumpTick(HttpPanel* p) {
    const HostBundle& b = p->bundle;
    if (!b.media || !b.session || b.media->status().connected) return;
    const auto snap = b.session->snapshot();
    const std::string peer = snap.peer;
    const size_t colon = peer.rfind(':');
    const std::string host = (colon == std::string::npos) ? peer : peer.substr(0, colon);
    if (snap.phase != LinkPhase::Connected || host.empty()) return;
    if (p->mediaBusy.load()) return;
    const uint64_t now = steadyMs();
    if (now < p->mediaNextMs) return;
    p->mediaNextMs = now + 4000;
    if (p->mediaThread.joinable()) p->mediaThread.join();
    p->mediaBusy.store(true);
    apxpc::media::MediaSession* media = b.media;
    p->mediaThread = std::thread([p, media, host] {
        if (media->connect(host, apxpc::media::kMediaPort)) {
            APX_LOGI("媒体通道已连接 {}:{}", host.c_str(),
                     static_cast<unsigned>(apxpc::media::kMediaPort));
        }
        p->mediaBusy.store(false);
    });
}

void openDirInFileManager(const std::string& dir) {
#if defined(__APPLE__)
    const char* tool = "open";
#else
    const char* tool = "xdg-open";
#endif
    const pid_t pid = ::fork();
    if (pid < 0) {
        APX_LOGW("打开目录失败（fork 失败）: {}", dir.c_str());
        return;
    }
    if (pid == 0) {
        ::execlp(tool, tool, dir.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    APX_LOGI("已请求打开互传目录: {}", dir.c_str());
}

bool startLocalSend(const HostBundle& b) {
    if (!b.localSend) return false;
    if (b.localSend->running()) return true;

    apxpc::localsend::Options opt;
    opt.dir = apxpc::wireless::FileReceiver::defaultDir();
    const bool ok = b.localSend->start(
        opt, [](const std::string& path, uint64_t size, const std::string& from) {
            if (g_hp && g_hp->bundle.tray) {
                const size_t slash = path.find_last_of('/');
                const std::string name =
                    (slash == std::string::npos) ? path : path.substr(slash + 1);
                g_hp->bundle.tray->notify("收到文件", from + " 发来 " + name);
            }
            Json d = Json::makeObject();
            d["path"] = path;
            d["size"] = static_cast<double>(size);
            d["from"] = from;
            d["via"] = std::string("localsend");
            pushEvent("on-localsend-received", d);
        });
    if (ok)
        APX_LOGI("互传接收端已启动：端口 {}", std::to_string(b.localSend->listenPort()).c_str());
    else
        APX_LOGW("互传接收端启动失败：{}", b.localSend->lastError().c_str());
    return ok;
}

Json localSendStatusJson(const HostBundle& b) {
    std::string err;
    return Json::parse(apxpc::localsend::statusJson(b.localSend->snapshot()), &err);
}

bool startLocalSendToPeer(HttpPanel* p, const std::string& ip, uint16_t port,
                          const std::vector<std::string>& paths) {
    if (p->sendBusy.exchange(true)) return false;
    apxpc::localsend::Target target;
    target.ip = ip;
    target.port = port;
    std::vector<apxpc::localsend::OutgoingFile> files;
    files.reserve(paths.size());
    for (const auto& f : paths) files.push_back({f, {}});

    if (p->sendThread.joinable()) p->sendThread.join();
    p->sendThread = std::thread([p, target, files] {
        apxpc::localsend::LocalSendSender tx;
        apxpc::localsend::SenderIdentity self;
        const auto res = tx.send(target, self, files);
        p->sendBusy.store(false);
        Json d = Json::makeObject();
        d["ok"] = res.ok;
        d["ip"] = target.ip;
        d["port"] = static_cast<double>(target.port);
        d["peerAlias"] = res.peerAlias;
        d["error"] = res.error;
        d["count"] = static_cast<double>(res.deliveredNames.size());
        d["bytes"] = static_cast<double>(res.bytesSent);
        pushEvent("on-localsend-sent", d);
    });
    return true;
}

void handleAct(HttpPanel* p, const std::string& id, const std::string& name, const Json& payload) {
    APX_LOGI("handleAct: name={} id={}", name.c_str(), id.c_str());
    const HostBundle& b = p->bundle;

    // —— 连接控制 ——
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
    if (name == "session.scanLan") {
        auto ips = apxpc::wireless::scanLan9511(250);
        Json out = Json::makeObject();
        Json arr = Json::makeArray();
        for (const auto& ip : ips) arr.push(Json(ip));
        out["ips"] = arr;
        return postResponse(p, id, okResp(out));
    }

    // —— 远程输入 ——
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

    if (name == "screen.toggle") {
        return postResponse(p, id, failResp("此平台不支持副屏（虚拟显示器仅 Windows）"));
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
        return postResponse(p, id, ok ? okResp()
                                      : failResp(err.empty() ? "音箱启动失败" : err));
    }

    if (name == "mic.toggle" && b.micBridge) {
        if (b.micBridge->running()) { b.micBridge->stop(); return postResponse(p, id, okResp()); }
        std::string devId = payload.find("deviceId") ? payload.find("deviceId")->asString() : "";
        std::string err;
        const bool ok = b.micBridge->start(devId, &err);
        return postResponse(p, id, ok ? okResp()
                                      : failResp(err.empty() ? "麦克风桥启动失败" : err));
    }

    if (name == "config.setAdaptive" && b.cfg) {
        auto* v = payload.find("on");
        b.cfg->displayAdaptive = v ? v->asBool(b.cfg->displayAdaptive) : !b.cfg->displayAdaptive;
        if (!b.configPath.empty()) apxpc::config::saveConfig(b.configPath, *b.cfg);
        Json d1 = Json::makeObject(); d1["adaptive"] = b.cfg->displayAdaptive;
        return postResponse(p, id, okResp(d1));
    }
    if (name == "config.setBitrate" && b.cfg) {
        auto* v = payload.find("mbps");
        if (v) b.cfg->displayBitrateMbps = static_cast<uint32_t>(v->asInt(b.cfg->displayBitrateMbps));
        if (!b.configPath.empty()) apxpc::config::saveConfig(b.configPath, *b.cfg);
        Json d2 = Json::makeObject(); d2["bitrateMbps"] = static_cast<double>(b.cfg->displayBitrateMbps);
        return postResponse(p, id, okResp(d2));
    }
    if (name == "config.setLogLevel" && b.cfg) {
        auto* l = payload.find("level");
        if (l) b.cfg->logLevel = l->asString();
        if (!b.configPath.empty()) apxpc::config::saveConfig(b.configPath, *b.cfg);
        return postResponse(p, id, okResp());
    }

    if (name == "window.hide") {
        return postResponse(p, id, okResp());
    }
    if (name == "window.quit") {
        p->running.store(false);
        return postResponse(p, id, okResp());
    }

    if (name == "tray.setAutostart") {
        bool enable = payload.find("enable") ? payload.find("enable")->asBool(false) : false;
        const bool ok = apxpc::autostart::setAutostart(enable);
        if (ok && b.cfg) {
            b.cfg->autostart = enable;
            if (!b.configPath.empty()) apxpc::config::saveConfig(b.configPath, *b.cfg);
        }
        APX_LOGI("tray.setAutostart({}) → {}", enable ? "on" : "off", ok ? "success" : "failed");
        Json d3 = Json::makeObject(); d3["ok"] = ok; d3["autostart"] = enable;
        return postResponse(p, id, okResp(d3));
    }

    if (name == "file.send") {
        return postResponse(p, id, failResp("浏览器面板不支持发送本机文件（无系统文件对话框），请在桌面客户端操作"));
    }
    if (name == "file.list") {
        struct Ent { std::string name; std::string path; long long size; long long ftick; };
        std::vector<Ent> ents;
        std::error_code ec;
        const fs::path dir = fs::path(apxpc::wireless::FileReceiver::defaultDir());
        if (!dir.empty() && fs::exists(dir, ec)) {
            fs::directory_iterator it(dir, ec), end;
            for (; !ec && it != end; it.increment(ec)) {
                std::error_code fec;
                if (!it->is_regular_file(fec)) continue;
                Ent e;
                e.path  = it->path().string();
                e.name  = it->path().filename().string();
                e.size  = static_cast<long long>(it->file_size(fec));
                e.ftick = static_cast<long long>(
                    it->last_write_time(fec).time_since_epoch().count());
                ents.push_back(std::move(e));
            }
        }
        std::sort(ents.begin(), ents.end(),
                  [](const Ent& a, const Ent& b) { return a.ftick > b.ftick; });
        Json arr = Json::makeArray();
        for (const auto& e : ents) {
            Json o = Json::makeObject();
            o["name"] = e.name;
            o["path"] = e.path;
            o["size"] = static_cast<double>(e.size);
            arr.push(o);
        }
        return postResponse(p, id, okResp(arr));
    }
    if (name == "file.delete") {
        const std::string path = payload.find("path") ? payload.find("path")->asString() : "";
        if (path.empty()) return postResponse(p, id, failResp("未指定文件"));
        std::error_code ec1, ec2, ec3;
        const fs::path root = fs::weakly_canonical(
            fs::path(apxpc::wireless::FileReceiver::defaultDir()), ec1);
        fs::path target = fs::weakly_canonical(fs::path(path), ec2);
        if (ec1 || ec2) return postResponse(p, id, failResp("路径无效"));
        auto norm = [](std::string s) {
            std::replace(s.begin(), s.end(), '\\', '/');
            for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return s;
        };
        const std::string r8 = norm(root.generic_string());
        const std::string t8 = norm(target.generic_string());
        if (t8.size() <= r8.size() + 1 || t8.compare(0, r8.size(), r8) != 0 ||
            t8[r8.size()] != '/') {
            return postResponse(p, id, failResp("只允许删除收到的文件（接收目录内）"));
        }
        if (!fs::remove(target, ec3) || ec3)
            return postResponse(p, id, failResp("删除失败: " + ec3.message()));
        APX_LOGI("已删除收到的文件: {}", path.c_str());
        return postResponse(p, id, okResp());
    }

    if (name == "localsend.status") {
        if (!b.localSend) return postResponse(p, id, failResp("互传接收端未初始化"));
        return postResponse(p, id, okResp(localSendStatusJson(b)));
    }
    if (name == "localsend.toggle") {
        if (!b.localSend) return postResponse(p, id, failResp("互传接收端未初始化"));
        const Json* en = payload.find("enable");
        const bool enable = (en && en->isBool()) ? en->asBool() : !b.localSend->running();
        if (enable) {
            if (!startLocalSend(b))
                return postResponse(p, id, failResp("互传接收端启动失败：" + b.localSend->lastError()));
        } else {
            b.localSend->stop();
            APX_LOGI("互传接收端已停止");
        }
        return postResponse(p, id, okResp(localSendStatusJson(b)));
    }
    if (name == "localsend.send") {
        const std::string ip = payload.find("ip") ? payload.find("ip")->asString() : "";
        const int port = payload.find("port") ? payload.find("port")->asInt(0) : 0;
        std::vector<std::string> paths;
        if (const Json* arr = payload.find("paths"); arr && arr->isArray()) {
            for (const auto& v : arr->asArray())
                if (v.isString() && !v.asString().empty()) paths.push_back(v.asString());
        }
        if (ip.empty() || port <= 0 || port > 65535)
            return postResponse(p, id, failResp("目标设备不合法（需要 ip 与 port）"));
        if (paths.empty()) return postResponse(p, id, failResp("未选择文件"));
        if (!startLocalSendToPeer(p, ip, static_cast<uint16_t>(port), paths))
            return postResponse(p, id, failResp("上一批文件还在发送中"));
        return postResponse(p, id, okResp());
    }
    if (name == "localsend.openDir") {
        if (!b.localSend) return postResponse(p, id, failResp("互传接收端未初始化"));
        const std::string dir = b.localSend->snapshot().dir;
        openDirInFileManager(dir);
        Json d = Json::makeObject();
        d["dir"] = dir;
        return postResponse(p, id, okResp(d));
    }

    return postResponse(p, id, failResp("未知动作: " + name));
}

void handleQuery(HttpPanel* p, const std::string& id, const std::string& name) {
    if (name == "state") {
        Json s;
        buildState(p->bundle, s, p);
        return postResponse(p, id, okResp(s), "query-response");
    }
    postResponse(p, id, failResp("未知查询: " + name), "query-response");
}

std::string guessMime(const std::string& path) {
    auto ends = [&](const char* ext) {
        const size_t n = std::strlen(ext);
        return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".js"))   return "text/javascript; charset=utf-8";
    if (ends(".css"))  return "text/css; charset=utf-8";
    if (ends(".json") || ends(".map")) return "application/json; charset=utf-8";
    if (ends(".svg"))  return "image/svg+xml";
    if (ends(".png"))  return "image/png";
    if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
    if (ends(".webp")) return "image/webp";
    if (ends(".ico"))  return "image/x-icon";
    if (ends(".woff2")) return "font/woff2";
    if (ends(".txt"))  return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percentDecode(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hexVal(s[i + 1]), lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                o.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        o.push_back(s[i]);
    }
    return o;
}

struct Req {
    std::string method, path, query, body;
    bool ok = false;
    bool streamBody = false;
    size_t contentLength = 0;
    std::string fileName;
};

constexpr size_t kMaxUploadBytes = 4ull * 1024 * 1024 * 1024;

Req readRequest(int fd) {
    Req r;
    std::string buf;
    char tmp[4096];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return r;
        buf.append(tmp, static_cast<size_t>(n));
        if (buf.size() > 64 * 1024) return r;
        headerEnd = buf.find("\r\n\r\n");
    }
    const std::string head = buf.substr(0, headerEnd);
    std::string rest = buf.substr(headerEnd + 4);

    const size_t sp1 = head.find(' ');
    const size_t sp2 = head.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return r;
    r.method = head.substr(0, sp1);
    std::string uri = head.substr(sp1 + 1, sp2 - sp1 - 1);
    const size_t q = uri.find('?');
    if (q != std::string::npos) {
        r.query = uri.substr(q + 1);
        uri = uri.substr(0, q);
    }
    r.path = percentDecode(uri);

    std::string lowerHead = head;
    std::transform(lowerHead.begin(), lowerHead.end(), lowerHead.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t contentLength = 0;
    const size_t cl = lowerHead.find("content-length:");
    if (cl != std::string::npos) {
        contentLength = static_cast<size_t>(
            std::strtoull(lowerHead.c_str() + cl + 15, nullptr, 10));
    }

    const size_t fn = lowerHead.find("x-file-name:");
    if (fn != std::string::npos) {
        const size_t lineEnd = head.find("\r\n", fn);
        std::string v = head.substr(fn + 12,
            (lineEnd == std::string::npos ? head.size() : lineEnd) - (fn + 12));
        const size_t b = v.find_first_not_of(" \t");
        const size_t e = v.find_last_not_of(" \t");
        v = (b == std::string::npos) ? std::string() : v.substr(b, e - b + 1);
        r.fileName = percentDecode(v);
    }

    if (r.method == "POST" && r.path == "/api/upload") {
        if (contentLength == 0 || contentLength > kMaxUploadBytes) return r;
        r.contentLength = contentLength;
        r.streamBody = true;
        r.body = rest;
        r.ok = true;
        return r;
    }

    if (contentLength > 512 * 1024) return r;
    while (rest.size() < contentLength) {
        const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return r;
        rest.append(tmp, static_cast<size_t>(n));
    }
    r.body = rest.substr(0, contentLength);
    r.ok = true;
    return r;
}

void writeAll(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return;
        off += static_cast<size_t>(n);
    }
}

void respond(int fd, int code, const char* status, const std::string& contentType,
             const std::string& body) {
    char hdr[512];
    std::snprintf(hdr, sizeof(hdr),
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: %s\r\n"
                  "Content-Length: %zu\r\n"
                  "Connection: close\r\n"
                  "Cache-Control: no-store\r\n"
                  "X-Content-Type-Options: nosniff\r\n"
                  "\r\n",
                  code, status, contentType.c_str(), body.size());
    std::string out = hdr;
    out += body;
    writeAll(fd, out);
}

void serveStatic(HttpPanel* p, int fd, const std::string& uriPath) {
    const fs::path root = fs::weakly_canonical(fs::path(p->webDir));
    std::string rel = uriPath;
    if (!rel.empty() && rel.front() == '/') rel.erase(0, 1);
    if (rel.empty()) rel = "index.html";
    if (rel.find("..") != std::string::npos)
        return respond(fd, 403, "Forbidden", "text/plain; charset=utf-8", "forbidden");

    fs::path full = root / rel;
    std::error_code dEc;
    if (fs::is_directory(full, dEc) && !dEc) full /= "index.html";
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(full, ec);
    if (ec)
        return respond(fd, 404, "Not Found", "text/plain; charset=utf-8", "not found");
    const std::string rootS = root.generic_string();
    const std::string canonS = canon.generic_string();
    if (canonS != rootS &&
        (canonS.size() <= rootS.size() ||
         canonS.compare(0, rootS.size(), rootS) != 0 ||
         canonS[rootS.size()] != '/'))
        return respond(fd, 403, "Forbidden", "text/plain; charset=utf-8", "forbidden");

    if (!fs::is_regular_file(canon, ec) || ec) {
        if (uriPath == "/" || uriPath == "/index.html") {
            const std::string tip =
                "<!doctype html><meta charset=utf-8><title>全能外设</title>"
                "<body style=\"font:16px system-ui;padding:40px;line-height:1.8\">"
                "<h2>前端未打包</h2>"
                "<p>此构建未附带 <code>web/</code> 目录。请使用 CI 打包的完整产物，"
                "或在 pc/web/ 执行 <code>npm install &amp;&amp; npm run build</code> "
                "后把 dist/ 内容拷到可执行文件旁的 web/ 目录。</p></body>";
            return respond(fd, 200, "OK", "text/html; charset=utf-8", tip);
        }
        return respond(fd, 404, "Not Found", "text/plain; charset=utf-8", "not found");
    }
    std::ifstream f(canon, std::ios::binary);
    if (!f)
        return respond(fd, 404, "Not Found", "text/plain; charset=utf-8", "not found");
    const std::string body((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    respond(fd, 200, "OK", guessMime(canon.generic_string()), body);
}

void dispatchEnvelope(HttpPanel* p, const std::string& body, bool isAct) {
    p->resetResp();
    std::string err;
    auto j = Json::parse(body, &err);
    std::string idStr;
    if (!j.isNull()) {
        auto* id = j.find("id");
        idStr = id ? id->asString() : "";
    }
    if (j.isNull()) {
        Json r = failResp("请求 JSON 解析失败");
        r["kind"] = std::string(isAct ? "act-response" : "query-response");
        r["id"] = idStr;
        p->resp = r;
        p->hasResp = true;
        return;
    }
    auto* name = j.find("name");
    if (isAct) {
        auto* pl = j.find("payload");
        handleAct(p, idStr, name ? name->asString() : "",
                  pl ? *pl : Json::makeObject());
    } else {
        handleQuery(p, idStr, name ? name->asString() : "");
    }
    if (!p->hasResp) {
        Json r = failResp("请求处理失败（空响应）");
        r["kind"] = std::string(isAct ? "act-response" : "query-response");
        r["id"] = idStr;
        p->resp = r;
        p->hasResp = true;
    }
}

std::string queryValue(const std::string& query, const std::string& key) {
    const std::string pat = key + "=";
    size_t i = 0;
    while ((i = query.find(pat, i)) != std::string::npos) {
        const bool atStart = (i == 0) || query[i - 1] == '&';
        if (atStart) {
            const size_t b = i + pat.size();
            const size_t e = query.find('&', b);
            return percentDecode(query.substr(b, (e == std::string::npos ? query.size() : e) - b));
        }
        i += pat.size();
    }
    return std::string();
}

fs::path dedupePath(const fs::path& dir, const std::string& name) {
    const fs::path first = dir / name;
    std::error_code ec;
    if (!fs::exists(first, ec)) return first;
    const std::string stem = first.stem().string();
    const std::string ext = first.extension().string();
    for (int i = 2; i < 1000; ++i) {
        const fs::path cand = dir / (stem + " (" + std::to_string(i) + ")" + ext);
        if (!fs::exists(cand, ec)) return cand;
    }
    return first;
}

void handleUpload(HttpPanel* p, int fd, const Req& req) {
    const std::string ip = queryValue(req.query, "ip");
    const long portL = std::strtol(queryValue(req.query, "port").c_str(), nullptr, 10);
    std::string rawName = req.fileName.empty() ? queryValue(req.query, "name") : req.fileName;
    if (rawName.empty()) rawName = "upload.bin";

    auto fail = [&](int code, const std::string& msg) {
        Json r = Json::makeObject();
        r["kind"] = std::string("upload-response");
        r["ok"] = false;
        r["error"] = msg;
        respond(fd, code, code == 200 ? "OK" : "Bad Request",
                "application/json; charset=utf-8", r.stringify());
    };

    if (ip.empty() || portL <= 0 || portL > 65535) {
        fail(400, "目标设备不合法（需要 ip 与 port）");
        return;
    }

    timeval tv{60, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    const fs::path dir = fs::path(apxpc::wireless::FileReceiver::defaultDir());
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string safe = apxpc::localsend::LocalSendReceiver::sanitizeName(rawName);
    const fs::path finalPath = dedupePath(dir, safe.empty() ? std::string("upload.bin") : safe);
    const fs::path tmpPath = finalPath.string() + ".part";

    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        fail(500, "无法写入互传目录：" + dir.string());
        return;
    }

    size_t written = 0;
    std::string chunk = req.body;
    if (chunk.size() > req.contentLength) chunk.resize(req.contentLength);
    if (!chunk.empty()) {
        out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        written = chunk.size();
    }
    char buf[64 * 1024];
    while (written < req.contentLength) {
        const size_t want = std::min(sizeof(buf), req.contentLength - written);
        const ssize_t n = ::recv(fd, buf, want, 0);
        if (n <= 0) break;
        out.write(buf, static_cast<std::streamsize>(n));
        written += static_cast<size_t>(n);
    }
    out.flush();
    const bool streamOk = out.good();
    out.close();

    if (written != req.contentLength || !streamOk) {
        std::error_code rmEc;
        fs::remove(tmpPath, rmEc);
        APX_LOGW("上传中断：收到 {} / {} 字节，已丢弃临时文件", written, req.contentLength);
        fail(400, "上传中断（收到 " + std::to_string(written) + " / " +
                  std::to_string(req.contentLength) + " 字节）");
        return;
    }

    fs::rename(tmpPath, finalPath, ec);
    if (ec) {
        std::error_code rmEc;
        fs::remove(tmpPath, rmEc);
        fail(500, "落盘失败：" + ec.message());
        return;
    }
    APX_LOGI("浏览器上传已落盘：{}（{} 字节）→ 发送给 {}:{}",
             finalPath.string().c_str(), written, ip.c_str(), static_cast<int>(portL));

    const bool queued = startLocalSendToPeer(p, ip, static_cast<uint16_t>(portL),
                                             {finalPath.string()});
    Json r = Json::makeObject();
    r["kind"] = std::string("upload-response");
    r["ok"] = true;
    r["queued"] = queued;
    r["name"] = finalPath.filename().string();
    r["path"] = finalPath.string();
    r["size"] = static_cast<double>(written);
    if (!queued) r["warn"] = std::string("上一批文件还在发送中，本次未入队（文件已保存在互传目录）");
    respond(fd, 200, "OK", "application/json; charset=utf-8", r.stringify());
}

void handleConnection(HttpPanel* p, int fd) {
    timeval tv{5, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    const Req req = readRequest(fd);
    if (!req.ok) { ::close(fd); return; }

    if (req.method == "GET" && req.path == "/api/state") {
        const uint64_t now = steadyMs();
        if (now >= p->usbNextMs) {
            refreshUsbStatus(p);
            p->usbNextMs = now + 10000;
        }
        mediaPumpTick(p);
        Json state;
        buildState(p->bundle, state, p);
        Json push = Json::makeObject();
        push["kind"] = std::string("state");
        push["t"] = static_cast<double>(steadyMs());
        push["data"] = state;
        respond(fd, 200, "OK", "application/json; charset=utf-8", push.stringify());
    } else if (req.method == "GET" && req.path == "/api/events") {
        long long since = 0;
        const size_t k = req.query.find("since=");
        if (k != std::string::npos)
            since = std::strtoll(req.query.c_str() + k + 6, nullptr, 10);
        Json arr = Json::makeArray();
        {
            std::lock_guard<std::mutex> lk(g_evMu);
            for (const auto& e : g_events) {
                if (e.seq <= since) continue;
                std::string perr;
                Json ev = Json::parse(e.json, &perr);
                if (!ev.isNull()) arr.push(ev);
            }
        }
        Json box = Json::makeObject();
        box["events"] = arr;
        respond(fd, 200, "OK", "application/json; charset=utf-8", box.stringify());
    } else if (req.method == "POST" && req.path == "/api/act") {
        dispatchEnvelope(p, req.body, true);
        respond(fd, 200, "OK", "application/json; charset=utf-8", p->resp.stringify());
    } else if (req.method == "POST" && req.path == "/api/query") {
        dispatchEnvelope(p, req.body, false);
        respond(fd, 200, "OK", "application/json; charset=utf-8", p->resp.stringify());
    } else if (req.method == "POST" && req.path == "/api/upload" && req.streamBody) {
        handleUpload(p, fd, req);
    } else if (req.method == "POST" && req.path == "/api/open-file") {
        std::string err;
        auto j = Json::parse(req.body, &err);
        std::string idStr;
        if (!j.isNull()) {
            auto* id = j.find("id");
            idStr = id ? id->asString() : "";
        }
        Json r = Json::makeObject();
        r["kind"] = std::string("open-file-response");
        r["id"] = idStr;
        r["ok"] = false;
        respond(fd, 200, "OK", "application/json; charset=utf-8", r.stringify());
    } else if (req.method == "GET") {
        serveStatic(p, fd, req.path);
    } else {
        respond(fd, 405, "Method Not Allowed", "text/plain; charset=utf-8",
                "method not allowed");
    }
    ::close(fd);
}

}  // namespace

int runWebPanelHttp(const HostBundle& bundle, const std::string& webDir) {
    HttpPanel panel;
    panel.bundle = bundle;
    if (!webDir.empty()) {
        panel.webDir = webDir;
    } else {
        const std::string exe = apxpc::autostart::getExePath();
        const size_t slash = exe.find_last_of('/');
        const std::string exeDir =
            exe.empty() ? std::string(".")
                        : (slash == std::string::npos ? std::string(".") : exe.substr(0, slash));
        panel.webDir = exeDir + "/web";
    }
    g_hp = &panel;
    panel.running.store(true);

    if (panel.bundle.cfg && panel.bundle.cfg->autostart) {
        if (!apxpc::autostart::isAutostartEnabled()) {
            APX_LOGI("启动同步: config autostart=true 但自启项缺失，补写");
            apxpc::autostart::setAutostart(true);
        }
    }

    if (panel.bundle.session) {
        panel.bundle.session->startAuto();
        APX_LOGI("无线自动发现已启动（等待手机 APX1PH 信标）");
    }

    {
        char hname[256] = {};
        if (gethostname(hname, sizeof(hname) - 1) == 0 && hname[0] != '\0') {
            hname[sizeof(hname) - 1] = '\0';
            if (!panel.ctrlSrv.start(9511, "", hname))
                APX_LOGW("9511 受控端启动失败（端口可能已被 apxhost serve/其他实例占用）");
            else
                APX_LOGI("9511 受控端已启动（面板自带，主机名 {}）", hname);
        }
    }

    if (panel.bundle.media && panel.bundle.micBridge) {
        apxpc::media::MediaSession* media = panel.bundle.media;
        apxpc::media::MicBridge* mic = panel.bundle.micBridge;
        media->setHandler([mic](uint8_t streamId, uint8_t /*flags*/, uint32_t /*seq*/,
                                const uint8_t* body, size_t len) {
            if (streamId == apxpc::media::kStreamMic && mic->running()) {
                mic->feed(body, len);
            }
        });
    }

    if (panel.bundle.fileReceiver) {
        const bool ok = panel.bundle.fileReceiver->start(
            9512, "",
            [](const std::string& path, uint64_t) {
                if (g_hp && g_hp->bundle.tray) {
                    const size_t slash = path.find_last_of("/");
                    const std::string name =
                        slash == std::string::npos ? path : path.substr(slash + 1);
                    g_hp->bundle.tray->notify("收到文件", name);
                }
                Json d = Json::makeObject();
                d["path"] = path;
                pushEvent("on-file-received", d);
            });
        if (!ok)
            APX_LOGW("文件接收端启动失败：{}",
                     panel.bundle.fileReceiver->lastError().c_str());
    }

    if (panel.bundle.localSend) {
        if (!startLocalSend(panel.bundle))
            APX_LOGW("互传接收端未启动：{}", panel.bundle.localSend->lastError().c_str());
    } else {
        APX_LOGI("互传接收端未接线（bundle.localSend 为空）");
    }

    if (panel.bundle.session) {
        panel.bundle.session->setOnRequestInput([](const std::string& hint) {
            Json d = Json::makeObject();
            d["hint"] = hint;
            pushEvent("on-remote-input-request", d);
        });
    }

    const int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        APX_LOGE("socket 创建失败");
        g_hp = nullptr;
        return -1;
    }
    int reuse = 1;
    ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(lfd, 16) != 0) {
        APX_LOGE("bind/listen 127.0.0.1 失败: errno={}", errno);
        ::close(lfd);
        g_hp = nullptr;
        return -1;
    }
    socklen_t alen = sizeof(addr);
    ::getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &alen);
    panel.port = ntohs(addr.sin_port);

    const std::string url = "http://127.0.0.1:" + std::to_string(panel.port) + "/?host=1";
    std::printf("\n[面板] 浏览器打开: %s\n", url.c_str());
    std::fflush(stdout);
    APX_LOGI("浏览器面板就绪: {}（webDir={}）", url.c_str(), panel.webDir.c_str());

#if defined(__APPLE__)
    (void)std::system(("open '" + url + "'").c_str());
#endif

    ::signal(SIGPIPE, SIG_IGN);
    struct sigaction sa {};
    sa.sa_handler = [](int) {
        if (g_hp) g_hp->running.store(false);
    };
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    while (panel.running.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(lfd, &rfds);
        timeval tv{1, 0};
        const int r = ::select(lfd + 1, &rfds, nullptr, nullptr, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) {
            const uint64_t now = steadyMs();
            if (now >= panel.usbNextMs) {
                refreshUsbStatus(&panel);
                panel.usbNextMs = now + 10000;
            }
            mediaPumpTick(&panel);
            continue;
        }
        const int fd = ::accept(lfd, nullptr, nullptr);
        if (fd < 0) continue;
        handleConnection(&panel, fd);
    }

    ::close(lfd);
    panel.ctrlSrv.stop();
    if (panel.bundle.localSend) panel.bundle.localSend->stop();
    if (panel.sendThread.joinable()) panel.sendThread.join();
    if (panel.mediaThread.joinable()) panel.mediaThread.join();
    g_hp = nullptr;
    APX_LOGI("浏览器面板已退出");
    return 0;
}

}  // namespace apxpc::ui

#else  // _WIN32

namespace apxpc::ui {
int runWebPanelHttp(const HostBundle&, const std::string&) {
    return -1;
}
}  // namespace apxpc::ui

#endif  // !_WIN32
