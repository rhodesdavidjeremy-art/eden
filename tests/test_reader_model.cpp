#include "TestFramework.hpp"
#include "app/LibraryModel.hpp"
#include "app/ReaderModel.hpp"
#include "db/Migrations.hpp"
#include "doc/DocumentImporter.hpp"
#include "repo/CardRepository.hpp"
#include "repo/UserRepository.hpp"

using namespace eden;

namespace {

std::string bookText() {
    std::string text = "THE GARDEN\n\n";
    for (int chapter = 0; chapter < 3; ++chapter) {
        text += "Chapter " + std::to_string(chapter + 1) + "\n\n";
        for (int i = 0; i < 30; ++i) {
            text += "A river went out of Eden to water garden number " + std::to_string(chapter * 100 + i) + ". ";
            text += "From there it divided and became four rivers running through the land of Havilah.\n\n";
        }
    }
    return text;
}

struct Fixture {
    Database db = Database::openMemory();
    User user;
    Document doc;
    LibraryModel* library = nullptr;
    std::unique_ptr<LibraryModel> libraryHolder;

    Fixture() {
        migrate(db);
        User candidate;
        candidate.displayName = "Reader";
        user = UserRepository(db).insert(candidate).value();
        libraryHolder = std::make_unique<LibraryModel>(db, user.id);
        library = libraryHolder.get();
        DocumentImporter importer(db);
        ImportOptions options;
        options.ownerId = user.id;
        const std::string text = bookText();
        doc = importer.importBytes(Bytes(text.begin(), text.end()), "garden.txt", options).value().document;
    }
};

}  // namespace

EDEN_TEST(reader_pages_cover_the_document_and_marks_stay_inside_pages) {
    Fixture f;
    ReaderModel reader(f.db);
    EXPECT_TRUE(reader.open(f.doc.id).ok());
    EXPECT_TRUE(reader.pageCount() > 2);
    std::string rebuilt;
    for (std::int64_t i = 0; i < reader.pageCount(); ++i) {
        auto view = reader.page(i);
        EXPECT_TRUE(view.ok());
        rebuilt += view.value().text;
        for (const PackMark& mark : view.value().marks) {
            EXPECT_TRUE(mark.start >= 0 && mark.end <= static_cast<std::int64_t>(view.value().text.size()));
            EXPECT_TRUE(mark.end > mark.start);
        }
    }
    EXPECT_EQ(rebuilt, reader.content().text);
    EXPECT_FALSE(reader.page(-1).ok());
    EXPECT_FALSE(reader.page(reader.pageCount()).ok());
}

EDEN_TEST(reader_marks_match_pack_text_and_flag_continuations) {
    Fixture f;
    ReaderModel reader(f.db);
    EXPECT_TRUE(reader.open(f.doc.id).ok());
    for (std::int64_t i = 0; i < reader.pageCount(); ++i) {
        const PageView view = reader.page(i).value();
        for (const PackMark& mark : view.marks) {
            const std::string shown = view.text.substr(static_cast<std::size_t>(mark.start),
                                                       static_cast<std::size_t>(mark.end - mark.start));
            const ResearchPack* pack = nullptr;
            for (const ResearchPack& candidate : reader.packs()) {
                if (candidate.id == mark.packId) pack = &candidate;
            }
            EXPECT_TRUE(pack != nullptr);
            if (!mark.continuesFromPrevious && !mark.continuesOnNext) EXPECT_EQ(shown, pack->text);
            else EXPECT_TRUE(pack->text.find(shown) != std::string::npos);
        }
    }
}

EDEN_TEST(reader_navigation_and_position_persistence) {
    Fixture f;
    {
        ReaderModel reader(f.db);
        EXPECT_TRUE(reader.open(f.doc.id).ok());
        EXPECT_EQ(reader.currentPage(), static_cast<std::int64_t>(0));
        EXPECT_FALSE(reader.previous().ok());
        EXPECT_TRUE(reader.next().ok());
        EXPECT_TRUE(reader.next().ok());
        EXPECT_EQ(reader.currentPage(), static_cast<std::int64_t>(2));
        EXPECT_TRUE(reader.progress() > 0.0);
        EXPECT_TRUE(reader.goToPage(reader.pageCount() - 1).ok());
        EXPECT_FALSE(reader.next().ok());
        EXPECT_FALSE(reader.goToPage(999).ok());
        EXPECT_TRUE(reader.goToPage(1).ok());
    }
    ReaderModel again(f.db);
    EXPECT_TRUE(again.open(f.doc.id).ok());
    EXPECT_EQ(again.currentPage(), static_cast<std::int64_t>(1));
    auto entries = f.library->entries();
    EXPECT_TRUE(entries.value()[0].progress > 0.0);
    EXPECT_TRUE(f.db.scalarInt("SELECT COUNT(*) FROM reading_sessions WHERE ended_at > 0") >= 1);
}

EDEN_TEST(reader_chapters_and_offsets) {
    Fixture f;
    ReaderModel reader(f.db);
    EXPECT_TRUE(reader.open(f.doc.id).ok());
    EXPECT_TRUE(reader.chapters().size() >= 3);
    EXPECT_TRUE(reader.goToChapter(2).ok());
    EXPECT_EQ(reader.currentOffset(), reader.chapters()[2].offset);
    EXPECT_EQ(reader.currentPage(), reader.content().pageIndexAt(reader.chapters()[2].offset));
    EXPECT_FALSE(reader.goToChapter(99).ok());
    EXPECT_TRUE(reader.goToOffset(0).ok());
    EXPECT_FALSE(reader.goToOffset(-1).ok());
    EXPECT_FALSE(reader.goToOffset(reader.content().charCount() + 1).ok());
    const PageView view = reader.page(reader.content().pageIndexAt(reader.chapters()[1].offset)).value();
    EXPECT_FALSE(view.chapter.empty());
}

EDEN_TEST(reader_creates_finds_and_removes_packs) {
    Fixture f;
    ReaderModel reader(f.db);
    EXPECT_TRUE(reader.open(f.doc.id).ok());
    const std::size_t before = reader.packs().size();
    const std::int64_t from = static_cast<std::int64_t>(reader.content().text.find("divided and became"));
    auto created = reader.createPack(from, from + 5, true);
    EXPECT_TRUE(created.ok());
    EXPECT_EQ(reader.packs().size(), before + 1);
    EXPECT_TRUE(created.value().text.find("divided and became") != std::string::npos);
    auto hit = reader.packAt(from + 2);
    EXPECT_TRUE(hit.ok());
    EXPECT_EQ(hit.value().id, created.value().id);
    EXPECT_FALSE(reader.packAt(reader.content().charCount() + 5).ok());
    EXPECT_TRUE(reader.removePack(created.value().id).ok());
    EXPECT_EQ(reader.packs().size(), before);
    EXPECT_FALSE(reader.createPack(5, 5, false).ok());
}

EDEN_TEST(reader_bookmarks_search_and_find) {
    Fixture f;
    ReaderModel reader(f.db);
    EXPECT_TRUE(reader.open(f.doc.id).ok());
    reader.goToPage(1);
    auto bookmark = reader.addBookmark("");
    EXPECT_TRUE(bookmark.ok());
    EXPECT_EQ(bookmark.value().label, std::string("Page 2"));
    EXPECT_EQ(reader.bookmarks().value().size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(reader.removeBookmark(bookmark.value().id).ok());
    EXPECT_EQ(reader.bookmarks().value().size(), static_cast<std::size_t>(0));

    auto hits = reader.search("Havilah", 5);
    EXPECT_TRUE(hits.ok());
    EXPECT_EQ(hits.value().size(), static_cast<std::size_t>(5));
    EXPECT_TRUE(hits.value()[0].snippet.find("[") != std::string::npos);

    const auto spans = reader.findInPage(0, "RIVER", false);
    EXPECT_TRUE(!spans.empty());
    EXPECT_TRUE(reader.findInPage(0, "RIVER", true).empty());
    const PageView first = reader.page(0).value();
    EXPECT_EQ(first.text.substr(static_cast<std::size_t>(spans[0].start), 5), std::string("river"));
    auto next = reader.findNext("number 100", 0);
    EXPECT_TRUE(next.has_value());
    EXPECT_EQ(reader.content().text.substr(static_cast<std::size_t>(*next), 10), std::string("number 100"));
    auto previous = reader.findPrevious("number", *next);
    EXPECT_TRUE(previous.has_value() && *previous < *next);
    EXPECT_FALSE(reader.findNext("zzzz-not-here", 0).has_value());
    EXPECT_FALSE(reader.findPrevious("number", 0).has_value());
}

EDEN_TEST(reader_rejects_use_before_open) {
    Fixture f;
    ReaderModel reader(f.db);
    EXPECT_FALSE(reader.view().ok());
    EXPECT_FALSE(reader.next().ok());
    EXPECT_FALSE(reader.addBookmark("x").ok());
    EXPECT_FALSE(reader.open("missing").ok());
}

EDEN_TEST(library_filters_sorts_renames_and_removes) {
    Fixture f;
    DocumentImporter importer(f.db);
    ImportOptions options;
    options.ownerId = f.user.id;
    const std::string other = "ZEBRA STUDY\n\nStripes and savannas are discussed here at some length.";
    EXPECT_TRUE(importer.importBytes(Bytes(other.begin(), other.end()), "zebra.txt", options).ok());
    auto all = f.library->entries();
    EXPECT_EQ(all.value().size(), static_cast<std::size_t>(2));
    LibraryQuery byTitle;
    byTitle.sort = LibrarySort::Title;
    EXPECT_EQ(f.library->entries(byTitle).value()[0].document.title, std::string("THE GARDEN"));
    LibraryQuery filtered;
    filtered.text = "zeb";
    EXPECT_EQ(f.library->entries(filtered).value().size(), static_cast<std::size_t>(1));
    const Id zebra = f.library->entries(filtered).value()[0].document.id;
    EXPECT_TRUE(f.library->rename(zebra, "  Stripes  ").ok());
    EXPECT_EQ(f.library->entries(filtered).value().size(), static_cast<std::size_t>(0));
    EXPECT_FALSE(f.library->rename(zebra, "   ").ok());
    auto stats = f.library->stats();
    EXPECT_EQ(stats.value().getInt("documents"), static_cast<std::int64_t>(2));
    EXPECT_TRUE(f.library->remove(zebra).ok());
    EXPECT_EQ(f.library->entries().value().size(), static_cast<std::size_t>(1));
    LibraryModel stranger(f.db, "someone-else");
    EXPECT_FALSE(stranger.remove(f.doc.id).ok());
    EXPECT_EQ(describeDocument(f.doc, 1), std::string("TXT \xC2\xB7 " + std::to_string(f.doc.pageCount) + " pages \xC2\xB7 1 pack"));
}
