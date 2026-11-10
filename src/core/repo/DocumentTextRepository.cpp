#include "repo/DocumentTextRepository.hpp"

#include "common/Clock.hpp"

namespace eden {

Status DocumentTextRepository::put(const Id& docId, const ExtractedDocument& doc) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "INSERT INTO document_texts(doc_id, text, pages_json, chapters_json, author, language, warnings_json, "
            "extracted_at) VALUES(:doc, :text, :pages, :chapters, :author, :language, :warnings, :now) "
            "ON CONFLICT(doc_id) DO UPDATE SET text = excluded.text, pages_json = excluded.pages_json, "
            "chapters_json = excluded.chapters_json, author = excluded.author, language = excluded.language, "
            "warnings_json = excluded.warnings_json, extracted_at = excluded.extracted_at");
        stmt.bindText(":doc", docId);
        stmt.bindText(":text", doc.text);
        stmt.bindText(":pages", pagesToJson(doc.pages).dump());
        stmt.bindText(":chapters", chaptersToJson(doc.chapters).dump());
        stmt.bindText(":author", doc.author);
        stmt.bindText(":language", doc.language);
        stmt.bindText(":warnings", Json::arrayOfStrings(doc.warnings).dump());
        stmt.bindInt(":now", nowMillis());
        stmt.execute();
    });
}

Result<ExtractedDocument> DocumentTextRepository::get(const Id& docId) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT d.title, d.file_type, t.text, t.pages_json, t.chapters_json, t.author, t.language, t.warnings_json "
            "FROM document_texts t JOIN documents d ON d.doc_id = t.doc_id WHERE t.doc_id = :doc");
        stmt.bindText(":doc", docId);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "no stored text for document " + docId);
        ExtractedDocument doc;
        doc.title = stmt.getText(0);
        doc.type = fileTypeFromString(stmt.getText(1));
        doc.text = stmt.getText(2);
        auto pages = Json::parse(stmt.getText(3));
        if (pages.ok()) doc.pages = pagesFromJson(pages.value());
        auto chapters = Json::parse(stmt.getText(4));
        if (chapters.ok()) doc.chapters = chaptersFromJson(chapters.value());
        doc.author = stmt.getText(5);
        doc.language = stmt.getText(6);
        auto warnings = Json::parse(stmt.getText(7));
        if (warnings.ok()) {
            doc.warnings.clear();
            for (const Json& item : warnings.value().items()) doc.warnings.push_back(item.asString());
        }
        return doc;
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::string> DocumentTextRepository::slice(const Id& docId, std::int64_t start, std::int64_t length) {
    if (start < 0 || length < 0) return makeError(ErrorCode::InvalidArgument, "negative slice bounds");
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT CAST(substr(CAST(text AS BLOB), :start, :length) AS TEXT) FROM document_texts WHERE doc_id = :doc");
        stmt.bindInt(":start", start + 1);
        stmt.bindInt(":length", length);
        stmt.bindText(":doc", docId);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "no stored text for document " + docId);
        return stmt.getText(0);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::int64_t> DocumentTextRepository::length(const Id& docId) {
    return guarded<std::int64_t>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("SELECT LENGTH(CAST(text AS BLOB)) FROM document_texts WHERE doc_id = :doc");
        stmt.bindText(":doc", docId);
        if (!stmt.step()) throw std::runtime_error("no stored text for document " + docId);
        return stmt.getInt(0);
    });
}

Result<std::vector<ChapterMark>> DocumentTextRepository::chapters(const Id& docId) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("SELECT chapters_json FROM document_texts WHERE doc_id = :doc");
        stmt.bindText(":doc", docId);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "no stored text for document " + docId);
        auto parsed = Json::parse(stmt.getText(0));
        if (!parsed.ok()) return parsed.error();
        return chaptersFromJson(parsed.value());
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Status DocumentTextRepository::remove(const Id& docId) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM document_texts WHERE doc_id = :doc");
        stmt.bindText(":doc", docId);
        stmt.execute();
    });
}

}  // namespace eden
