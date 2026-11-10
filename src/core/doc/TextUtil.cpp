#include "doc/TextUtil.hpp"

#include <algorithm>

#include "common/Strings.hpp"

namespace eden {

namespace {

const char32_t kCp1252High[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                                  0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                  0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

}  // namespace

std::string windows1252ToUtf8(const Bytes& data) {
    std::string out;
    out.reserve(data.size() + data.size() / 8);
    for (std::uint8_t byte : data) {
        if (byte < 0x80) {
            out.push_back(static_cast<char>(byte));
        } else if (byte >= 0x80 && byte < 0xA0) {
            out += encodeUtf8(kCp1252High[byte - 0x80]);
        } else {
            out += encodeUtf8(byte);
        }
    }
    return out;
}

std::string utf16ToUtf8(const Bytes& data, bool littleEndian, std::size_t skip) {
    std::string out;
    out.reserve(data.size());
    auto unitAt = [&](std::size_t index) -> char32_t {
        const std::uint8_t first = data[index];
        const std::uint8_t second = data[index + 1];
        return littleEndian ? static_cast<char32_t>(first | (second << 8))
                            : static_cast<char32_t>((first << 8) | second);
    };
    for (std::size_t i = skip; i + 1 < data.size(); i += 2) {
        char32_t unit = unitAt(i);
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < data.size()) {
            const char32_t low = unitAt(i + 2);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        if (unit >= 0xD800 && unit <= 0xDFFF) unit = 0xFFFD;
        out += encodeUtf8(unit);
    }
    return out;
}

std::string decodeToUtf8(const Bytes& data, std::string* detectedEncoding) {
    auto report = [&](const char* name) {
        if (detectedEncoding != nullptr) *detectedEncoding = name;
    };
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        report("utf-8-bom");
        return std::string(data.begin() + 3, data.end());
    }
    if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        report("utf-16le");
        return utf16ToUtf8(data, true, 2);
    }
    if (data.size() >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        report("utf-16be");
        return utf16ToUtf8(data, false, 2);
    }
    if (data.size() >= 4 && data.size() % 2 == 0) {
        std::size_t zerosEven = 0;
        std::size_t zerosOdd = 0;
        const std::size_t probe = std::min<std::size_t>(data.size(), 512);
        for (std::size_t i = 0; i + 1 < probe; i += 2) {
            if (data[i] == 0) ++zerosEven;
            if (data[i + 1] == 0) ++zerosOdd;
        }
        if (zerosOdd > probe / 4) {
            report("utf-16le");
            return utf16ToUtf8(data, true, 0);
        }
        if (zerosEven > probe / 4) {
            report("utf-16be");
            return utf16ToUtf8(data, false, 0);
        }
    }
    const std::string raw(data.begin(), data.end());
    if (isValidUtf8(raw)) {
        report("utf-8");
        return raw;
    }
    report("windows-1252");
    return windows1252ToUtf8(data);
}

std::string normalizeNewlines(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            out.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else if (c == '\f') {
            out.append("\n\n");
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string stripControlCharacters(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u == '\n' || u == '\t' || u >= 0x20) {
            if (u != 0x7F) out.push_back(c);
        }
    }
    return out;
}

std::string trimLineEnds(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        const bool last = end == std::string_view::npos;
        if (last) end = text.size();
        std::size_t lineEnd = end;
        while (lineEnd > start && (text[lineEnd - 1] == ' ' || text[lineEnd - 1] == '\t')) --lineEnd;
        out.append(text.substr(start, lineEnd - start));
        if (last) break;
        out.push_back('\n');
        start = end + 1;
    }
    return out;
}

std::string collapseBlankLines(std::string_view text, int maxConsecutiveBlank) {
    std::string out;
    out.reserve(text.size());
    int newlines = 0;
    const int allowed = maxConsecutiveBlank + 1;
    for (char c : text) {
        if (c == '\n') {
            ++newlines;
            if (newlines <= allowed) out.push_back(c);
        } else {
            newlines = 0;
            out.push_back(c);
        }
    }
    return out;
}

std::string dehyphenate(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '-' && i + 2 < text.size() && text[i + 1] == '\n' && i > 0 && isAlpha(text[i - 1]) &&
            isLower(text[i + 2])) {
            ++i;
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string reflowHardWrapped(std::string_view text) {
    const std::vector<std::string> lines = splitLines(text);
    std::size_t counted = 0;
    std::size_t wrapped = 0;
    for (const std::string& line : lines) {
        if (line.empty()) continue;
        ++counted;
        if (line.size() >= 45 && line.size() <= 110) ++wrapped;
    }
    if (counted < 6 || wrapped * 10 < counted * 6) return std::string(text);
    std::vector<std::size_t> lengths;
    for (const std::string& line : lines) {
        if (!line.empty()) lengths.push_back(line.size());
    }
    std::sort(lengths.begin(), lengths.end());
    const std::size_t maxLength = lengths[lengths.size() * 9 / 10];
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        out += line;
        if (i + 1 >= lines.size()) break;
        const std::string& next = lines[i + 1];
        const bool joinable = !line.empty() && !next.empty() && line.size() + 12 >= maxLength &&
                              next[0] != ' ' && next[0] != '\t' && next[0] != '-' && next[0] != '*';
        if (joinable) {
            out.push_back(' ');
        } else {
            out.push_back('\n');
        }
    }
    return out;
}

std::string cleanExtractedText(std::string_view text) {
    std::string normalized = normalizeNewlines(text);
    normalized = stripControlCharacters(normalized);
    normalized = trimLineEnds(normalized);
    normalized = dehyphenate(normalized);
    normalized = collapseBlankLines(normalized, 1);
    return trim(normalized);
}

void paginateByLength(const std::string& text, std::int64_t rangeStart, std::int64_t rangeEnd,
                      std::size_t targetChars, std::int64_t firstIndex, std::vector<PageSpan>& pages) {
    if (targetChars < 200) targetChars = 200;
    std::int64_t position = rangeStart;
    std::int64_t index = firstIndex;
    while (position < rangeEnd) {
        std::int64_t limit = std::min<std::int64_t>(rangeEnd, position + static_cast<std::int64_t>(targetChars));
        if (limit < rangeEnd) {
            const std::int64_t floor = position + static_cast<std::int64_t>(targetChars / 2);
            std::int64_t cut = -1;
            for (std::int64_t i = limit; i > floor; --i) {
                if (text[static_cast<std::size_t>(i - 1)] == '\n' && text[static_cast<std::size_t>(i - 2)] == '\n') {
                    cut = i;
                    break;
                }
            }
            if (cut < 0) {
                for (std::int64_t i = limit; i > floor; --i) {
                    if (text[static_cast<std::size_t>(i - 1)] == '\n') {
                        cut = i;
                        break;
                    }
                }
            }
            if (cut < 0) {
                for (std::int64_t i = limit; i > floor; --i) {
                    if (text[static_cast<std::size_t>(i - 1)] == ' ') {
                        cut = i;
                        break;
                    }
                }
            }
            if (cut > 0) limit = cut;
            while (limit < rangeEnd && (static_cast<unsigned char>(text[static_cast<std::size_t>(limit)]) & 0xC0) == 0x80) {
                ++limit;
            }
        }
        pages.push_back({index++, position, limit});
        position = limit;
    }
}

std::string fileName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string fileStem(const std::string& path) {
    const std::string name = fileName(path);
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

}  // namespace eden
