#include "common/Strings.hpp"

#include <cctype>
#include <cstdio>

namespace eden {

namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

}  // namespace

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && isSpace(text[begin])) ++begin;
    while (end > begin && isSpace(text[end - 1])) --end;
    return std::string(text.substr(begin, end - begin));
}

std::string toLowerAscii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string toUpperAscii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && toLowerAscii(a) == toLowerAscii(b);
}

bool containsIgnoreCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    return toLowerAscii(haystack).find(toLowerAscii(needle)) != std::string::npos;
}

std::string replaceAll(std::string text, std::string_view from, std::string_view to) {
    if (from.empty()) return text;
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

std::vector<std::string> split(std::string_view text, char separator, bool keepEmpty) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t next = text.find(separator, start);
        if (next == std::string_view::npos) next = text.size();
        std::string piece(text.substr(start, next - start));
        if (keepEmpty || !piece.empty()) parts.push_back(std::move(piece));
        start = next + 1;
    }
    return parts;
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t next = text.find('\n', start);
        std::size_t end = next == std::string_view::npos ? text.size() : next;
        std::size_t lineEnd = end;
        if (lineEnd > start && text[lineEnd - 1] == '\r') --lineEnd;
        lines.emplace_back(text.substr(start, lineEnd - start));
        if (next == std::string_view::npos) break;
        start = next + 1;
    }
    return lines;
}

std::string join(const std::vector<std::string>& parts, std::string_view separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out.append(separator);
        out.append(parts[i]);
    }
    return out;
}

std::string collapseWhitespace(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (char c : text) {
        if (isSpace(c)) {
            pendingSpace = !out.empty();
        } else {
            if (pendingSpace) out.push_back(' ');
            pendingSpace = false;
            out.push_back(c);
        }
    }
    return out;
}

std::size_t wordCount(std::string_view text) {
    std::size_t count = 0;
    bool inWord = false;
    for (char c : text) {
        if (isSpace(c)) {
            inWord = false;
        } else if (!inWord) {
            inWord = true;
            ++count;
        }
    }
    return count;
}

std::string slugify(std::string_view text) {
    std::string out;
    bool lastDash = true;
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) && u < 128) {
            out.push_back(static_cast<char>(std::tolower(u)));
            lastDash = false;
        } else if (!lastDash) {
            out.push_back('-');
            lastDash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

std::string escapeHtml(std::string_view text) {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string escapeLikePattern(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '%' || c == '_' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string formatBytes(std::uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    if (unit == 0) {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
    }
    return buffer;
}

bool decodeUtf8(std::string_view text, std::size_t& position, char32_t& codepoint) {
    if (position >= text.size()) return false;
    const unsigned char lead = static_cast<unsigned char>(text[position]);
    std::size_t length = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if (lead < 0x80) {
        codepoint = lead;
        ++position;
        return true;
    } else if ((lead & 0xE0) == 0xC0) {
        length = 2;
        value = lead & 0x1Fu;
        minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        value = lead & 0x0Fu;
        minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        value = lead & 0x07u;
        minimum = 0x10000;
    } else {
        return false;
    }
    if (position + length > text.size()) return false;
    for (std::size_t i = 1; i < length; ++i) {
        const unsigned char next = static_cast<unsigned char>(text[position + i]);
        if ((next & 0xC0) != 0x80) return false;
        value = (value << 6) | (next & 0x3Fu);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    codepoint = value;
    position += length;
    return true;
}

std::string encodeUtf8(char32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

bool isValidUtf8(std::string_view text) {
    std::size_t position = 0;
    char32_t cp = 0;
    while (position < text.size()) {
        if (!decodeUtf8(text, position, cp)) return false;
    }
    return true;
}

std::size_t utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
    }
    return count;
}

std::string truncateUtf8(std::string_view text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) return std::string(text);
    std::size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return std::string(text.substr(0, cut));
}

std::string truncateWithEllipsis(std::string_view text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) return std::string(text);
    if (maxBytes <= 3) return truncateUtf8(text, maxBytes);
    return truncateUtf8(text, maxBytes - 3) + "...";
}

std::string sanitizeUtf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t before = position;
        char32_t cp = 0;
        if (decodeUtf8(text, position, cp)) {
            out.append(text.substr(before, position - before));
        } else {
            out.append("\xEF\xBF\xBD");
            position = before + 1;
        }
    }
    return out;
}

namespace {

const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

std::string base64Encode(const std::vector<std::uint8_t>& data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < data.size()) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out.push_back(kBase64Alphabet[(chunk >> 18) & 63]);
        out.push_back(kBase64Alphabet[(chunk >> 12) & 63]);
        out.push_back(kBase64Alphabet[(chunk >> 6) & 63]);
        out.push_back(kBase64Alphabet[chunk & 63]);
        i += 3;
    }
    if (i + 1 == data.size()) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kBase64Alphabet[(chunk >> 18) & 63]);
        out.push_back(kBase64Alphabet[(chunk >> 12) & 63]);
        out += "==";
    } else if (i + 2 == data.size()) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kBase64Alphabet[(chunk >> 18) & 63]);
        out.push_back(kBase64Alphabet[(chunk >> 12) & 63]);
        out.push_back(kBase64Alphabet[(chunk >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::vector<std::uint8_t> base64Decode(std::string_view text) {
    std::vector<std::uint8_t> out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        const int value = base64Value(c);
        if (value < 0) continue;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFFu));
        }
    }
    return out;
}

}  // namespace eden
