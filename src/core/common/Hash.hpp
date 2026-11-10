#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "common/Result.hpp"

namespace eden {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t length);
    void update(std::string_view text) { update(text.data(), text.size()); }
    std::array<std::uint8_t, 32> finish();
    std::string finishHex();

private:
    void processBlock(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_;
    std::size_t bufferLength_ = 0;
    std::uint64_t totalLength_ = 0;
};

std::string sha256Hex(std::string_view data);
Result<std::string> sha256FileHex(const std::string& path);

}  // namespace eden
