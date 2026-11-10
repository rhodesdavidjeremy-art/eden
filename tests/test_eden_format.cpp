#include <fstream>

#include "TestFramework.hpp"
#include "common/Strings.hpp"
#include "app/AppController.hpp"
#include "app/AppPaths.hpp"
#include "app/AuthorTools.hpp"
#include "db/Migrations.hpp"
#include "doc/DocumentImporter.hpp"
#include "doc/DocumentLoader.hpp"
#include "doc/EdenFormat.hpp"
#include "doc/Zip.hpp"
#include "repo/CardRepository.hpp"
#include "repo/CommentRepository.hpp"
#include "repo/PackRepository.hpp"
#include "repo/SettingsRepository.hpp"
#include "repo/SourceRepository.hpp"
#include "repo/UserRepository.hpp"

using namespace eden;

namespace {

struct TempDir {
    std::string path;
    TempDir() { path = makeTemporaryDirectory("eden-format-").value(); }
    ~TempDir() { removeDirectoryTree(path); }
};

std::string bookText() {
    std::string text = "THE GARDEN\n\n";
    for (int i = 0; i < 12; ++i) {
        text += "A river went out of Eden to water the garden number " + std::to_string(i) + ". From there it divided into four rivers.\n\n";
    }
    return text;
}

Bytes wavBytes() { return Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 1, 2, 3, 4}; }

struct Publisher {
    Database db = Database::openMemory();
    TempDir dir;
    User author;
    Id docId;
    std::vector<ResearchPack> packs;

    Publisher() {
        migrate(db);
        User a;
        a.displayName = "Dr. Author";
        author = UserRepository(db).insert(a).value();
        DocumentImporter importer(db);
        ImportOptions options;
        options.ownerId = author.id;
        const std::string text = bookText();
        auto imported = importer.importBytes(Bytes(text.begin(), text.end()), "garden.txt", options);
        docId = imported.value().document.id;
        packs = PackRepository(db).listByDocument(docId).value();

        CardRepository cards(db);
        ResearchCard card = cards.getOrCreateByPack(packs[1].id).value();
        card.summaryText = "A river flows from Eden.";
        card = cards.save(card).value();
        CardSection section;
        section.cardId = card.id;
        section.functionId = "keypoints";
        section.provider = "claude";
        section.content = Json::object({{"text", Json("- a")}, {"data", Json::parse(R"(["a"])").value()}}).dump();
        cards.putSection(section);
        Source source;
        source.cardId = card.id;
        source.url = "https://example.org/eden";
        source.title = "Eden";
        SourceRepository(db).add(source);
        std::ofstream(dir.path + "/note.wav", std::ios::binary).write(reinterpret_cast<const char*>(wavBytes().data()), static_cast<std::streamsize>(wavBytes().size()));
        CommentService comments(db, dir.path + "/media", author.id);
        comments.addWriterOriginal(card.id, "Author's commentary");
        comments.addWriterOriginal(card.id, "Spoken note", dir.path + "/note.wav");
        comments.addText(card.id, "private reader thought", "", -1, true);
    }
};

}  // namespace

EDEN_TEST(enhanced_document_round_trips_between_libraries) {
    Publisher p;
    auto built = buildEnhancedDocument(p.db, p.docId, p.author.id, p.dir.path + "/media");
    EXPECT_TRUE(built.ok());
    EXPECT_TRUE(looksLikeEden(built.value()));
    EXPECT_EQ(sniffFileType(built.value(), "x.eden"), FileType::Eden);
    auto zip = ZipReader::open(built.value());
    EXPECT_TRUE(zip.value().has("eden.json") && zip.value().has("packs.json") && zip.value().has("text/document.txt"));
    EXPECT_EQ(zip.value().namesWithPrefix("cards/").size(), static_cast<std::size_t>(1));
    EXPECT_EQ(zip.value().namesWithPrefix("media/").size(), static_cast<std::size_t>(1));
    const std::string manifest = zip.value().readText("eden.json").value();
    EXPECT_TRUE(manifest.find("\"eden-enhanced\"") != std::string::npos);
    EXPECT_TRUE(manifest.find("private reader thought") == std::string::npos);

    Database reader = Database::openMemory();
    migrate(reader);
    User r;
    r.displayName = "Reader";
    const User readerUser = UserRepository(reader).insert(r).value();
    TempDir readerDir;
    DocumentImporter importer(reader);
    ImportOptions options;
    options.ownerId = readerUser.id;
    options.mediaDirectory = readerDir.path + "/media";
    options.imageDirectory = readerDir.path + "/images";
    auto imported = importer.importBytes(built.value(), "garden.eden", options);
    EXPECT_TRUE(imported.ok());
    EXPECT_FALSE(imported.value().alreadyExisted);
    EXPECT_TRUE(imported.value().document.isEnhanced);
    EXPECT_EQ(imported.value().document.title, std::string("THE GARDEN"));
    EXPECT_EQ(imported.value().packCount, static_cast<std::int64_t>(p.packs.size()));

    auto packs = PackRepository(reader).listByDocument(imported.value().document.id).value();
    EXPECT_EQ(packs.size(), p.packs.size());
    for (std::size_t i = 0; i < packs.size(); ++i) {
        EXPECT_EQ(packs[i].text, p.packs[i].text);
        EXPECT_TRUE(packs[i].origin == PackOrigin::Author);
        EXPECT_TRUE(packs[i].id != p.packs[i].id);
    }
    CardRepository cards(reader);
    auto card = cards.getByPack(packs[1].id);
    EXPECT_TRUE(card.ok());
    EXPECT_EQ(card.value().summaryText, std::string("A river flows from Eden."));
    EXPECT_EQ(cards.listSections(card.value().id).value().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(SourceRepository(reader).listByCard(card.value().id).value().size(), static_cast<std::size_t>(1));
    EXPECT_FALSE(cards.getByPack(packs[0].id).ok());
    auto comments = CommentRepository(reader).listByCard(card.value().id).value();
    EXPECT_EQ(comments.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(comments[0].isWriterOriginal && !comments[0].isPrivate);
    EXPECT_EQ(UserRepository(reader).get(comments[0].userId).value().displayName, std::string("Dr. Author"));
    CommentService readerComments(reader, readerDir.path + "/media", readerUser.id);
    auto thread = readerComments.thread(card.value().id);
    EXPECT_EQ(thread.value().size(), static_cast<std::size_t>(2));
    bool hasAudio = false;
    for (const CommentView& view : thread.value()) hasAudio = hasAudio || (view.comment.kind == CommentKind::Voice && !view.mediaPath.empty() && readFileBytes(view.mediaPath).ok());
    EXPECT_TRUE(hasAudio);
    EXPECT_FALSE(readerComments.addWriterOriginal(card.value().id, "forged").ok());
    EXPECT_FALSE(SettingsRepository(reader).getOr("eden.prompts." + imported.value().document.id, "").empty());

    auto again = importer.importBytes(built.value(), "garden.eden", options);
    EXPECT_TRUE(again.ok() && again.value().alreadyExisted);
    EXPECT_EQ(again.value().document.id, imported.value().document.id);
}

EDEN_TEST(publishing_is_limited_to_the_owner_and_selected_packs) {
    Publisher p;
    User other;
    other.displayName = "Someone";
    const User stranger = UserRepository(p.db).insert(other).value();
    EXPECT_EQ(buildEnhancedDocument(p.db, p.docId, stranger.id, p.dir.path).error().code, ErrorCode::Conflict);
    EdenPublishOptions subset;
    subset.packIds = {p.packs[2].id, p.packs[3].id};
    auto built = buildEnhancedDocument(p.db, p.docId, p.author.id, p.dir.path + "/media", subset);
    EXPECT_TRUE(built.ok());
    auto zip = ZipReader::open(built.value());
    const Json packs = Json::parse(zip.value().readText("packs.json").value()).value();
    EXPECT_EQ(packs.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(zip.value().namesWithPrefix("cards/").size(), static_cast<std::size_t>(0));
    EdenPublishOptions none;
    none.packIds = {"missing"};
    EXPECT_FALSE(buildEnhancedDocument(p.db, p.docId, p.author.id, p.dir.path, none).ok());
    EXPECT_FALSE(buildEnhancedDocument(p.db, "missing", p.author.id, p.dir.path).ok());
}

EDEN_TEST(enhanced_import_rejects_hostile_or_broken_archives) {
    Publisher p;
    auto built = buildEnhancedDocument(p.db, p.docId, p.author.id, p.dir.path + "/media");
    auto good = ZipReader::open(built.value()).value();
    auto rebuild = [&](const std::string& replaceName, const std::string& replacement, const std::string& extraName = "") {
        ZipWriter zip;
        for (const ZipEntry& entry : good.entries()) {
            if (entry.name == replaceName) zip.addText(entry.name, replacement);
            else zip.add(entry.name, good.read(entry.name).value());
        }
        if (!extraName.empty()) zip.addText(extraName, "x");
        return zip.finish();
    };
    Database db = Database::openMemory();
    migrate(db);
    User u;
    u.displayName = "R";
    ImportOptions options;
    options.ownerId = UserRepository(db).insert(u).value().id;
    DocumentImporter importer(db);
    auto manifest = good.readText("eden.json").value();

    EXPECT_FALSE(importer.importBytes(rebuild("eden.json", replaceAll(manifest, "\"version\": 1", "\"version\": 99")), "a.eden", options).ok());
    EXPECT_FALSE(importer.importBytes(rebuild("eden.json", replaceAll(manifest, "eden-enhanced", "something-else")), "a.eden", options).ok());
    EXPECT_FALSE(importer.importBytes(rebuild("packs.json", R"([{"id":"x","start":0,"end":999999}])"), "a.eden", options).ok());
    EXPECT_FALSE(importer.importBytes(rebuild("packs.json", "[]"), "a.eden", options).ok());
    EXPECT_FALSE(importer.importBytes(rebuild("packs.json", "not json"), "a.eden", options).ok());
    EXPECT_FALSE(importer.importBytes(rebuild("text/document.txt", "\xFF\xFE bad"), "a.eden", options).ok());
    auto traversal = importer.importBytes(rebuild("eden.json", manifest, "../evil.txt"), "a.eden", options);
    EXPECT_FALSE(traversal.ok());
    EXPECT_TRUE(traversal.error().message.find("unsafe") != std::string::npos);
    EXPECT_EQ(db.scalarInt("SELECT COUNT(*) FROM documents"), static_cast<std::int64_t>(0));
    EXPECT_TRUE(importer.importBytes(built.value(), "a.eden", options).ok());
    EXPECT_FALSE(isSafeArchiveName("a/../b") || isSafeArchiveName("/abs") || isSafeArchiveName("a\\b"));
    EXPECT_TRUE(isSafeArchiveName("media/ok.png"));
}

EDEN_TEST(author_batch_generates_cards_with_progress_and_cancellation) {
    Publisher p;
    SidecarOptions sidecarOptions;
    sidecarOptions.command = {"python3", EDEN_FAKE_SIDECAR};
    SidecarClient sidecar(sidecarOptions);
    CardService service(p.db, sidecar);
    AuthorBatchOptions options;
    options.docId = p.docId;
    options.functionIds = {"keypoints", "summary.plain"};
    std::vector<double> fractions;
    options.progress = [&](double fraction, const std::string&) { fractions.push_back(fraction); };
    auto report = runAuthorBatch(service, p.db, p.author.id, options);
    EXPECT_TRUE(report.ok());
    int eligible = 0;
    for (const ResearchPack& pack : p.packs) eligible += pack.text.size() >= 40 ? 1 : 0;
    EXPECT_EQ(report.value().total, eligible * 2);
    EXPECT_EQ(report.value().succeeded, report.value().total);
    EXPECT_EQ(report.value().failed, 0);
    EXPECT_TRUE(fractions.size() >= 3 && fractions.back() == 1.0);
    EXPECT_TRUE(CardRepository(p.db).getByPack(p.packs[3].id).ok());

    AuthorBatchOptions cancelled = options;
    std::atomic<bool> flag{true};
    cancelled.cancelFlag = &flag;
    auto stopped = runAuthorBatch(service, p.db, p.author.id, cancelled);
    EXPECT_TRUE(stopped.ok() && stopped.value().cancelled);
    AuthorBatchOptions empty;
    empty.docId = p.docId;
    EXPECT_FALSE(runAuthorBatch(service, p.db, p.author.id, empty).ok());
    AuthorBatchOptions subset = options;
    subset.packIds = {"nonexistent"};
    EXPECT_FALSE(runAuthorBatch(service, p.db, p.author.id, subset).ok());
}

EDEN_TEST(app_controller_exports_and_publishes_documents) {
    TempDir dir;
    AppOptions options;
    options.paths = pathsUnder(dir.path + "/data");
    options.sidecarCommand = {"python3", EDEN_FAKE_SIDECAR};
    auto created = AppController::create(std::move(options));
    EXPECT_TRUE(created.ok());
    AppController& app = *created.value();
    const std::string text = bookText();
    std::ofstream(dir.path + "/garden.txt") << text;
    auto imported = app.library().import(dir.path + "/garden.txt");
    EXPECT_TRUE(imported.ok());
    const Id docId = imported.value().document.id;
    auto packs = PackRepository(app.database()).listByDocument(docId).value();
    EXPECT_TRUE(app.cards().run(app.user().id, packs[1].id, "keypoints").ok());

    auto bundle = app.buildExport({}, docId, "", "", {});
    EXPECT_TRUE(bundle.ok());
    for (ExportFormat format : {ExportFormat::Markdown, ExportFormat::Html, ExportFormat::Docx, ExportFormat::Pdf, ExportFormat::EdenPdf, ExportFormat::Pptx, ExportFormat::HtmlSlides}) {
        const std::string path = dir.path + "/" + suggestedFileName(bundle.value(), format);
        EXPECT_TRUE(app.exportTo(format, bundle.value(), {}, path).ok());
        EXPECT_TRUE(readFileBytes(path).ok() && readFileBytes(path).value().size() > 200);
    }
    EXPECT_FALSE(app.buildExport({}, "", "", "x", {}).ok());
    EXPECT_TRUE(app.publishEnhanced(docId, dir.path + "/garden.eden").ok());
    auto published = readFileBytes(dir.path + "/garden.eden");
    EXPECT_TRUE(published.ok() && looksLikeEden(published.value()));

    std::mutex mutex;
    std::optional<AuthorBatchReport> report;
    app.authorBatchAsync(docId, {"summary.plain"}, nullptr, [&](Result<AuthorBatchReport> result) {
        std::lock_guard<std::mutex> lock(mutex);
        if (result.ok()) report = result.value();
    });
    app.tasks().waitIdle();
    EXPECT_TRUE(report.has_value() && report->succeeded > 5);
    app.shutdown();
}
