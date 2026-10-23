// update_check.cpp —— PC 端更新检测（与 Android UpdateChecker.kt 同一套比对规则）
// 零依赖：WinHTTP 是 Windows 系统组件（webview2_runtime.cpp / apxsetup 已有先例）。
// 国内可用性：直连 api.github.com 失败（超时/被拦）自动回退 gh-proxy.com 镜像（实测可通）。
#include "apxpc/app/update_check.hpp"
#include "apxpc/version.hpp"

#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp")
#endif

namespace apxpc::app {
namespace {

std::string stripPrefixV(std::string s) {
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    return s;
}

// 按非数字切出数字段（与 UpdateChecker.kt 的 split(非数字) 语义一致）
std::vector<long long> versionSegments(const std::string& s) {
    std::vector<long long> out;
    const std::string t = stripPrefixV(s);
    size_t i = 0;
    while (i < t.size()) {
        if (t[i] >= '0' && t[i] <= '9') {
            long long v = 0;
            while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
                v = v * 10 + (t[i] - '0');
                ++i;
            }
            out.push_back(v);
        } else {
            ++i;
        }
    }
    return out;
}

#ifdef _WIN32
// 最小化提取 "tag_name": "..."（releases/latest 响应结构固定，不引 JSON 库）。
bool extractTagName(const std::string& body, std::string& out) {
    const size_t k = body.find("\"tag_name\"");
    if (k == std::string::npos) return false;
    const size_t colon = body.find(':', k);
    if (colon == std::string::npos) return false;
    const size_t q1 = body.find('"', colon);
    if (q1 == std::string::npos) return false;
    const size_t q2 = body.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;
    out = body.substr(q1 + 1, q2 - q1 - 1);
    return !out.empty();
}

std::wstring toWide(const std::string& s) {
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.resize(static_cast<size_t>(n) - 1);
    return w;
}

// WinHTTP GET。返回 HTTP 状态码；0 = 连接层失败（err 写原因）。
DWORD httpsGet(const std::string& host, const std::string& path, std::string& body,
               std::string& err) {
    HINTERNET ses = ::WinHttpOpen(L"apxhost-check-update/1.0",
                                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { err = "WinHttpOpen 失败"; return 0; }
    ::WinHttpSetTimeouts(ses, 5000, 5000, 5000, 8000);
    const std::wstring whost = toWide(host);
    HINTERNET con = whost.empty() ? nullptr
        : ::WinHttpConnect(ses, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { err = "DNS/连接失败"; ::WinHttpCloseHandle(ses); return 0; }
    const std::wstring wpath = toWide(path);
    HINTERNET req = wpath.empty() ? nullptr
        : ::WinHttpOpenRequest(con, L"GET", wpath.c_str(), nullptr,
                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                               WINHTTP_FLAG_SECURE);
    if (!req) { err = "创建请求失败"; ::WinHttpCloseHandle(con); ::WinHttpCloseHandle(ses); return 0; }
    DWORD status = 0;
    BOOL ok = ::WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = ::WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        err = "HTTP 请求失败（网络不通或被拦截）";
        ::WinHttpCloseHandle(req); ::WinHttpCloseHandle(con); ::WinHttpCloseHandle(ses);
        return 0;
    }
    DWORD len = sizeof(status);
    ::WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                          WINHTTP_NO_HEADER_INDEX);
    DWORD avail = 0;
    while (::WinHttpQueryDataAvailable(req, &avail) && avail > 0 && body.size() < 1048576) {
        char buf[4096];
        DWORD got = 0;
        if (!::WinHttpReadData(req, buf, sizeof(buf), &got) || got == 0) break;
        body.append(buf, got);
    }
    ::WinHttpCloseHandle(req); ::WinHttpCloseHandle(con); ::WinHttpCloseHandle(ses);
    return status;
}
#endif  // _WIN32

}  // namespace

int compareVersion(const std::string& a, const std::string& b) {
    const auto sa = versionSegments(a);
    const auto sb = versionSegments(b);
    const size_t n = sa.size() > sb.size() ? sa.size() : sb.size();
    for (size_t i = 0; i < n; ++i) {
        const long long va = i < sa.size() ? sa[i] : 0;
        const long long vb = i < sb.size() ? sb[i] : 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
    return 0;
}

UpdateCheckResult checkForUpdate() {
    UpdateCheckResult r;
#ifdef _WIN32
    const char* kApiPath = "/repos/guocheng1378/apx-android/releases/latest";
    std::string body, err;
    DWORD status = httpsGet("api.github.com", kApiPath, body, err);
    // 直连不通（GFW 常见超时/重置，或 403/429 等非预期状态）→ gh-proxy 国内镜像复查一次。
    // 404 视为真实「无已发布 Release」，不再走镜像（避免把正常 404 当网络问题重试）。
    if (status != 200 && status != 404) {
        std::string body2, err2;
        const std::string path2 = std::string("/https://api.github.com") + kApiPath;
        const DWORD status2 = httpsGet("gh-proxy.com", path2, body2, err2);
        if (status2 == 200 || status2 == 404) {
            status = status2;
            body = std::move(body2);
        } else {
            r.error = "直连 GitHub 失败（" + err + "），镜像也失败（" + err2 + "）";
            return r;
        }
    }
    if (status == 404) {
        r.error = "GitHub 无已发布 Release（draft/prerelease 不参与检查更新）";
        return r;
    }
    if (status != 200) {
        r.error = "GitHub API 返回状态 " + std::to_string(status);
        return r;
    }
    std::string tag;
    if (!extractTagName(body, tag)) { r.error = "响应中未找到 tag_name"; return r; }
    r.ok = true;
    r.latestTag = tag;
    r.hasNewer = compareVersion(tag, APXPC_VERSION_STRING) > 0;
    return r;
#else
    r.error = "当前平台暂未实现更新检测（仅 Windows）";
    return r;
#endif
}

}  // namespace apxpc::app
