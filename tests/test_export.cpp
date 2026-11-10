#include <cstdlib>
#include <fstream>

#include "TestFramework.hpp"
#include "app/AppPaths.hpp"
#include "common/Strings.hpp"
#include "db/Migrations.hpp"
#include "doc/DocumentImporter.hpp"
#include "doc/DocumentLoader.hpp"
#include "doc/DocxReader.hpp"
#include "doc/PdfReader.hpp"
#include "doc/Inflate.hpp"
#include "doc/Zip.hpp"
#include "export/ExportModel.hpp"
#include "export/ExportService.hpp"
#include "export/ImageInfo.hpp"
#include "export/PdfWriter.hpp"
#include "export/SimpleExporters.hpp"
#include "export/SlideDeck.hpp"
#include "repo/CardRepository.hpp"
#include "repo/CollectionRepository.hpp"
#include "repo/PackRepository.hpp"
#include "repo/SourceRepository.hpp"
#include "repo/UserRepository.hpp"

using namespace eden;

namespace {

struct TempDir {
    std::string path;
    TempDir() { path = makeTemporaryDirectory("eden-export-").value(); }
    ~TempDir() { removeDirectoryTree(path); }
};

Bytes tinyPng() {
    ZipWriter unused;
    const Bytes raw = {0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
    Bytes scanlines;
    for (int row = 0; row < 2; ++row) {
        scanlines.push_back(0);
        scanlines.insert(scanlines.end(), raw.begin() + row * 6, raw.begin() + row * 6 + 6);
    }
    const Bytes packed = zlibWrapStored(scanlines.data(), scanlines.size());
    auto chunk = [](const std::string& type, const Bytes& body) {
        Bytes out;
        const std::uint32_t length = static_cast<std::uint32_t>(body.size());
        for (int shift : {24, 16, 8, 0}) out.push_back(static_cast<std::uint8_t>((length >> shift) & 0xFF));
        Bytes typed(type.begin(), type.end());
        typed.insert(typed.end(), body.begin(), body.end());
        out.insert(out.end(), typed.begin(), typed.end());
        const std::uint32_t crc = crc32(typed.data(), typed.size());
        for (int shift : {24, 16, 8, 0}) out.push_back(static_cast<std::uint8_t>((crc >> shift) & 0xFF));
        return out;
    };
    Bytes png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Bytes header = {0, 0, 0, 2, 0, 0, 0, 2, 8, 2, 0, 0, 0};
    for (const Bytes& part : {chunk("IHDR", header), chunk("IDAT", packed), chunk("IEND", {})}) png.insert(png.end(), part.begin(), part.end());
    return png;
}

ExportBundle sampleBundle(const std::string& imagePath = "") {
    ExportBundle bundle;
    bundle.title = "Genesis Study Guide";
    bundle.subtitle = "Notes on the garden";
    bundle.author = "Dr. Author";
    bundle.generatedAt = "2026-10-29T12:00:00.000Z";
    ExportCard card;
    card.packId = "pack-1";
    card.passage = "A river went out of Eden to water the garden.\nFrom there it divided into four rivers \xE2\x80\x94 \xE2\x80\x9Cthe first is Pishon\xE2\x80\x9D.";
    card.documentTitle = "Genesis Notes";
    card.chapter = "Chapter 2: The Garden";
    card.page = 3;
    card.summary = "A river flows from Eden and divides into four headwaters.";
    auto section = [&](const std::string& id, const std::string& title, const std::string& group, const std::string& output, const std::string& json,
                       const std::string& text) {
        ExportSection s;
        s.functionId = id;
        s.title = title;
        s.group = group;
        s.provider = "claude";
        s.text = text;
        if (!json.empty()) s.data = Json::parse(json).value();
        s.blocks = renderFunctionResult(id, output, s.data, text);
        card.sections.push_back(std::move(s));
    };
    section("keypoints", "Key points", "understand", "list", R"(["Four rivers","A garden in Eden"])", "");
    section("glossary", "Glossary", "understand", "json", R"([{"term":"Pishon","definition":"first river"},{"term":"Havilah","definition":"land of gold"}])", "");
    section("study_questions", "Study questions", "understand", "json", R"([{"question":"Where does the river begin?","level":"recall","answer_hint":"In Eden"}])", "");
    section("quotes.key", "Key quotes", "evidence", "json", R"([{"quote":"A river went out of Eden","why":"opens","verified":true,"start":0,"end":24}])", "");
    section("assist.critique", "Critique", "assist", "markdown", "", "## Strengths\n\nClear imagery.\n\n- concise\n- vivid\n\n1. first\n2. second");
    section("concept_map", "Concept map", "understand", "mermaid", "", "");
    card.sections.back().data = Json("graph TD\nEden-->River");
    card.sections.back().blocks = renderFunctionResult("concept_map", "mermaid", card.sections.back().data, "");
    Source source;
    source.title = "Garden of Eden";
    source.url = "https://example.org/eden";
    source.publisher = "example.org";
    card.sources.push_back(source);
    ExportComment original;
    original.author = "Dr. Author";
    original.body = "Note the four rivers.";
    original.kind = "text";
    original.writerOriginal = true;
    ExportComment reply;
    reply.author = "Reader";
    reply.body = "Agreed";
    reply.kind = "text";
    reply.depth = 1;
    card.comments = {original, reply};
    if (!imagePath.empty()) card.images.push_back({imagePath, "A river of gold"});
    bundle.cards.push_back(card);
    ExportCard second = card;
    second.chapter = "Chapter 3";
    second.summary = "Second passage summary.";
    second.images.clear();
    bundle.cards.push_back(second);
    return bundle;
}

void dumpIfRequested(const std::string& name, const Bytes& data) {
    const char* directory = std::getenv("EDEN_EXPORT_DUMP");
    if (directory == nullptr || *directory == '\0') return;
    writeFileBytes(std::string(directory) + "/" + name, data);
}

}  // namespace

EDEN_TEST(export_formats_parse_and_name_files) {
    EXPECT_TRUE(parseExportFormat("MD").value() == ExportFormat::Markdown);
    EXPECT_TRUE(parseExportFormat("eden-pdf").value() == ExportFormat::EdenPdf);
    EXPECT_FALSE(parseExportFormat("rtf").ok());
    ExportBundle bundle = sampleBundle();
    EXPECT_EQ(suggestedFileName(bundle, ExportFormat::Docx), std::string("genesis-study-guide.docx"));
    EXPECT_EQ(suggestedFileName(bundle, ExportFormat::EdenPdf), std::string("genesis-study-guide-illuminated.pdf"));
}

EDEN_TEST(markdown_and_text_exports_include_every_part) {
    const ExportBundle bundle = sampleBundle();
    const std::string md = exportMarkdown(bundle, {});
    EXPECT_TRUE(md.find("# Genesis Study Guide") == 0);
    EXPECT_TRUE(md.find("> A river went out of Eden") != std::string::npos);
    EXPECT_TRUE(md.find("**Summary.**") != std::string::npos);
    EXPECT_TRUE(md.find("- **Pishon** \xE2\x80\x94 first river") != std::string::npos);
    EXPECT_TRUE(md.find("1. Where does the river begin?") != std::string::npos);
    EXPECT_TRUE(md.find("```mermaid\ngraph TD") != std::string::npos);
    EXPECT_TRUE(md.find("[Garden of Eden](https://example.org/eden)") != std::string::npos);
    EXPECT_TRUE(md.find("**Author** **Dr. Author**") != std::string::npos);
    const std::string text = exportPlainText(bundle, {});
    EXPECT_TRUE(text.find("GENESIS STUDY GUIDE") == 0);
    EXPECT_TRUE(text.find("Pishon: first river") != std::string::npos);
    EXPECT_TRUE(text.find("(Author)") != std::string::npos);
}

EDEN_TEST(html_export_is_standalone_escaped_and_embeds_images) {
    TempDir dir;
    writeFileBytes(dir.path + "/pic.png", tinyPng());
    ExportBundle bundle = sampleBundle(dir.path + "/pic.png");
    bundle.cards[0].passage = "5 < 6 & \"quotes\"";
    const std::string html = exportHtml(bundle, {});
    EXPECT_TRUE(html.find("<!DOCTYPE html>") == 0);
    EXPECT_TRUE(html.find("5 &lt; 6 &amp; &quot;quotes&quot;") != std::string::npos);
    EXPECT_TRUE(html.find("data:image/png;base64,") != std::string::npos);
    EXPECT_TRUE(html.find("@media print") != std::string::npos);
    EXPECT_TRUE(html.find("<script") == std::string::npos);
    EXPECT_TRUE(html.find("&#10022; Author") != std::string::npos);
    dumpIfRequested("sample.html", Bytes(html.begin(), html.end()));
}

EDEN_TEST(anki_export_builds_cards_from_glossary_and_questions) {
    const ExportBundle bundle = sampleBundle();
    const std::string tsv = exportAnkiTsv(bundle);
    EXPECT_TRUE(tsv.find("#separator:tab") == 0);
    EXPECT_TRUE(tsv.find("Pishon\tfirst river\tgenesis-notes") != std::string::npos);
    EXPECT_TRUE(tsv.find("Where does the river begin?\tIn Eden") != std::string::npos);
    EXPECT_EQ(flashcardsFor(bundle.cards[0]).size(), static_cast<std::size_t>(3));
    ExportBundle empty = sampleBundle();
    for (ExportCard& card : empty.cards) card.sections.clear();
    EXPECT_FALSE(renderExport(ExportFormat::AnkiTsv, empty, {}).ok());
    EXPECT_FALSE(renderExport(ExportFormat::Markdown, ExportBundle(), {}).ok());
}

EDEN_TEST(slide_deck_uses_authored_slides_or_builds_from_cards) {
    ExportBundle bundle = sampleBundle();
    const auto generated = buildSlideDeck(bundle, {});
    EXPECT_TRUE(generated[0].isTitle);
    EXPECT_TRUE(generated.size() >= 6);
    EXPECT_EQ(generated[1].title, std::string("Chapter 2: The Garden"));
    ExportSection authored;
    authored.functionId = "output.slides";
    authored.data = Json::parse(R"([{"title":"Why rivers","bullets":["one","two"],"notes":"n"}])").value();
    bundle.cards[0].sections.push_back(authored);
    const auto custom = buildSlideDeck(bundle, {});
    bool found = false;
    for (const Slide& slide : custom) found = found || (slide.title == "Why rivers" && slide.bullets.size() == 2);
    EXPECT_TRUE(found);
    const std::string html = exportHtmlSlides(bundle, {});
    EXPECT_TRUE(html.find("ArrowRight") != std::string::npos && html.find("Why rivers") != std::string::npos);
    dumpIfRequested("sample-slides.html", Bytes(html.begin(), html.end()));
}

EDEN_TEST(docx_export_round_trips_through_the_docx_reader) {
    TempDir dir;
    writeFileBytes(dir.path + "/pic.png", tinyPng());
    const ExportBundle bundle = sampleBundle(dir.path + "/pic.png");
    auto bytes = renderExport(ExportFormat::Docx, bundle, {});
    EXPECT_TRUE(bytes.ok());
    dumpIfRequested("sample.docx", bytes.value());
    auto parsed = readDocxDocument(bytes.value(), "x.docx");
    EXPECT_TRUE(parsed.ok());
    const ExportCard& card = bundle.cards[0];
    EXPECT_EQ(parsed.value().title, std::string("Genesis Study Guide"));
    EXPECT_TRUE(parsed.value().text.find("A river went out of Eden to water the garden.") != std::string::npos);
    EXPECT_TRUE(parsed.value().text.find("A river flows from Eden") != std::string::npos);
    EXPECT_TRUE(parsed.value().text.find("Pishon") != std::string::npos);
    EXPECT_TRUE(parsed.value().text.find("Dr. Author") != std::string::npos);
    EXPECT_TRUE(parsed.value().chapters.size() >= 6);
    EXPECT_FALSE(card.passage.empty());
    auto zip = ZipReader::open(bytes.value());
    EXPECT_TRUE(zip.value().has("word/media/image1.png"));
    EXPECT_TRUE(zip.value().has("word/numbering.xml"));
}

EDEN_TEST(pdf_writer_text_metrics_and_encoding) {
    EXPECT_EQ(toWinAnsi("caf\xC3\xA9 \xE2\x80\x9Cq\xE2\x80\x9D \xE2\x80\x94 \xE2\x9C\xA6 \xE4\xB8\xAD"), std::string("caf\xE9 \x93q\x94 \x97 * ?"));
    EXPECT_TRUE(std::abs(PdfWriter::textWidth("Hello", PdfFace::Regular, 10) - 22.78) < 0.01);
    EXPECT_TRUE(PdfWriter::textWidth("Hello", PdfFace::Bold, 10) > PdfWriter::textWidth("Hello", PdfFace::Regular, 10));
    EXPECT_EQ(PdfWriter::textWidth("iii", PdfFace::Mono, 10), 18.0);
    const PdfColor gold = PdfColor::fromHex("#ffd54a");
    EXPECT_TRUE(gold.r > 0.99 && gold.b < 0.3);
}

EDEN_TEST(pdf_exports_open_in_the_pdf_reader_and_keep_text) {
    TempDir dir;
    writeFileBytes(dir.path + "/pic.png", tinyPng());
    ExportBundle bundle = sampleBundle(dir.path + "/pic.png");
    for (int i = 0; i < 6; ++i) {
        ExportCard extra = bundle.cards[1];
        extra.chapter = "Extra chapter " + std::to_string(i);
        extra.summary = std::string(900, 'x').replace(0, 4, "Long") + " words follow here to force wrapping across lines and pages again and again.";
        bundle.cards.push_back(extra);
    }
    for (bool illuminated : {false, true}) {
        auto bytes = renderExport(illuminated ? ExportFormat::EdenPdf : ExportFormat::Pdf, bundle, {});
        EXPECT_TRUE(bytes.ok());
        dumpIfRequested(illuminated ? "sample-eden.pdf" : "sample.pdf", bytes.value());
        auto parsed = readPdfDocument(bytes.value(), "x.pdf");
        EXPECT_TRUE(parsed.ok());
        EXPECT_TRUE(parsed.value().pages.size() >= 10);
        EXPECT_EQ(parsed.value().title, std::string("Genesis Study Guide"));
        EXPECT_TRUE(parsed.value().text.find("Genesis Study Guide") != std::string::npos);
        EXPECT_TRUE(parsed.value().text.find("A river went out of Eden") != std::string::npos);
        EXPECT_TRUE(parsed.value().text.find("Pishon") != std::string::npos);
        EXPECT_TRUE(parsed.value().text.find("Contents") != std::string::npos);
    }
}

EDEN_TEST(pptx_export_has_a_complete_package) {
    TempDir dir;
    writeFileBytes(dir.path + "/pic.png", tinyPng());
    const ExportBundle bundle = sampleBundle(dir.path + "/pic.png");
    auto bytes = renderExport(ExportFormat::Pptx, bundle, {});
    EXPECT_TRUE(bytes.ok());
    dumpIfRequested("sample.pptx", bytes.value());
    auto zip = ZipReader::open(bytes.value());
    EXPECT_TRUE(zip.ok());
    for (const char* name : {"[Content_Types].xml", "ppt/presentation.xml", "ppt/slides/slide1.xml", "ppt/slideMasters/slideMaster1.xml",
                             "ppt/slideLayouts/slideLayout1.xml", "ppt/theme/theme1.xml", "ppt/media/image1.png"}) {
        EXPECT_TRUE(zip.value().has(name));
    }
    EXPECT_TRUE(zip.value().readText("ppt/slides/slide1.xml").value().find("Genesis Study Guide") != std::string::npos);
}

EDEN_TEST(png_decoding_and_image_info) {
    const Bytes png = tinyPng();
    auto info = describeImage(png);
    EXPECT_TRUE(info.ok() && info.value().width == 2 && info.value().height == 2 && info.value().extension == "png");
    auto raw = decodePngRaw(png);
    EXPECT_TRUE(raw.ok());
    EXPECT_EQ(raw.value().pixels.size(), static_cast<std::size_t>(12));
    EXPECT_EQ(raw.value().pixels[0], static_cast<std::uint8_t>(0xFF));
    EXPECT_FALSE(describeImage(Bytes(40, 1)).ok());
    EXPECT_EQ(xmlSafe(std::string("a<b>\x01") + "c"), std::string("a&lt;b&gt;c"));
}

namespace {

struct Fixture {
    Database db = Database::openMemory();
    TempDir media;
    User author;
    Id docId;
    std::vector<ResearchPack> packs;
    FunctionDirectory directory = FunctionDirectory::fromJson(Json::parse(
        R"({"functions":[{"id":"keypoints","group":"understand","title":"Key points","output":"list"},
                          {"id":"glossary","group":"understand","title":"Glossary","output":"json"}]})").value());

    Fixture() {
        migrate(db);
        User a;
        a.displayName = "Author";
        author = UserRepository(db).insert(a).value();
        DocumentImporter importer(db);
        ImportOptions options;
        options.ownerId = author.id;
        const std::string text = "THE GARDEN\n\nA river went out of Eden to water the garden. It divided into four rivers.\n\nThe first river is Pishon, which flows around Havilah.";
        auto imported = importer.importBytes(Bytes(text.begin(), text.end()), "g.txt", options);
        docId = imported.value().document.id;
        packs = PackRepository(db).listByDocument(docId).value();
    }
};

}  // namespace

EDEN_TEST(export_builder_collects_cards_sections_comments_and_sources) {
    Fixture f;
    CardRepository cards(f.db);
    ResearchCard card = cards.getOrCreateByPack(f.packs[1].id).value();
    card.summaryText = "Summary text";
    card.aiImagesJson = "[]";
    card = cards.save(card).value();
    CardSection section;
    section.cardId = card.id;
    section.functionId = "keypoints";
    section.provider = "claude";
    section.content = Json::object({{"text", Json("- a\n- b")}, {"data", Json::parse(R"(["a","b"])").value()}}).dump();
    EXPECT_TRUE(cards.putSection(section).ok());
    Source source;
    source.cardId = card.id;
    source.url = "https://example.org/x";
    source.title = "X";
    EXPECT_TRUE(SourceRepository(f.db).add(source).ok());
    CommentService comments(f.db, f.media.path, f.author.id);
    EXPECT_TRUE(comments.addWriterOriginal(card.id, "Author note").ok());
    EXPECT_TRUE(comments.addText(card.id, "private thought", "", -1, true).ok());
    EXPECT_TRUE(comments.addText(card.id, "public thought", "", -1, false).ok());

    ExportBuilder builder(f.db, f.directory, comments);
    auto bundle = builder.forDocument(f.docId, {});
    EXPECT_TRUE(bundle.ok());
    EXPECT_EQ(bundle.value().cards.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(bundle.value().title, std::string("THE GARDEN - Study Guide"));
    EXPECT_EQ(bundle.value().author, std::string("Author"));
    const ExportCard& exported = bundle.value().cards[0];
    EXPECT_EQ(exported.summary, std::string("Summary text"));
    EXPECT_EQ(exported.sections.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(exported.sections[0].blocks.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(exported.sources.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(exported.comments.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(exported.comments[0].writerOriginal);
    ExportOptions withPrivate;
    withPrivate.includePrivateComments = true;
    EXPECT_EQ(builder.forDocument(f.docId, withPrivate).value().cards[0].comments.size(), static_cast<std::size_t>(3));
    ExportOptions everything;
    everything.onlyCardsWithContent = false;
    EXPECT_EQ(builder.forDocument(f.docId, everything).value().cards.size(), f.packs.size());
    EXPECT_FALSE(builder.forPacks({}, "x", {}).ok());
    EXPECT_FALSE(builder.forPacks({f.packs[0].id}, "x", {}).ok());
}

EDEN_TEST(export_builder_exports_collections_in_order) {
    Fixture f;
    CardRepository cards(f.db);
    CommentService comments(f.db, f.media.path, f.author.id);
    for (std::size_t i = 0; i < 3; ++i) {
        ResearchCard card = cards.getOrCreateByPack(f.packs[i].id).value();
        card.summaryText = "Summary " + std::to_string(i);
        cards.save(card);
    }
    CollectionRepository collections(f.db);
    Collection collection;
    collection.ownerId = f.author.id;
    collection.name = "Journey";
    collection.description = "A reading path";
    collection = collections.create(collection).value();
    collections.addPack(collection.id, f.packs[2].id);
    collections.addPack(collection.id, f.packs[0].id);
    ExportBuilder builder(f.db, f.directory, comments);
    auto bundle = builder.forCollection(collection.id, {});
    EXPECT_TRUE(bundle.ok());
    EXPECT_EQ(bundle.value().title, std::string("Journey"));
    EXPECT_EQ(bundle.value().subtitle, std::string("A reading path"));
    EXPECT_EQ(bundle.value().cards.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(bundle.value().cards[0].summary, std::string("Summary 2"));
    EXPECT_FALSE(builder.forCollection("missing", {}).ok());
}
