#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eden {

std::string trim(std::string_view text);
std::string toLowerAscii(std::string_view text);
std::string toUpperAscii(std::string_view text);
bool startsWith(std::string_view text, std::string_view prefix);
bool endsWith(std::string_view text, std::string_view suffix);
bool containsIgnoreCase(std::string_view haystack, std::string_view needle);
bool equalsIgnoreCase(std::string_view a, std::string_view b);
std::string replaceAll(std::string text, std::string_view from, std::string_view to);
std::vector<std::string> split(std::string_view text, char separator, bool keepEmpty = true);
std::vector<std::string> splitLines(std::string_view text);
std::string join(const std::vector<std::string>& parts, std::string_view separator);
std::string collapseWhitespace(std::string_view text);
std::size_t wordCount(std::string_view text);
std::string slugify(std::string_view text);
std::string escapeHtml(std::string_view text);
std::string escapeLikePattern(std::string_view text);
std::string formatBytes(std::uint64_t bytes);

bool isValidUtf8(std::string_view text);
std::size_t utf8Length(std::string_view text);
std::string truncateUtf8(std::string_view text, std::size_t maxBytes);
std::string truncateWithEllipsis(std::string_view text, std::size_t maxBytes);
std::string encodeUtf8(char32_t codepoint);
bool decodeUtf8(std::string_view text, std::size_t& position, char32_t& codepoint);
std::string sanitizeUtf8(std::string_view text);

std::string base64Encode(const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> base64Decode(std::string_view text);

}  // namespace eden
