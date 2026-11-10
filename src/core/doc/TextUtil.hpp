#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "common/Types.hpp"
#include "doc/ExtractedDocument.hpp"

namespace eden {

std::string decodeToUtf8(const Bytes& data, std::string* detectedEncoding = nullptr);
std::string windows1252ToUtf8(const Bytes& data);
std::string utf16ToUtf8(const Bytes& data, bool littleEndian, std::size_t skip);

std::string normalizeNewlines(std::string_view text);
std::string stripControlCharacters(std::string_view text);
std::string trimLineEnds(std::string_view text);
std::string collapseBlankLines(std::string_view text, int maxConsecutiveBlank = 1);
std::string dehyphenate(std::string_view text);
std::string reflowHardWrapped(std::string_view text);
std::string cleanExtractedText(std::string_view text);

void paginateByLength(const std::string& text, std::int64_t rangeStart, std::int64_t rangeEnd,
                      std::size_t targetChars, std::int64_t firstIndex, std::vector<PageSpan>& pages);

std::string fileStem(const std::string& path);
std::string fileName(const std::string& path);

}  // namespace eden
