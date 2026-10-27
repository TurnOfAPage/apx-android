// ncm_transport.cpp —— NCM 传输实现（TCP over USB-ECM）
// Issue #14 第二阶段（PC 端）

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
using apx_socket_t = SOCKET;
constexpr apx_socket_t APX_INVALID_SOCKET = INVALID_SOCKET;
using apx_socklen_t = int;
using apx_dword_t = DWORD;
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
using apx_socket_t = int;
constexpr apx_socket_t APX_INVALID_SOCKET = -1;
using apx_socklen_t = socklen_t;
using apx_dword_t = uint32_t;
#endif

#include "transport/i_transport.hpp"
#include "common/log.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace apxdisp {

namespace {

inline void apx_close_socket(apx_socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

constexpr size_t kFrameHeaderSize = 8;
constexpr uint8_t kStreamVideo   = 0;
constexpr uint8_t kStreamTouch   = 2;
constexpr uint8_t kStreamControl = 3;
constexpr size_t  kMaxFrameSize = 1 << 20;

inline void encodeLength(uint8_t* out, uint32_t len) {
    out[0] = (uint8_t)(len >> 16);
    out[1] = (uint8_t)(len >> 8);
    out[2] = (uint8_t)(len);
}
inline uint32_t decodeLength(const uint8_t* in) {
    return ((uint32_t)in[0] << 16) | ((uint32_t)in[1] << 8) | (uint32_t)in[2];
}

inline void buildHeader(uint8_t* hdr, uint8_t streamId, uint32_t payloadLen) {
    hdr[0] = 'A'; hdr[1] = 'P'; hdr[2] = 'X'; hdr[3] = 'N';
    hdr[4] = streamId;
    encodeLength(hdr + 5, payloadLen);
}

class NcmChannel : public IChannel {
public:
    NcmChannel(apx_socket_t sock, uint8_t sid, bool reliable)
        : sock_(sock), sid_(sid), reliable_(reliable) {}

    size_t write(const uint8_t* data, size_t len, uint32_t timeoutMs) override {
        if (sock_ == APX_INVALID_SOCKET) return 0;
        apx_dword_t t = timeoutMs;
        setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof(t));
        uint8_t hdr[kFrameHeaderSize];
        buildHeader(hdr, sid_, (uint32_t)len);
        size_t sent = 0;
        while (sent < kFrameHeaderSize) {
            int n = send(sock_, (const char*)(hdr + sent), (int)(kFrameHeaderSize - sent), 0);
            if (n <= 0) return 0;
            sent += n;
        }
        if (len == 0) return 0;
        sent = 0;
        while (sent < len) {
            int n = send(sock_, (const char*)(data + sent), (int)(len - sent), 0);
            if (n <= 0) break;
            sent += n;
        }
        return sent;
    }

    size_t read(uint8_t* buf, size_t cap, uint32_t timeoutMs) override {
        if (sock_ == APX_INVALID_SOCKET) return 0;
        apx_dword_t t = timeoutMs;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
        uint8_t hdr[kFrameHeaderSize];
        size_t got = 0;
        while (got < kFrameHeaderSize) {
            int n = recv(sock_, (char*)(hdr + got), (int)(kFrameHeaderSize - got), 0);
            if (n <= 0) return 0;
            got += n;
        }
        if (hdr[0]!='A'||hdr[1]!='P'||hdr[2]!='X'||hdr[3]!='N') return 0;
        uint32_t payloadLen = decodeLength(hdr + 5);
        if (payloadLen == 0 || payloadLen > kMaxFrameSize || payloadLen > cap) return 0;
        got = 0;
        while (got < payloadLen) {
            int n = recv(sock_, (char*)buf + got, (int)(payloadLen - got), 0);
            if (n <= 0) break;
            got += n;
        }
        return got;
    }

    bool reliable() const override { return reliable_; }
    void cancel() override { apx_close_socket(sock_); sock_ = APX_INVALID_SOCKET; }
    std::string lastError() const override { return err_; }

private:
    apx_socket_t sock_;
    uint8_t sid_;
    bool reliable_;
    std::string err_;
};

}  // namespace

struct NcmTransport::Impl {
    apx_socket_t    sock = APX_INVALID_SOCKET;
    bool            open = false;
    bool            isServer = false;
    uint16_t        port = 9512;
    std::string     localIp;
    std::string     peerIp;
    std::string     lastError;
    std::string     adapterName;
    bool            linkUp = false;
    std::unique_ptr<NcmChannel> video, control, touch;
};

NcmTransport::NcmTransport() : impl_(std::make_unique<Impl>()) {}
NcmTransport::~NcmTransport() { close(); }

std::vector<std::string> NcmTransport::listAdapters() {
    std::vector<std::string> result;
#ifdef _WIN32
    ULONG bufLen = 16 * 1024;
    std::vector<uint8_t> buf(bufLen);
    auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST,
                             nullptr, addrs, &bufLen) == NO_ERROR) {
        for (auto* a = addrs; a; a = a->Next) {
            std::string desc;
            if (a->Description) { char d[256]={}; wcstombs(d,a->Description,255); desc=d; }
            if ((desc.find("CDC")!=std::string::npos || desc.find("ECM")!=std::string::npos ||
                 desc.find("NCM")!=std::string::npos || desc.find("RNDIS")!=std::string::npos ||
                 desc.find("USB Ethernet")!=std::string::npos) &&
                a->OperStatus == IfOperStatusUp) {
                char name[128]={};
                wcstombs(name, a->FriendlyName ? a->FriendlyName : L"", 127);
                result.push_back(name);
            }
        }
    }
#else
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr)==0) {
        for (auto* i=ifaddr; i; i=i->ifa_next) {
            if (!i->ifa_name) continue;
            std::string n(i->ifa_name);
            if (n.find("usb")!=std::string::npos || n.find("cdc")!=std::string::npos ||
                n.find("rndis")!=std::string::npos || n.find("enx")!=std::string::npos) {
                if (std::find(result.begin(),result.end(),n)==result.end())
                    result.push_back(n);
            }
        }
        freeifaddrs(ifaddr);
    }
#endif
    return result;
}

bool NcmTransport::open(const TransportSpec& spec) {
    if (spec.kind != TransportKind::Ncm) { impl_->lastError="kind must be Ncm"; return false; }
    const auto& cfg = spec.netConfig;
    impl_->port = cfg.port; impl_->isServer = cfg.isServer;
    impl_->localIp = cfg.localIp; impl_->peerIp = cfg.peerIp;

    auto adapters = listAdapters();
    if (adapters.empty()) { impl_->lastError="No NCM adapter found"; return false; }
    impl_->adapterName = adapters[0];
    APX_LOG_I("NCM adapter: %s", impl_->adapterName.c_str());

    impl_->linkUp = isLinkUp();
    if (!impl_->linkUp) { impl_->lastError="NCM link down"; return false; }

#ifdef _WIN32
    static bool wsa=false;
    if (!wsa) { WSADATA w; if (WSAStartup(MAKEWORD(2,2),&w)) { impl_->lastError="WSAStartup fail"; return false; } wsa=true; }
#endif

    if (impl_->isServer) {
        apx_socket_t ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ls==APX_INVALID_SOCKET) { impl_->lastError="socket fail"; return false; }
        int opt=1; setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,(const char*)&opt,sizeof(opt));
        struct sockaddr_in addr={}; addr.sin_family=AF_INET; addr.sin_port=htons(impl_->port);
        if (impl_->localIp.empty()) addr.sin_addr.s_addr=INADDR_ANY;
        else inet_pton(AF_INET,impl_->localIp.c_str(),&addr.sin_addr);
        if (bind(ls,(struct sockaddr*)&addr,sizeof(addr))||listen(ls,1)) {
            impl_->lastError="bind/listen fail";
            apx_close_socket(ls);
            return false;
        }
        APX_LOG_I("NCM listen :%u", (unsigned)impl_->port);
        struct timeval tv={5,0}; setsockopt(ls,SOL_SOCKET,SO_RCVTIMEO,(const char*)&tv,sizeof(tv));
        struct sockaddr_in ca={}; apx_socklen_t cl=sizeof(ca);
        apx_socket_t c=accept(ls,(struct sockaddr*)&ca,&cl);
        apx_close_socket(ls);
        if (c==APX_INVALID_SOCKET) { impl_->lastError="accept timeout"; return false; }
        char ip[INET_ADDRSTRLEN]={}; inet_ntop(AF_INET,&ca.sin_addr,ip,sizeof(ip));
        impl_->peerIp=ip; impl_->sock=c;
        APX_LOG_I("NCM client: %s", ip);
    } else {
        if (impl_->peerIp.empty()) { impl_->lastError="peerIp required"; return false; }
        apx_socket_t s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if (s==APX_INVALID_SOCKET) { impl_->lastError="socket fail"; return false; }
        struct sockaddr_in a={}; a.sin_family=AF_INET; a.sin_port=htons(impl_->port);
        inet_pton(AF_INET,impl_->peerIp.c_str(),&a.sin_addr);
        struct timeval tv={5,0}; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&tv,sizeof(tv));
        if (connect(s,(struct sockaddr*)&a,sizeof(a))) {
            impl_->lastError="connect fail";
            apx_close_socket(s);
            return false;
        }
        impl_->sock=s;
        APX_LOG_I("NCM connect %s:%u", impl_->peerIp.c_str(), (unsigned)impl_->port);
    }

    impl_->video=std::make_unique<NcmChannel>(impl_->sock,kStreamVideo,false);
    impl_->control=std::make_unique<NcmChannel>(impl_->sock,kStreamControl,true);
    impl_->touch=std::make_unique<NcmChannel>(impl_->sock,kStreamTouch,true);
    impl_->open=true;
    return true;
}

void NcmTransport::close() {
    if (impl_->sock!=APX_INVALID_SOCKET) {
        impl_->video.reset(); impl_->control.reset(); impl_->touch.reset();
        apx_close_socket(impl_->sock);
        impl_->sock=APX_INVALID_SOCKET;
    }
    impl_->open=false; impl_->linkUp=false;
}

bool NcmTransport::isOpen() const { return impl_->open; }
IChannel* NcmTransport::videoChannel() { return impl_->video.get(); }
IChannel* NcmTransport::controlChannel() { return impl_->control.get(); }
IChannel* NcmTransport::touchChannel() { return impl_->touch.get(); }

bool NcmTransport::reset() {
    close();
    TransportSpec s; s.kind=TransportKind::Ncm;
    s.netConfig.port=impl_->port; s.netConfig.isServer=impl_->isServer;
    s.netConfig.localIp=impl_->localIp; s.netConfig.peerIp=impl_->peerIp;
    return open(s);
}
std::string NcmTransport::lastError() const { return impl_->lastError; }
std::string NcmTransport::localIp() const { return impl_->localIp; }
std::string NcmTransport::peerIp() const { return impl_->peerIp; }

bool NcmTransport::isLinkUp() const {
#ifdef _WIN32
    ULONG bufLen=16*1024; std::vector<uint8_t> buf(bufLen);
    auto* addrs=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    if (GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST,nullptr,addrs,&bufLen)==NO_ERROR) {
        for (auto* a=addrs;a;a=a->Next) {
            char n[128]={}; wcstombs(n,a->FriendlyName?a->FriendlyName:L"",127);
            if (impl_->adapterName==n) return a->OperStatus==IfOperStatusUp;
        }
    }
    return false;
#else
    int fd=socket(AF_INET,SOCK_DGRAM,0); if(fd<0) return false;
    struct ifreq ifr={}; strncpy(ifr.ifr_name,impl_->adapterName.c_str(),IFNAMSIZ-1);
    bool up=false;
    if(ioctl(fd,SIOCGIFFLAGS,&ifr)==0) up=(ifr.ifr_flags&IFF_RUNNING)!=0;
    // 必须写 ::close —— 类内成员 close() 会遮蔽全局的，Linux 上这一行一直编不过
    ::close(fd); return up;
#endif
}

std::unique_ptr<ITransport> createNcmTransport() { return std::make_unique<NcmTransport>(); }

}  // namespace apxdisp