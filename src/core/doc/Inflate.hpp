#pragma once

#include <cstddef>
#include <cstdint>

#include "common/Result.hpp"
#include "common/Types.hpp"

namespace eden {

constexpr std::size_t kDefaultInflateLimit = 512u * 1024u * 1024u;

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t seed = 0);
std::uint32_t adler32(const std::uint8_t* data, std::size_t size);

Result<Bytes> inflateRaw(const std::uint8_t* data, std::size_t size,
                         std::size_t maxOutput = kDefaultInflateLimit);
Result<Bytes> inflateZlib(const std::uint8_t* data, std::size_t size,
                          std::size_t maxOutput = kDefaultInflateLimit);

Bytes deflateStored(const std::uint8_t* data, std::size_t size);
Bytes deflateCompress(const std::uint8_t* data, std::size_t size);
Bytes zlibWrapCompressed(const std::uint8_t* data, std::size_t size);
Bytes zlibWrapStored(const std::uint8_t* data, std::size_t size);

}  // namespace eden
