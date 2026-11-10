#include "export/SimpleExporters.hpp"

#include <fstream>
#include <sstream>

#include "app/ThemeManager.hpp"
#include "common/Strings.hpp"
#include "doc/Zip.hpp"

namespace eden {

namespace {

std::string repeat(const std::string& unit, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) out += unit;
    return out;
}

std::string blockToMarkdown(const RenderBlock& block, int& numbered) {
    if (block.kind != BlockKind::Numbered) numbered = 0;
    switch (block.kind) {
        case BlockKind::Heading: return repeat("#", std::min(6, 3 + std::max(1, block.level))) + " " + block.text + "\n\n";
        case BlockKind::Paragraph: return block.text + "\n\n";
        case BlockKind::Bullet: return "- " + block.text + "\n";
        case BlockKind::Numbered: {
            std::string out = std::to_string(++numbered) + ". " + block.text + "\n";
            if (!block.detail.empty()) out += "   - Hint: " + block.detail + "\n";
            return out;
        }
        case BlockKind::Quote: {
            std::string out = "> " + block.text + "\n";
            if (!block.detail.empty()) out += ">\n> " + replaceAll(block.detail, "\n", "\n> ") + "\n";
            return out + "\n";
        }
        case BlockKind::KeyValue: {
            std::string out = "- **" + block.text + "**";
            if (!block.detail.empty()) out += " \xE2\x80\x94 " + replaceAll(block.detail, "\n", " ");
            return out + "\n";
        }
        case BlockKind::Code: return "```" + block.detail + "\n" + block.text + "\n```\n\n";
        case BlockKind::Badge: return "**[" + block.text + "]** " + block.detail + "\n\n";
    }
    return std::string();
}

std::string blockToText(const RenderBlock& block, int& numbered) {
    if (block.kind != BlockKind::Numbered) numbered = 0;
    switch (block.kind) {
        case BlockKind::Heading: return "\n" + toUpperAscii(block.text) + "\n";
        case BlockKind::Paragraph: return block.text + "\n\n";
        case BlockKind::Bullet: return "  * " + block.text + "\n";
        case BlockKind::Numbered: return "  " + std::to_string(++numbered) + ". " + block.text + (block.detail.empty() ? "" : " (hint: " + block.detail + ")") + "\n";
        case BlockKind::Quote: return "    \"" + block.text + "\"" + (block.detail.empty() ? "" : " - " + replaceAll(block.detail, "\n", " ")) + "\n";
        case BlockKind::KeyValue: return "  " + block.text + (block.detail.empty() ? "" : ": " + replaceAll(block.detail, "\n", " ")) + "\n";
        case BlockKind::Code: return block.text + "\n\n";
        case BlockKind::Badge: return "[" + block.text + "] " + block.detail + "\n";
    }
    return std::string();
}

std::string mimeFor(const std::string& path) {
    const std::string lower = toLowerAscii(path);
    if (endsWith(lower, ".png")) return "image/png";
    if (endsWith(lower, ".jpg") || endsWith(lower, ".jpeg")) return "image/jpeg";
    if (endsWith(lower, ".gif")) return "image/gif";
    if (endsWith(lower, ".webp")) return "image/webp";
    return "application/octet-stream";
}

}  // namespace

std::string imageDataUri(const std::string& path) {
    auto bytes = readFileBytes(path);
    if (!bytes.ok()) return std::string();
    return "data:" + mimeFor(path) + ";base64," + base64Encode(bytes.value());
}

std::string exportMarkdown(const ExportBundle& bundle, const ExportOptions& options) {
    std::ostringstream out;
    out << "# " << bundle.title << "\n\n";
    if (!bundle.subtitle.empty()) out << "_" << bundle.subtitle << "_\n\n";
    if (!bundle.author.empty()) out << "Prepared by " << bundle.author << "\n\n";
    out << "Generated " << bundle.generatedAt << " by Eden Research Reader\n\n---\n\n";
    int index = 0;
    for (const ExportCard& card : bundle.cards) {
        out << "## " << ++index << ". " << (card.chapter.empty() ? card.documentTitle : card.chapter) << "\n\n";
        out << "*" << card.documentTitle << ", page " << card.page << "*\n\n";
        out << "> " << replaceAll(card.passage, "\n", "\n> ") << "\n\n";
        if (!card.summary.empty()) out << "**Summary.** " << card.summary << "\n\n";
        if (options.includeImages) {
            for (const ExportImage& image : card.images) out << "![" << image.caption << "](" << image.path << ")\n\n";
        }
        for (const ExportSection& section : card.sections) {
            out << "### " << section.title << "\n\n";
            int numbered = 0;
            for (const RenderBlock& block : section.blocks) out << blockToMarkdown(block, numbered);
            out << "\n";
        }
        if (!card.sources.empty()) {
            out << "### Sources\n\n";
            for (const Source& source : card.sources) {
                out << "- [" << (source.title.empty() ? source.url : source.title) << "](" << source.url << ")";
                if (!source.publisher.empty()) out << " - " << source.publisher;
                out << "\n";
            }
            out << "\n";
        }
        if (!card.comments.empty()) {
            out << "### Discussion\n\n";
            for (const ExportComment& comment : card.comments) {
                out << repeat("  ", comment.depth) << "- " << (comment.writerOriginal ? "**Author** " : "") << "**" << comment.author << "**: "
                    << (comment.body.empty() ? "[" + comment.kind + " attachment]" : comment.body) << "\n";
            }
            out << "\n";
        }
        out << "---\n\n";
    }
    return out.str();
}

std::string exportPlainText(const ExportBundle& bundle, const ExportOptions& options) {
    std::ostringstream out;
    out << toUpperAscii(bundle.title) << "\n" << repeat("=", static_cast<int>(std::min<std::size_t>(bundle.title.size(), 72))) << "\n";
    if (!bundle.subtitle.empty()) out << bundle.subtitle << "\n";
    out << "Generated " << bundle.generatedAt << "\n\n";
    int index = 0;
    for (const ExportCard& card : bundle.cards) {
        out << ++index << ". " << (card.chapter.empty() ? card.documentTitle : card.chapter) << " (page " << card.page << ")\n";
        out << repeat("-", 40) << "\n\"" << card.passage << "\"\n\n";
        if (!card.summary.empty()) out << "Summary: " << card.summary << "\n\n";
        for (const ExportSection& section : card.sections) {
            out << "[" << section.title << "]\n";
            int numbered = 0;
            for (const RenderBlock& block : section.blocks) out << blockToText(block, numbered);
            out << "\n";
        }
        if (options.includeSources && !card.sources.empty()) {
            out << "Sources:\n";
            for (const Source& source : card.sources) out << "  - " << source.title << " <" << source.url << ">\n";
            out << "\n";
        }
        if (!card.comments.empty()) {
            out << "Discussion:\n";
            for (const ExportComment& comment : card.comments) {
                out << repeat("  ", comment.depth + 1) << comment.author << (comment.writerOriginal ? " (Author)" : "") << ": "
                    << (comment.body.empty() ? "[" + comment.kind + " attachment]" : comment.body) << "\n";
            }
            out << "\n";
        }
        out << "\n";
    }
    return out.str();
}

std::string exportHtml(const ExportBundle& bundle, const ExportOptions& options) {
    const Palette* found = findPalette(options.palette);
    const Palette& p = found != nullptr ? *found : builtinPalettes().back();
    std::ostringstream out;
    out << "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><title>" << escapeHtml(bundle.title) << "</title>\n<style>\n"
        << "body{background:" << p.background << ";color:" << p.text << ";font-family:Georgia,'Times New Roman',serif;max-width:820px;margin:0 auto;padding:32px;line-height:1.55}\n"
        << "h1,h2,h3{color:" << p.gold << ";font-weight:600}h1{border-bottom:2px solid " << p.gold << ";padding-bottom:8px}\n"
        << ".card{background:" << p.surface << ";border:1px solid " << p.border << ";border-radius:12px;padding:20px 26px;margin:28px 0;page-break-inside:avoid}\n"
        << ".passage{border-left:4px solid " << p.ruby << ";padding:6px 16px;background:" << p.surfaceAlt << ";font-style:italic}\n"
        << ".meta{color:" << p.mutedText << ";font-size:.9em}.seal{color:" << p.gold << ";font-weight:bold}\n"
        << ".comment{border-left:2px solid " << p.border << ";padding-left:10px;margin:8px 0}.comment.author{border-color:" << p.gold << "}\n"
        << "img{max-width:100%;border-radius:8px}a{color:" << p.sapphire << "}pre{background:" << p.surfaceAlt << ";padding:10px;overflow:auto}\n"
        << "@media print{body{background:#fff;color:#000}.card{border-color:#999;background:#fff}h1,h2,h3{color:#333}.passage{background:#f4f4f4}}\n"
        << "</style></head><body>\n<h1>" << escapeHtml(bundle.title) << "</h1>\n";
    if (!bundle.subtitle.empty()) out << "<p class=\"meta\">" << escapeHtml(bundle.subtitle) << "</p>\n";
    out << "<p class=\"meta\">Generated " << escapeHtml(bundle.generatedAt) << " by Eden Research Reader</p>\n";
    int index = 0;
    for (const ExportCard& card : bundle.cards) {
        out << "<section class=\"card\"><h2>" << ++index << ". " << escapeHtml(card.chapter.empty() ? card.documentTitle : card.chapter) << "</h2>\n";
        out << "<p class=\"meta\">" << escapeHtml(card.documentTitle) << " &middot; page " << card.page << "</p>\n";
        out << "<div class=\"passage\">" << replaceAll(escapeHtml(card.passage), "\n", "<br>") << "</div>\n";
        if (!card.summary.empty()) out << "<h3>Summary</h3><p>" << inlineToHtml(card.summary) << "</p>\n";
        if (options.includeImages) {
            for (const ExportImage& image : card.images) {
                const std::string uri = imageDataUri(image.path);
                if (!uri.empty()) out << "<figure><img src=\"" << uri << "\" alt=\"" << escapeHtml(image.caption) << "\"></figure>\n";
            }
        }
        for (const ExportSection& section : card.sections) {
            out << "<h3>" << escapeHtml(section.title) << "</h3>\n" << blocksToHtml(section.blocks, p) << "\n";
        }
        if (options.includeSources && !card.sources.empty()) {
            out << "<h3>Sources</h3><ul>\n";
            for (const Source& source : card.sources) {
                out << "<li><a href=\"" << escapeHtml(source.url) << "\">" << escapeHtml(source.title.empty() ? source.url : source.title) << "</a>";
                if (!source.publisher.empty()) out << " <span class=\"meta\">" << escapeHtml(source.publisher) << "</span>";
                out << "</li>\n";
            }
            out << "</ul>\n";
        }
        if (!card.comments.empty()) {
            out << "<h3>Discussion</h3>\n";
            for (const ExportComment& comment : card.comments) {
                out << "<div class=\"comment" << (comment.writerOriginal ? " author" : "") << "\" style=\"margin-left:" << comment.depth * 18 << "px\">";
                out << "<b>" << escapeHtml(comment.author) << "</b>";
                if (comment.writerOriginal) out << " <span class=\"seal\">&#10022; Author</span>";
                out << "<br>" << (comment.body.empty() ? "<i>" + escapeHtml(comment.kind) + " attachment</i>" : replaceAll(escapeHtml(comment.body), "\n", "<br>"))
                    << "</div>\n";
            }
        }
        out << "</section>\n";
    }
    out << "</body></html>\n";
    return out.str();
}

std::string exportAnkiTsv(const ExportBundle& bundle) {
    std::ostringstream out;
    out << "#separator:tab\n#html:true\n#tags column:3\n";
    auto cell = [](const std::string& text) {
        std::string clean = replaceAll(replaceAll(trim(text), "\t", " "), "\r", "");
        return replaceAll(escapeHtml(clean), "\n", "<br>");
    };
    for (const ExportCard& card : bundle.cards) {
        for (const FlashCard& flash : flashcardsFor(card)) out << cell(flash.front) << "\t" << cell(flash.back) << "\t" << flash.tag << "\n";
    }
    return out.str();
}

}  // namespace eden
