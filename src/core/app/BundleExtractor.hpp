#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "common/Result.hpp"
#include "common/Types.hpp"

namespace eden {

// Unpacks a zip archive held in memory into `destination` (created if needed). Used on Android to unpack the
// bundled Python runtime and the AI sidecar from the app package on first start. Entry names that would escape
// the destination (absolute paths, "..") are rejected.
using ExtractProgress = std::function<void(std::size_t done, std::size_t total)>;

Status extractZipBundle(const Bytes& archive, const std::string& destination, const ExtractProgress& progress = {});

}  // namespace eden
