#include "export/ExportService.hpp"

#include "common/Strings.hpp"
#include "doc/Zip.hpp"
#include "export/DocxExporter.hpp"
#include "export/PdfExporter.hpp"
#include "export/PptxExporter.hpp"
#include "export/SimpleExporters.hpp"
#include "export/SlideDeck.hpp"

namespace eden {

namespace {

Bytes toBytes(const std::string& text) { return Bytes(text.begin(), text.end()); }

}  // namespace

Result<Bytes> renderExport(ExportFormat format, const ExportBundle& bundle, const ExportOptions& options) {
    if (bundle.cards.empty()) return makeError(ErrorCode::InvalidArgument, "nothing to export");
    switch (format) {
        case ExportFormat::Markdown: return toBytes(exportMarkdown(bundle, options));
        case ExportFormat::Text: return toBytes(exportPlainText(bundle, options));
        case ExportFormat::Html: return toBytes(exportHtml(bundle, options));
        case ExportFormat::Docx: return exportDocx(bundle, options);
        case ExportFormat::Pdf: return exportPdf(bundle, options, false);
        case ExportFormat::EdenPdf: return exportPdf(bundle, options, true);
        case ExportFormat::Pptx: return exportPptx(bundle, options);
        case ExportFormat::AnkiTsv: {
            const std::string tsv = exportAnkiTsv(bundle);
            if (tsv.find('\n', tsv.find("#tags column:3")) == std::string::npos || tsv.size() < 60) {
                return makeError(ErrorCode::NotFound, "no flashcards, glossary terms or study questions were found; run those research functions first");
            }
            return toBytes(tsv);
        }
        case ExportFormat::HtmlSlides: return toBytes(exportHtmlSlides(bundle, options));
    }
    return makeError(ErrorCode::InvalidArgument, "unsupported export format");
}

Status writeExport(ExportFormat format, const ExportBundle& bundle, const ExportOptions& options, const std::string& path) {
    auto rendered = renderExport(format, bundle, options);
    if (!rendered.ok()) return rendered.error();
    return writeFileBytes(path, rendered.value());
}

std::string suggestedFileName(const ExportBundle& bundle, ExportFormat format) {
    std::string stem = slugify(bundle.title);
    if (stem.empty()) stem = "eden-export";
    if (format == ExportFormat::EdenPdf) stem += "-illuminated";
    if (format == ExportFormat::HtmlSlides) stem += "-slides";
    return stem + "." + formatExtension(format);
}

}  // namespace eden
