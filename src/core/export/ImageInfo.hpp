#pragma once

#include <string>

#include "common/Result.hpp"
#include "common/Types.hpp"

namespace eden {

struct ImageData {
    Bytes bytes;
    std::string extension;
    int width = 0;
    int height = 0;
    int components = 3;
};

struct RawImage {
    int width = 0;
    int height = 0;
    int components = 3;
    Bytes pixels;
};

Result<ImageData> loadImage(const std::string& path);
Result<ImageData> describeImage(Bytes bytes);
Result<RawImage> decodePngRaw(const Bytes& bytes);
std::string xmlSafe(const std::string& text);

}  // namespace eden
