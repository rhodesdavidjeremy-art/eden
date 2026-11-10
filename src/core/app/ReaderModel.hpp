#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "doc/ExtractedDocument.hpp"
#include "doc/PackSegmenter.hpp"
#include "model/Models.hpp"

namespace eden {

struct PackMark {
    Id packId;
    std::int64_t start = 0;
    std::int64_t end = 0;
    bool hasCard = false;
    bool continuesFromPrevious = false;
    bool continuesOnNext = false;
    PackOrigin origin = PackOrigin::Auto;
};

struct PageView {
    std::int64_t index = 0;
    std::int64_t start = 0;
    std::int64_t end = 0;
    std::int64_t pageCount = 0;
    std::string text;
    std::vector<PackMark> marks;
    std::string chapter;
    double progress = 0.0;
};

class ReaderModel {
public:
    explicit ReaderModel(Database& db) : db_(db) {}
    ~ReaderModel();
    ReaderModel(const ReaderModel&) = delete;
    ReaderModel& operator=(const ReaderModel&) = delete;

    Status open(const Id& docId);
    void close();
    bool isOpen() const { return open_; }

    const Document& document() const { return document_; }
    const ExtractedDocument& content() const { return content_; }
    const std::vector<ChapterMark>& chapters() const { return content_.chapters; }
    const std::vector<ResearchPack>& packs() const { return packs_; }
    std::int64_t pageCount() const { return static_cast<std::int64_t>(content_.pages.size()); }
    std::int64_t currentPage() const { return currentPage_; }
    std::int64_t currentOffset() const { return offset_; }
    double progress() const;

    Result<PageView> view() const { return page(currentPage_); }
    Result<PageView> page(std::int64_t index) const;
    Status goToPage(std::int64_t index);
    Status next();
    Status previous();
    Status goToOffset(std::int64_t offset);
    Status goToChapter(std::size_t index);

    Result<ResearchPack> packAt(std::int64_t offset) const;
    Result<ResearchPack> createPack(std::int64_t start, std::int64_t end, bool snapToSentences = true);
    Status removePack(const Id& packId);
    Status reloadPacks();

    Result<Bookmark> addBookmark(const std::string& label);
    Result<std::vector<Bookmark>> bookmarks();
    Status removeBookmark(const Id& id);

    Result<std::vector<PackHit>> search(const std::string& query, int limit = 30);
    std::vector<TextSpan> findInPage(std::int64_t index, const std::string& needle, bool caseSensitive = false) const;
    std::optional<std::int64_t> findNext(const std::string& needle, std::int64_t fromOffset, bool caseSensitive = false) const;
    std::optional<std::int64_t> findPrevious(const std::string& needle, std::int64_t fromOffset, bool caseSensitive = false) const;

private:
    Status loadCardFlags();
    void persistPosition();

    Database& db_;
    bool open_ = false;
    Document document_;
    ExtractedDocument content_;
    std::vector<ResearchPack> packs_;
    std::set<Id> packsWithCards_;
    std::int64_t currentPage_ = 0;
    std::int64_t offset_ = 0;
    std::int64_t sessionId_ = 0;
};

}  // namespace eden
