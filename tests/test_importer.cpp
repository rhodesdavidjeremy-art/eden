#include "TestFramework.hpp"
#include "db/Database.hpp"
#include "db/Migrations.hpp"
#include "doc/DocumentImporter.hpp"
#include "repo/DocumentTextRepository.hpp"
#include "repo/PackRepository.hpp"
#include "repo/UserRepository.hpp"

using namespace eden;

namespace {

Bytes bytesOf(const std::string& text) { return Bytes(text.begin(), text.end()); }

std::string sampleText() {
    std::string text = "GENESIS NOTES\n\n";
    for (int i = 0; i < 8; ++i) {
        text += "A river went out of Eden to water the garden. From there it divided into four rivers. ";
        text += "The name of the first is Pishon, which flows around the land of Havilah.\n\n";
    }
    return text;
}

struct Fixture {
    Database db = Database::openMemory();
    User user;
    Fixture() {
        migrate(db);
        User candidate;
        candidate.displayName = "Reader";
        user = UserRepository(db).insert(candidate).value();
    }
};

}  // namespace

EDEN_TEST(importer_stores_document_text_and_packs) {
    Fixture f;
    DocumentImporter importer(f.db);
    ImportOptions options;
    options.ownerId = f.user.id;
    auto imported = importer.importBytes(bytesOf(sampleText()), "genesis.txt", options);
    EXPECT_TRUE(imported.ok());
    EXPECT_FALSE(imported.value().alreadyExisted);
    EXPECT_TRUE(imported.value().packCount > 4);
    EXPECT_EQ(imported.value().document.title, std::string("GENESIS NOTES"));
    DocumentTextRepository texts(f.db);
    auto stored = texts.get(imported.value().document.id);
    EXPECT_TRUE(stored.ok());
    EXPECT_EQ(stored.value().text.size(), static_cast<std::size_t>(imported.value().document.charCount));
    auto slice = texts.slice(imported.value().document.id, 0, 13);
    EXPECT_EQ(slice.value(), std::string("GENESIS NOTES"));
    PackRepository packs(f.db);
    EXPECT_EQ(packs.countByDocument(imported.value().document.id).value(), imported.value().packCount);
    EXPECT_TRUE(packs.search("Pishon", imported.value().document.id, 5).value().size() > 0);
}

EDEN_TEST(importer_deduplicates_by_content_hash) {
    Fixture f;
    DocumentImporter importer(f.db);
    ImportOptions options;
    options.ownerId = f.user.id;
    auto first = importer.importBytes(bytesOf(sampleText()), "a.txt", options);
    auto second = importer.importBytes(bytesOf(sampleText()), "renamed.txt", options);
    EXPECT_TRUE(first.ok() && second.ok());
    EXPECT_TRUE(second.value().alreadyExisted);
    EXPECT_EQ(second.value().document.id, first.value().document.id);
}

EDEN_TEST(importer_rolls_back_on_failure) {
    Fixture f;
    DocumentImporter importer(f.db);
    ImportOptions options;
    options.ownerId = f.user.id;
    EXPECT_FALSE(importer.importBytes(bytesOf("%PDF-1.4 broken"), "bad.pdf", options).ok());
    EXPECT_EQ(f.db.scalarInt("SELECT COUNT(*) FROM documents"), static_cast<std::int64_t>(0));
    ImportOptions orphan;
    EXPECT_FALSE(importer.importBytes(bytesOf(sampleText()), "x.txt", orphan).ok());
}

EDEN_TEST(importer_creates_manual_packs_with_snapping) {
    Fixture f;
    DocumentImporter importer(f.db);
    ImportOptions options;
    options.ownerId = f.user.id;
    options.autoSegment = false;
    auto imported = importer.importBytes(bytesOf(sampleText()), "g.txt", options);
    EXPECT_TRUE(imported.ok());
    const std::string text = sampleText();
    const std::int64_t from = static_cast<std::int64_t>(text.find("divided into"));
    auto pack = importer.createManualPack(imported.value().document.id, from, from + 7, true);
    EXPECT_TRUE(pack.ok());
    EXPECT_EQ(pack.value().text, std::string("From there it divided into four rivers."));
    EXPECT_EQ(pack.value().origin, PackOrigin::Manual);
    EXPECT_EQ(text.substr(static_cast<std::size_t>(pack.value().startOffset), pack.value().text.size()), pack.value().text);
}

EDEN_TEST(importer_regenerates_only_auto_packs) {
    Fixture f;
    DocumentImporter importer(f.db);
    ImportOptions options;
    options.ownerId = f.user.id;
    auto imported = importer.importBytes(bytesOf(sampleText()), "g.txt", options);
    const Id docId = imported.value().document.id;
    EXPECT_TRUE(importer.createManualPack(docId, 20, 40, true).ok());
    SegmenterOptions coarse;
    coarse.mode = SegmentMode::Paragraph;
    auto regenerated = importer.regenerateAutoPacks(docId, coarse);
    EXPECT_TRUE(regenerated.ok());
    PackRepository packs(f.db);
    auto all = packs.listByDocument(docId).value();
    std::size_t manual = 0;
    for (const ResearchPack& pack : all) manual += pack.origin == PackOrigin::Manual ? 1 : 0;
    EXPECT_EQ(manual, static_cast<std::size_t>(1));
    EXPECT_EQ(static_cast<std::int64_t>(all.size()), regenerated.value() + 1);
}
