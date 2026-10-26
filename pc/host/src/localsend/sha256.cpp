#include "apxpc/localsend/sha256.hpp"

#include <algorithm>
#include <cstdio>

namespace apxpc::localsend {
namespace {

// FIPS 180-4 §4.2.2 的 64 个轮常量
constexpr uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

}  // namespace

void Sha256::reset() {
    h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    buf_.fill(0);
    bufLen_ = 0;
    total_ = 0;
}

void Sha256::compress(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
               (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(p[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = hh + s1 + ch + kK[i] + w[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
}

void Sha256::update(const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    total_ += n;
    while (n > 0) {
        const size_t take = std::min<size_t>(64 - bufLen_, n);
        std::copy(p, p + take, buf_.begin() + static_cast<std::ptrdiff_t>(bufLen_));
        bufLen_ += take;
        p += take;
        n -= take;
        if (bufLen_ == 64) {
            compress(buf_.data());
            bufLen_ = 0;
        }
    }
}

std::string Sha256::hex() {
    // 收尾：0x80 + 若干 0 + 64 位大端比特长度。
    // 注意 **不能** 走 update()——那会把填充字节算进 total_，长度就错了。
    const uint64_t bits = total_ * 8;
    uint8_t pad[72];
    size_t padLen = 0;
    pad[padLen++] = 0x80u;
    while ((bufLen_ + padLen) % 64 != 56) pad[padLen++] = 0x00u;
    for (int i = 0; i < 8; ++i)
        pad[padLen++] = static_cast<uint8_t>((bits >> (56 - 8 * i)) & 0xffu);

    const uint8_t* p = pad;
    size_t remaining = padLen;
    while (remaining > 0) {
        const size_t take = std::min<size_t>(64 - bufLen_, remaining);
        std::copy(p, p + take, buf_.begin() + static_cast<std::ptrdiff_t>(bufLen_));
        bufLen_ += take;
        p += take;
        remaining -= take;
        if (bufLen_ == 64) {
            compress(buf_.data());
            bufLen_ = 0;
        }
    }

    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint32_t v : h_) {
        for (int i = 3; i >= 0; --i) {
            // 一个字节要两位十六进制。首版这里只取低半字节、每字节只吐一个字符，
            // 于是摘要恒为 32 字符且数值全错 —— 而算法本身是对的（CI 上六个向量全红，
            // 打印出来的“实际值”恰好等于正确摘要的每字节低半字节）。
            const uint32_t byte = (v >> (8 * i)) & 0xffu;
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0xfu]);
        }
    }
    reset();
    return out;
}

std::string sha256Hex(const void* data, size_t n) {
    Sha256 s;
    s.update(data, n);
    return s.hex();
}

}  // namespace apxpc::localsend
