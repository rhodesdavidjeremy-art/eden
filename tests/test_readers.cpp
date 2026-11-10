#include <string>

#include "TestFramework.hpp"
#include "doc/DocumentLoader.hpp"
#include "doc/DocxReader.hpp"
#include "doc/EpubReader.hpp"
#include "doc/Inflate.hpp"
#include "doc/PdfReader.hpp"
#include "doc/TextUtil.hpp"
#include "doc/TxtReader.hpp"
#include "doc/Zip.hpp"

using namespace eden;

namespace {

Bytes bytesOf(const std::string& text) { return Bytes(text.begin(), text.end()); }

std::string buildDocx() {
    return "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
           "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>The Garden</w:t></w:r></w:p>"
           "<w:p><w:r><w:t xml:space=\"preserve\">A river went out </w:t></w:r><w:r><w:t>of Eden.</w:t></w:r></w:p>"
           "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/></w:numPr></w:pPr><w:r><w:t>first point</w:t></w:r></w:p>"
           "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/></w:numPr></w:pPr><w:r><w:t>second point</w:t></w:r></w:p>"
           "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>Name</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>Value</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
           "<w:p><w:r><w:br w:type=\"page\"/><w:t>Second page text.</w:t></w:r></w:p>"
           "<w:p><w:del><w:r><w:delText>removed</w:delText></w:r></w:del></w:p>"
           "</w:body></w:document>";
}

Bytes makeDocx() {
    ZipWriter zip;
    zip.addText("[Content_Types].xml", "<Types/>");
    zip.addText("word/document.xml", buildDocx());
    zip.addText("docProps/core.xml",
                "<cp:coreProperties xmlns:cp=\"c\" xmlns:dc=\"d\"><dc:title>Eden Notes</dc:title>"
                "<dc:creator>A. Writer</dc:creator></cp:coreProperties>");
    return zip.finish();
}

Bytes makeEpub() {
    ZipWriter zip;
    zip.addText("mimetype", "application/epub+zip");
    zip.addText("META-INF/container.xml",
                "<container><rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
                "media-type=\"application/oebps-package+xml\"/></rootfiles></container>");
    zip.addText("OEBPS/content.opf",
                "<package><metadata xmlns:dc=\"d\"><dc:title>The Book</dc:title><dc:creator>An Author</dc:creator>"
                "<dc:language>en</dc:language></metadata><manifest>"
                "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
                "<item id=\"c1\" href=\"text/ch%201.xhtml\" media-type=\"application/xhtml+xml\"/>"
                "<item id=\"c2\" href=\"text/ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
                "</manifest><spine><itemref idref=\"nav\"/><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
    zip.addText("OEBPS/nav.xhtml", "<html><body><nav>Contents</nav></body></html>");
    zip.addText("OEBPS/text/ch 1.xhtml",
                "<html><head><title>x</title></head><body><h1>Chapter One</h1><p>It began &mdash; quietly.</p>"
                "<p>The river ran on.</p></body></html>");
    zip.addText("OEBPS/text/ch2.xhtml",
                "<html><body><h2>Chapter Two</h2><p>Then came the light.</p></body></html>");
    return zip.finish();
}

void addObject(std::string& pdf, int number, const std::string& body) {
    pdf += std::to_string(number) + " 0 obj\n" + body + "\nendobj\n";
}

void addStream(std::string& pdf, int number, const std::string& dictExtras, const std::string& data) {
    pdf += std::to_string(number) + " 0 obj\n<< /Length " + std::to_string(data.size()) + " " + dictExtras +
           " >>\nstream\n" + data + "\nendstream\nendobj\n";
}

std::string buildPdf() {
    std::string pdf = "%PDF-1.4\n";
    addObject(pdf, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    addObject(pdf, 2,
              "<< /Type /Pages /Kids [3 0 R 6 0 R 11 0 R] /Count 3 /Resources << /Font << /F1 5 0 R /F2 8 0 R >> >> >>");
    addObject(pdf, 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>");
    addStream(pdf, 4, "",
              "BT /F1 12 Tf 72 700 Td (Hello World) Tj 0 -14 Td (Second line of text here.) Tj ET");
    addObject(pdf, 5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    addObject(pdf, 6, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 7 0 R >>");
    addStream(pdf, 7, "", "BT /F2 12 Tf 72 700 Td <00010002> Tj ET");
    addObject(pdf, 8,
              "<< /Type /Font /Subtype /Type0 /BaseFont /Test /Encoding /Identity-H /DescendantFonts [9 0 R] "
              "/ToUnicode 10 0 R >>");
    addObject(pdf, 9, "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Test /DW 600 >>");
    addStream(pdf, 10, "",
              "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n"
              "1 begincodespacerange <0000> <FFFF> endcodespacerange\n"
              "2 beginbfchar <0001> <0048> <0002> <0069> endbfchar\nendcmap end end");
    addObject(pdf, 11, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 12 0 R >>");
    const std::string content = "BT /F1 12 Tf 72 700 Td [(Hel) -300 (lo)] TJ ET";
    const Bytes packed = zlibWrapStored(reinterpret_cast<const std::uint8_t*>(content.data()), content.size());
    addStream(pdf, 12, "/Filter /FlateDecode", std::string(packed.begin(), packed.end()));
    addObject(pdf, 13, "<< /Title (Test \\(PDF\\) Title) /Author (Jane) >>");
    pdf += "trailer\n<< /Root 1 0 R /Info 13 0 R /Size 14 >>\n%%EOF\n";
    return pdf;
}

}  // namespace

EDEN_TEST(text_decoding_handles_bom_and_legacy_encodings) {
    std::string encoding;
    EXPECT_EQ(decodeToUtf8(Bytes{0xEF, 0xBB, 0xBF, 'h', 'i'}, &encoding), std::string("hi"));
    EXPECT_EQ(decodeToUtf8(Bytes{0xFF, 0xFE, 'h', 0, 'i', 0}, &encoding), std::string("hi"));
    EXPECT_EQ(encoding, std::string("utf-16le"));
    EXPECT_EQ(decodeToUtf8(Bytes{0xFE, 0xFF, 0, 'h', 0, 'i'}, &encoding), std::string("hi"));
    EXPECT_EQ(decodeToUtf8(Bytes{'c', 'a', 'f', 0xE9}, &encoding), std::string("caf\xC3\xA9"));
    EXPECT_EQ(encoding, std::string("windows-1252"));
    EXPECT_EQ(decodeToUtf8(Bytes{0x93, 'q', 0x94}, &encoding), std::string("\xE2\x80\x9Cq\xE2\x80\x9D"));
}

EDEN_TEST(text_cleanup_helpers) {
    EXPECT_EQ(dehyphenate("inter-\nnational and well-\nKnown"), std::string("international and well-\nKnown"));
    EXPECT_EQ(collapseBlankLines("a\n\n\n\nb", 1), std::string("a\n\nb"));
    EXPECT_EQ(normalizeNewlines("a\r\nb\rc"), std::string("a\nb\nc"));
    EXPECT_EQ(fileStem("/tmp/my.book.txt"), std::string("my.book"));
}

EDEN_TEST(pagination_covers_text_without_gaps) {
    std::string text;
    for (int i = 0; i < 200; ++i) text += "Sentence number " + std::to_string(i) + " ends here.\n\n";
    std::vector<PageSpan> pages;
    paginateByLength(text, 0, static_cast<std::int64_t>(text.size()), 1000, 0, pages);
    EXPECT_TRUE(pages.size() > 3);
    std::int64_t cursor = 0;
    for (const PageSpan& page : pages) {
        EXPECT_EQ(page.start, cursor);
        EXPECT_TRUE(page.end > page.start);
        cursor = page.end;
    }
    EXPECT_EQ(cursor, static_cast<std::int64_t>(text.size()));
}

EDEN_TEST(txt_reader_extracts_title_chapters_and_pages) {
    const std::string text =
        "THE BOOK OF TESTS\n\nChapter 1\n\nIt was a dark and stormy night. The rain fell.\n\n"
        "Chapter II: The Morning\n\nThe sun rose over the hills.\n";
    auto result = readTxtDocument(bytesOf(text), "tests.txt");
    EXPECT_TRUE(result.ok());
    const ExtractedDocument& doc = result.value();
    EXPECT_EQ(doc.title, std::string("THE BOOK OF TESTS"));
    EXPECT_TRUE(doc.chapters.size() >= 2);
    EXPECT_EQ(doc.text.substr(static_cast<std::size_t>(doc.chapters[1].offset), 9), std::string("Chapter 1"));
    EXPECT_FALSE(doc.pages.empty());
    EXPECT_EQ(doc.pageIndexAt(0), static_cast<std::int64_t>(0));
}

EDEN_TEST(txt_reader_reflows_hard_wrapped_paragraphs) {
    std::string text;
    for (int paragraph = 0; paragraph < 3; ++paragraph) {
        text += "This is a hard wrapped line of prose that runs to about sixty chars\n";
        text += "and continues on a second line of similar length for the test run\n";
        text += "before ending the paragraph here.\n\n";
    }
    auto result = readTxtDocument(bytesOf(text), "wrapped.txt");
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result.value().text.find("chars and continues") != std::string::npos);
}

EDEN_TEST(txt_reader_rejects_empty_input) {
    EXPECT_FALSE(readTxtDocument(Bytes{}, "x.txt").ok());
    EXPECT_FALSE(readTxtDocument(bytesOf("   \n\n  "), "x.txt").ok());
}

EDEN_TEST(docx_reader_extracts_structure) {
    auto result = readDocxDocument(makeDocx(), "fallback.docx");
    EXPECT_TRUE(result.ok());
    const ExtractedDocument& doc = result.value();
    EXPECT_EQ(doc.title, std::string("Eden Notes"));
    EXPECT_EQ(doc.author, std::string("A. Writer"));
    EXPECT_TRUE(doc.text.find("A river went out of Eden.") != std::string::npos);
    EXPECT_TRUE(doc.text.find("\xE2\x80\xA2 first point\n\xE2\x80\xA2 second point") != std::string::npos);
    EXPECT_TRUE(doc.text.find("Name | Value") != std::string::npos);
    EXPECT_TRUE(doc.text.find("removed") == std::string::npos);
    EXPECT_EQ(doc.chapters.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(doc.chapters[0].title, std::string("The Garden"));
    EXPECT_EQ(doc.text.substr(static_cast<std::size_t>(doc.chapters[0].offset), 10), std::string("The Garden"));
    EXPECT_TRUE(doc.pages.size() >= 2);
    const std::size_t second = doc.text.find("Second page text.");
    EXPECT_EQ(doc.pageIndexAt(static_cast<std::int64_t>(second)), static_cast<std::int64_t>(1));
}

EDEN_TEST(docx_reader_rejects_other_zip_files) {
    ZipWriter zip;
    zip.addText("hello.txt", "x");
    EXPECT_FALSE(readDocxDocument(zip.finish(), "x.docx").ok());
    EXPECT_FALSE(readDocxDocument(Bytes{1, 2, 3, 4}, "x.docx").ok());
}

EDEN_TEST(epub_reader_follows_spine_and_decodes_markup) {
    auto result = readEpubDocument(makeEpub(), "book.epub");
    EXPECT_TRUE(result.ok());
    const ExtractedDocument& doc = result.value();
    EXPECT_EQ(doc.title, std::string("The Book"));
    EXPECT_EQ(doc.author, std::string("An Author"));
    EXPECT_EQ(doc.language, std::string("en"));
    EXPECT_TRUE(doc.text.find("It began \xE2\x80\x94 quietly.") != std::string::npos);
    EXPECT_TRUE(doc.text.find("Contents") == std::string::npos);
    EXPECT_EQ(doc.chapters.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(doc.chapters[0].title, std::string("Chapter One"));
    EXPECT_EQ(doc.chapters[1].title, std::string("Chapter Two"));
    EXPECT_EQ(doc.text.substr(static_cast<std::size_t>(doc.chapters[1].offset), 11), std::string("Chapter Two"));
}

EDEN_TEST(epub_path_resolution) {
    EXPECT_EQ(resolveArchivePath("OEBPS/", "text/ch%201.xhtml#frag"), std::string("OEBPS/text/ch 1.xhtml"));
    EXPECT_EQ(resolveArchivePath("OEBPS/text/", "../img/a.png"), std::string("OEBPS/img/a.png"));
    EXPECT_EQ(resolveArchivePath("OEBPS/", "/root.xhtml"), std::string("root.xhtml"));
}

EDEN_TEST(pdf_reader_extracts_text_across_fonts_and_filters) {
    const std::string pdf = buildPdf();
    auto result = readPdfDocument(bytesOf(pdf), "sample.pdf");
    EXPECT_TRUE(result.ok());
    const ExtractedDocument& doc = result.value();
    EXPECT_EQ(doc.title, std::string("Test (PDF) Title"));
    EXPECT_EQ(doc.author, std::string("Jane"));
    EXPECT_EQ(doc.pages.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(doc.pageText(0), std::string("Hello World Second line of text here."));
    EXPECT_EQ(doc.pageText(1), std::string("Hi"));
    EXPECT_EQ(doc.pageText(2), std::string("Hel lo"));
}

EDEN_TEST(pdf_reader_rejects_bad_input) {
    EXPECT_FALSE(readPdfDocument(bytesOf("not a pdf at all"), "x.pdf").ok());
    std::string encrypted = buildPdf();
    const std::size_t at = encrypted.find("/Size 14");
    encrypted.insert(at, "/Encrypt 99 0 R ");
    auto result = readPdfDocument(bytesOf(encrypted), "x.pdf");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::Unsupported);
}

EDEN_TEST(loader_sniffs_formats_by_content) {
    EXPECT_EQ(sniffFileType(bytesOf(buildPdf()), "noext"), FileType::Pdf);
    EXPECT_EQ(sniffFileType(makeDocx(), "x.bin"), FileType::Docx);
    EXPECT_EQ(sniffFileType(makeEpub(), "x.bin"), FileType::Epub);
    EXPECT_EQ(sniffFileType(bytesOf("plain words"), "x.md"), FileType::Txt);
    EXPECT_EQ(sniffFileType(Bytes{0, 1, 2, 3, 0, 0, 9, 8, 0, 0}, "x.dat"), FileType::Unknown);
    auto loaded = loadDocumentBytes(makeEpub(), FileType::Unknown, "book.epub");
    EXPECT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value().type, FileType::Epub);
    EXPECT_FALSE(loadDocumentBytes(Bytes{}, FileType::Txt, "e.txt").ok());
}
