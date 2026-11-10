#include "common/Ids.hpp"

#include <array>
#include <cstdint>
#include <random>

namespace eden {

namespace {

std::mt19937_64& generator() {
    thread_local std::mt19937_64 engine([] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device()};
        return std::mt19937_64(seed);
    }());
    return engine;
}

const char* kHex = "0123456789abcdef";

}  // namespace

std::string randomHex(std::size_t byteCount) {
    std::string out;
    out.reserve(byteCount * 2);
    auto& engine = generator();
    std::size_t produced = 0;
    while (produced < byteCount) {
        std::uint64_t word = engine();
        for (int i = 0; i < 8 && produced < byteCount; ++i, ++produced) {
            const unsigned byte = static_cast<unsigned>((word >> (i * 8)) & 0xFFu);
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0xF]);
        }
    }
    return out;
}

Id newId() {
    std::array<std::uint8_t, 16> bytes{};
    auto& engine = generator();
    std::uint64_t high = engine();
    std::uint64_t low = engine();
    for (int i = 0; i < 8; ++i) {
        bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((high >> (i * 8)) & 0xFFu);
        bytes[static_cast<std::size_t>(i + 8)] = static_cast<std::uint8_t>((low >> (i * 8)) & 0xFFu);
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0Fu) | 0x40u);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3Fu) | 0x80u);
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0xF]);
    }
    return out;
}

bool looksLikeId(const std::string& text) {
    if (text.size() != 36) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

}  // namespace eden
