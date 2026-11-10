#pragma once

#include <string>
#include <vector>

#include "common/Json.hpp"
#include "common/Types.hpp"
#include "model/Models.hpp"

namespace eden {

struct PageSpan {
    std::int64_t index = 0;
    std::int64_t start = 0;
    std::int64_t end = 0;
};

struct ChapterMark {
    std::string title;
    std::int64_t offset = 0;
    int level = 1;
};

struct ExtractedDocument {
    FileType type = FileType::Unknown;
    std::string title;
    std::string author;
    std::string language;
    std::string text;
    std::vector<PageSpan> pages;
    std::vector<ChapterMark> chapters;
    std::vector<std::string> warnings;

    std::int64_t pageIndexAt(std::int64_t offset) const;
    std::string pageText(std::int64_t index) const;
    const ChapterMark* chapterAt(std::int64_t offset) const;
    std::int64_t charCount() const { return static_cast<std::int64_t>(text.size()); }
};

Json pagesToJson(const std::vector<PageSpan>& pages);
std::vector<PageSpan> pagesFromJson(const Json& json);
Json chaptersToJson(const std::vector<ChapterMark>& chapters);
std::vector<ChapterMark> chaptersFromJson(const Json& json);

}  // namespace eden
