#pragma once

#include <string>
#include <vector>

#include "app/CommentService.hpp"
#include "app/FunctionDirectory.hpp"
#include "app/RenderBlocks.hpp"
#include "common/Json.hpp"
#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

enum class ExportFormat { Markdown, Text, Html, Docx, Pdf, EdenPdf, Pptx, AnkiTsv, HtmlSlides };

const char* formatExtension(ExportFormat format);
const char* formatTitle(ExportFormat format);
Result<ExportFormat> parseExportFormat(const std::string& name);

struct ExportImage {
    std::string path;
    std::string caption;
};

struct ExportComment {
    std::string author;
    std::string body;
    std::string kind;
    std::string mediaName;
    bool writerOriginal = false;
    int depth = 0;
};

struct ExportSection {
    std::string functionId;
    std::string title;
    std::string group;
    std::string provider;
    std::string text;
    Json data;
    std::vector<RenderBlock> blocks;
};

struct ExportCard {
    Id packId;
    std::string passage;
    std::string documentTitle;
    std::string chapter;
    std::int64_t page = 0;
    std::string summary;
    std::vector<ExportSection> sections;
    std::vector<Source> sources;
    std::vector<ExportComment> comments;
    std::vector<ExportImage> images;
};

struct ExportBundle {
    std::string title;
    std::string subtitle;
    std::string author;
    std::string generatedAt;
    std::vector<ExportCard> cards;
};

struct ExportOptions {
    bool includeComments = true;
    bool includePrivateComments = false;
    bool includeSources = true;
    bool includeImages = true;
    bool onlyCardsWithContent = true;
    std::string palette = "pearl-light";
};

struct FlashCard {
    std::string front;
    std::string back;
    std::string tag;
};

std::vector<FlashCard> flashcardsFor(const ExportCard& card);

class ExportBuilder {
public:
    ExportBuilder(Database& db, const FunctionDirectory& directory, CommentService& comments)
        : db_(db), directory_(directory), comments_(comments) {}

    Result<ExportCard> buildCard(const Id& packId, const ExportOptions& options);
    Result<ExportBundle> forPacks(const std::vector<Id>& packIds, const std::string& title, const ExportOptions& options);
    Result<ExportBundle> forDocument(const Id& docId, const ExportOptions& options);
    Result<ExportBundle> forCollection(const Id& collectionId, const ExportOptions& options);

private:
    Database& db_;
    const FunctionDirectory& directory_;
    CommentService& comments_;
};

}  // namespace eden
