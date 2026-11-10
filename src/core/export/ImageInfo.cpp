#include "export/ImageInfo.hpp"

#include "common/Strings.hpp"
#include "doc/Inflate.hpp"
#include "doc/PdfDocument.hpp"
#include "doc/Zip.hpp"

namespace eden {

namespace {

std::uint32_t readBe32(const Bytes& data, std::size_t offset) {
    return (static_cast<std::uint32_t>(data[offset]) << 24) | (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8) | data[offset + 3];
}

}  // namespace

std::string xmlSafe(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u == '\t' || u == '\n' || u >= 0x20) out.push_back(c);
    }
    return escapeHtml(sanitizeUtf8(out));
}

Result<ImageData> describeImage(Bytes bytes) {
    ImageData image;
    if (bytes.size() > 24 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
        image.extension = "png";
        image.width = static_cast<int>(readBe32(bytes, 16));
        image.height = static_cast<int>(readBe32(bytes, 20));
        const int colorType = bytes[25];
        image.components = colorType == 0 || colorType == 4 ? 1 : 3;
    } else if (bytes.size() > 4 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        image.extension = "jpeg";
        std::size_t i = 2;
        bool found = false;
        while (i + 9 < bytes.size()) {
            if (bytes[i] != 0xFF) {
                ++i;
                continue;
            }
            const std::uint8_t marker = bytes[i + 1];
            if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0xFF) {
                i += marker == 0xFF ? 1 : 2;
                continue;
            }
            const std::size_t length = (static_cast<std::size_t>(bytes[i + 2]) << 8) | bytes[i + 3];
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
                image.height = (bytes[i + 5] << 8) | bytes[i + 6];
                image.width = (bytes[i + 7] << 8) | bytes[i + 8];
                image.components = bytes[i + 9];
                found = true;
                break;
            }
            i += 2 + length;
        }
        if (!found) return makeError(ErrorCode::Parse, "jpeg dimensions not found");
    } else {
        return makeError(ErrorCode::Unsupported, "only PNG and JPEG images can be embedded");
    }
    if (image.width <= 0 || image.height <= 0 || image.width > 20000 || image.height > 20000) {
        return makeError(ErrorCode::Parse, "image has invalid dimensions");
    }
    image.bytes = std::move(bytes);
    return image;
}

Result<ImageData> loadImage(const std::string& path) {
    auto bytes = readFileBytes(path);
    if (!bytes.ok()) return bytes.error();
    return describeImage(std::move(bytes.value()));
}

Result<RawImage> decodePngRaw(const Bytes& bytes) {
    if (bytes.size() < 33 || bytes[0] != 0x89 || bytes[1] != 'P') return makeError(ErrorCode::Parse, "not a png");
    int width = 0;
    int height = 0;
    int depth = 0;
    int colorType = 0;
    int interlace = 0;
    Bytes compressed;
    Bytes palette;
    std::size_t position = 8;
    while (position + 12 <= bytes.size()) {
        const std::uint32_t length = readBe32(bytes, position);
        const std::string type(bytes.begin() + static_cast<std::ptrdiff_t>(position + 4), bytes.begin() + static_cast<std::ptrdiff_t>(position + 8));
        const std::size_t body = position + 8;
        if (body + length > bytes.size()) return makeError(ErrorCode::Parse, "truncated png chunk");
        if (type == "IHDR" && length >= 13) {
            width = static_cast<int>(readBe32(bytes, body));
            height = static_cast<int>(readBe32(bytes, body + 4));
            depth = bytes[body + 8];
            colorType = bytes[body + 9];
            interlace = bytes[body + 12];
        } else if (type == "PLTE") {
            palette.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body), bytes.begin() + static_cast<std::ptrdiff_t>(body + length));
        } else if (type == "IDAT") {
            compressed.insert(compressed.end(), bytes.begin() + static_cast<std::ptrdiff_t>(body), bytes.begin() + static_cast<std::ptrdiff_t>(body + length));
        } else if (type == "IEND") {
            break;
        }
        position = body + length + 4;
    }
    if (width <= 0 || height <= 0) return makeError(ErrorCode::Parse, "png header missing");
    if (depth != 8 || interlace != 0) return makeError(ErrorCode::Unsupported, "only 8-bit non-interlaced png images are supported");
    int channels = 0;
    switch (colorType) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: return makeError(ErrorCode::Unsupported, "unsupported png color type");
    }
    auto inflated = inflateZlib(compressed.data(), compressed.size());
    if (!inflated.ok()) return inflated.error();
    const Bytes rows = applyPredictor(inflated.value(), 15, width, channels, 8);
    const std::size_t rowBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    if (rows.size() < rowBytes * static_cast<std::size_t>(height)) return makeError(ErrorCode::Parse, "png pixel data is truncated");

    RawImage raw;
    raw.width = width;
    raw.height = height;
    raw.components = (colorType == 0 || colorType == 4) ? 1 : 3;
    raw.pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(raw.components));
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = rows.data() + static_cast<std::size_t>(y) * rowBytes;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* px = row + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
            auto over = [](int value, int alpha) { return static_cast<std::uint8_t>((value * alpha + 255 * (255 - alpha)) / 255); };
            switch (colorType) {
                case 0: raw.pixels.push_back(px[0]); break;
                case 2: raw.pixels.insert(raw.pixels.end(), {px[0], px[1], px[2]}); break;
                case 3: {
                    const std::size_t index = static_cast<std::size_t>(px[0]) * 3;
                    if (index + 2 >= palette.size()) return makeError(ErrorCode::Parse, "png palette index out of range");
                    raw.pixels.insert(raw.pixels.end(), {palette[index], palette[index + 1], palette[index + 2]});
                    break;
                }
                case 4: raw.pixels.push_back(over(px[0], px[1])); break;
                default: raw.pixels.insert(raw.pixels.end(), {over(px[0], px[3]), over(px[1], px[3]), over(px[2], px[3])}); break;
            }
        }
    }
    return raw;
}

}  // namespace eden
