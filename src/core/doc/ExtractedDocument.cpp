#include "doc/ExtractedDocument.hpp"

#include <algorithm>

namespace eden {

std::int64_t ExtractedDocument::pageIndexAt(std::int64_t offset) const {
    if (pages.empty()) return 0;
    auto it = std::upper_bound(pages.begin(), pages.end(), offset,
                               [](std::int64_t value, const PageSpan& page) { return value < page.start; });
    if (it == pages.begin()) return pages.front().index;
    return (it - 1)->index;
}

std::string ExtractedDocument::pageText(std::int64_t index) const {
    for (const PageSpan& page : pages) {
        if (page.index == index) {
            return text.substr(static_cast<std::size_t>(page.start),
                               static_cast<std::size_t>(page.end - page.start));
        }
    }
    return std::string();
}

const ChapterMark* ExtractedDocument::chapterAt(std::int64_t offset) const {
    const ChapterMark* best = nullptr;
    for (const ChapterMark& chapter : chapters) {
        if (chapter.offset <= offset && (best == nullptr || chapter.offset >= best->offset)) best = &chapter;
    }
    return best;
}

Json pagesToJson(const std::vector<PageSpan>& pages) {
    Json out = Json::array();
    for (const PageSpan& page : pages) {
        out.push(Json::arrayOf({Json(page.index), Json(page.start), Json(page.end)}));
    }
    return out;
}

std::vector<PageSpan> pagesFromJson(const Json& json) {
    std::vector<PageSpan> pages;
    for (const Json& item : json.items()) {
        if (item.size() != 3) continue;
        PageSpan page;
        page.index = item.at(static_cast<std::size_t>(0)).asInt();
        page.start = item.at(static_cast<std::size_t>(1)).asInt();
        page.end = item.at(static_cast<std::size_t>(2)).asInt();
        pages.push_back(page);
    }
    return pages;
}

Json chaptersToJson(const std::vector<ChapterMark>& chapters) {
    Json out = Json::array();
    for (const ChapterMark& chapter : chapters) {
        Json entry = Json::object();
        entry["title"] = chapter.title;
        entry["offset"] = chapter.offset;
        entry["level"] = chapter.level;
        out.push(std::move(entry));
    }
    return out;
}

std::vector<ChapterMark> chaptersFromJson(const Json& json) {
    std::vector<ChapterMark> chapters;
    for (const Json& item : json.items()) {
        ChapterMark chapter;
        chapter.title = item.getString("title");
        chapter.offset = item.getInt("offset");
        chapter.level = static_cast<int>(item.getInt("level", 1));
        chapters.push_back(std::move(chapter));
    }
    return chapters;
}

}  // namespace eden
