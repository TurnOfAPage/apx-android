#pragma once
// SHA-256（FIPS 180-4）—— LocalSend 接收端校验用。
//
// 为什么要自己带一份：LocalSend 的 prepare-upload 会给每个文件声明 sha256，
// 接收端据此判断落盘内容是否完整；仓库里此前没有任何哈希实现（只有 crc32），
// 所以就地补一个最小实现，顺便让「传过来的文件和声明的不一致」这件事有据可依。
//
// 只用标准库整数运算，跨 Windows / macOS / Linux 一致；无外部依赖。
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace apxpc::localsend {

class Sha256 {
public:
    Sha256() { reset(); }

    void reset();
    void update(const void* data, size_t n);
    /// 收尾并取十六进制摘要（小写 64 字符）。取完自动 reset，实例可继续复用。
    std::string hex();

private:
    void compress(const uint8_t* block);

    std::array<uint32_t, 8> h_{};
    std::array<uint8_t, 64> buf_{};
    size_t bufLen_ = 0;
    uint64_t total_ = 0;
};

/// 一次性便捷接口
std::string sha256Hex(const void* data, size_t n);

}  // namespace apxpc::localsend
