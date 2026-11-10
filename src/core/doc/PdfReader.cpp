#include "doc/PdfReader.hpp"

#include <cctype>

#include "common/Strings.hpp"
#include "doc/PdfDocument.hpp"
#include "doc/PdfText.hpp"
#include "doc/TextUtil.hpp"

namespace eden {

namespace {

bool continuesParagraph(const std::string& text, const std::string& nextPageText) {
    if (text.empty() || nextPageText.empty()) return false;
    const char last = text.back();
    if (last == '.' || last == '?' || last == '!' || last == ':' || last == '"' || last == ')') return false;
    return std::islower(static_cast<unsigned char>(nextPageText[0])) != 0;
}

}  // namespace

Result<ExtractedDocument> readPdfDocument(const Bytes& data, const std::string& fileName) {
    auto parsed = PdfDocument::parse(data);
    if (!parsed.ok()) return parsed.error();
    const PdfDocument& pdf = parsed.value();

    ExtractedDocument doc;
    doc.type = FileType::Pdf;
    doc.title = pdf.infoString("Title");
    doc.author = pdf.infoString("Author");
    doc.warnings = pdf.warnings();

    PdfFontCache fonts;
    std::int64_t emptyPages = 0;
    std::int64_t index = 0;
    for (const PdfPage& page : pdf.pages()) {
        PdfPageText extracted = extractPdfPageText(pdf, page, fonts, doc.warnings);
        extracted.text = stripControlCharacters(extracted.text);
        if (trim(extracted.text).empty()) {
            ++emptyPages;
            const std::int64_t at = doc.charCount();
            doc.pages.push_back({index++, at, at});
            continue;
        }
        if (!doc.text.empty()) {
            const bool joined = continuesParagraph(doc.text, extracted.text);
            doc.text += joined ? " " : "\n\n";
        }
        const std::int64_t start = doc.charCount();
        doc.text += extracted.text;
        for (const PdfHeading& heading : extracted.headings) {
            ChapterMark mark;
            mark.title = heading.title;
            mark.offset = start + static_cast<std::int64_t>(heading.offset);
            mark.level = heading.level;
            doc.chapters.push_back(std::move(mark));
        }
        doc.pages.push_back({index++, start, doc.charCount()});
    }
    if (trim(doc.text).empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         "pdf contains no extractable text (it may consist of scanned images)");
    }
    if (emptyPages > 0) {
        doc.warnings.push_back(std::to_string(emptyPages) + " page(s) had no extractable text");
    }
    if (doc.title.empty()) {
        doc.title = doc.chapters.empty() ? fileStem(fileName) : doc.chapters.front().title;
    }
    return doc;
}

}  // namespace eden
