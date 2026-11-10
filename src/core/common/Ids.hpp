#pragma once

#include <cstddef>
#include <string>

#include "common/Types.hpp"

namespace eden {

Id newId();
std::string randomHex(std::size_t byteCount);
bool looksLikeId(const std::string& text);

}  // namespace eden
