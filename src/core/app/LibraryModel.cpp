#include "app/LibraryModel.hpp"

#include <algorithm>

#include "common/Strings.hpp"
#include "repo/CardRepository.hpp"
#include "repo/DocumentRepository.hpp"
#include "repo/PackRepository.hpp"

namespace eden {

std::string describeDocument(const Document& document, std::int64_t packCount) {
    std::string out = toUpperAscii(toString(document.fileType));
    if (document.pageCount > 0) {
        out += " \xC2\xB7 " + std::to_string(document.pageCount) + (document.pageCount == 1 ? " page" : " pages");
    }
    out += " \xC2\xB7 " + std::to_string(packCount) + (packCount == 1 ? " pack" : " packs");
    return out;
}

Result<std::vector<LibraryEntry>> LibraryModel::entries(const LibraryQuery& query) {
    DocumentRepository documents(db_);
    DocumentQuery request;
    request.ownerId = ownerId_;
    request.titleContains = query.text;
    request.limit = std::max(1, query.limit);
    switch (query.sort) {
        case LibrarySort::Title: request.sort = DocumentSort::Title; break;
        case LibrarySort::Added: request.sort = DocumentSort::Added; break;
        default: request.sort = DocumentSort::RecentlyOpened; break;
    }
    auto listed = documents.list(request);
    if (!listed.ok()) return listed.error();
    PackRepository packs(db_);
    CardRepository cards(db_);
    std::vector<LibraryEntry> out;
    for (const Document& document : listed.value()) {
        LibraryEntry entry;
        entry.document = document;
        auto packCount = packs.countByDocument(document.id);
        auto cardCount = cards.countByDocument(document.id);
        entry.packCount = packCount.ok() ? packCount.value() : 0;
        entry.cardCount = cardCount.ok() ? cardCount.value() : 0;
        entry.progress = document.charCount > 0
                             ? std::min(1.0, static_cast<double>(document.lastPosition) / static_cast<double>(document.charCount))
                             : 0.0;
        entry.subtitle = describeDocument(document, entry.packCount);
        out.push_back(std::move(entry));
    }
    if (query.sort == LibrarySort::Progress) {
        std::stable_sort(out.begin(), out.end(),
                         [](const LibraryEntry& a, const LibraryEntry& b) { return a.progress > b.progress; });
    }
    return out;
}

Result<ImportResult> LibraryModel::import(const std::string& path, ImportOptions options) {
    options.ownerId = ownerId_;
    DocumentImporter importer(db_);
    return importer.importFile(path, options);
}

Status LibraryModel::remove(const Id& docId) {
    DocumentRepository documents(db_);
    auto existing = documents.get(docId);
    if (!existing.ok()) return existing.error();
    if (existing.value().ownerId != ownerId_) return makeError(ErrorCode::Conflict, "document belongs to another user");
    return documents.remove(docId);
}

Status LibraryModel::rename(const Id& docId, const std::string& title) {
    const std::string cleaned = trim(title);
    if (cleaned.empty()) return makeError(ErrorCode::InvalidArgument, "title cannot be empty");
    DocumentRepository documents(db_);
    auto existing = documents.get(docId);
    if (!existing.ok()) return existing.error();
    if (existing.value().ownerId != ownerId_) return makeError(ErrorCode::Conflict, "document belongs to another user");
    Document document = existing.value();
    document.title = cleaned;
    return documents.update(document);
}

Result<Json> LibraryModel::stats() {
    return guarded<Json>([&]() {
        auto guard = db_.lock();
        Json out = Json::object();
        out["documents"] = db_.scalarInt("SELECT COUNT(*) FROM documents");
        out["packs"] = db_.scalarInt("SELECT COUNT(*) FROM research_packs");
        out["cards"] = db_.scalarInt("SELECT COUNT(*) FROM research_cards");
        out["comments"] = db_.scalarInt("SELECT COUNT(*) FROM comments");
        out["ai_sessions"] = db_.scalarInt("SELECT COUNT(*) FROM ai_sessions");
        out["characters"] = db_.scalarInt("SELECT COALESCE(SUM(char_count), 0) FROM documents");
        return out;
    });
}

}  // namespace eden
