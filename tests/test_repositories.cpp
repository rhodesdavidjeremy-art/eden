#include "TestFramework.hpp"
#include "db/Database.hpp"
#include "db/Migrations.hpp"
#include "repo/AiSessionRepository.hpp"
#include "repo/BookmarkRepository.hpp"
#include "repo/CardRepository.hpp"
#include "repo/CollectionRepository.hpp"
#include "repo/CommentRepository.hpp"
#include "repo/DocumentRepository.hpp"
#include "repo/PackRepository.hpp"
#include "repo/SettingsRepository.hpp"
#include "repo/SourceRepository.hpp"
#include "repo/UserRepository.hpp"

using namespace eden;

namespace {

struct Fixture {
    Database db = Database::openMemory();
    User user;
    Document doc;

    Fixture() {
        migrate(db);
        UserRepository users(db);
        User candidate;
        candidate.displayName = "Reader";
        user = users.insert(candidate).value();
        DocumentRepository docs(db);
        Document document;
        document.title = "Genesis Notes";
        document.fileType = FileType::Txt;
        document.ownerId = user.id;
        document.filePath = "/tmp/genesis.txt";
        document.contentHash = "hash-1";
        doc = docs.insert(document).value();
    }

    ResearchPack makePack(const std::string& text, std::int64_t start, std::int64_t order) {
        PackRepository packs(db);
        ResearchPack pack;
        pack.docId = doc.id;
        pack.text = text;
        pack.startOffset = start;
        pack.endOffset = start + static_cast<std::int64_t>(text.size());
        pack.orderIndex = order;
        return packs.insert(pack).value();
    }
};

}  // namespace

EDEN_TEST(documents_round_trip_and_dedupe_by_hash) {
    Fixture f;
    DocumentRepository docs(f.db);
    auto loaded = docs.get(f.doc.id);
    EXPECT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value().title, std::string("Genesis Notes"));
    EXPECT_TRUE(docs.findByHash(f.user.id, "hash-1").ok());
    EXPECT_FALSE(docs.findByHash(f.user.id, "other").ok());
    Document duplicate = f.doc;
    duplicate.id.clear();
    EXPECT_FALSE(docs.insert(duplicate).ok());
    EXPECT_TRUE(docs.recordOpened(f.doc.id, 77).ok());
    EXPECT_EQ(docs.get(f.doc.id).value().lastPosition, static_cast<std::int64_t>(77));
}

EDEN_TEST(documents_list_filters_by_title) {
    Fixture f;
    DocumentRepository docs(f.db);
    Document second = f.doc;
    second.id.clear();
    second.title = "Revelation Study";
    second.contentHash = "hash-2";
    EXPECT_TRUE(docs.insert(second).ok());
    DocumentQuery query;
    query.ownerId = f.user.id;
    query.titleContains = "revel";
    auto result = docs.list(query);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.value().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(docs.count(f.user.id).value(), static_cast<std::int64_t>(2));
}

EDEN_TEST(packs_support_range_lookup_and_ordering) {
    Fixture f;
    f.makePack("In the beginning", 0, 0);
    f.makePack("God created the heavens", 20, 1);
    PackRepository packs(f.db);
    EXPECT_EQ(packs.countByDocument(f.doc.id).value(), static_cast<std::int64_t>(2));
    auto at = packs.findAtOffset(f.doc.id, 25);
    EXPECT_TRUE(at.ok());
    EXPECT_EQ(at.value().text, std::string("God created the heavens"));
    EXPECT_FALSE(packs.findAtOffset(f.doc.id, 500).ok());
    auto range = packs.listInRange(f.doc.id, 0, 10);
    EXPECT_EQ(range.value().size(), static_cast<std::size_t>(1));
}

EDEN_TEST(packs_full_text_search_tracks_updates) {
    Fixture f;
    ResearchPack pack = f.makePack("a river flowed out of the garden", 0, 0);
    PackRepository packs(f.db);
    auto hits = packs.search("river garden", f.doc.id, 10);
    EXPECT_TRUE(hits.ok());
    EXPECT_EQ(hits.value().size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(packs.updateText(pack.id, "a golden jewel", 0, 14).ok());
    EXPECT_EQ(packs.search("river", f.doc.id, 10).value().size(), static_cast<std::size_t>(0));
    EXPECT_EQ(packs.search("jewel", f.doc.id, 10).value().size(), static_cast<std::size_t>(1));
}

EDEN_TEST(packs_reject_invalid_ranges) {
    Fixture f;
    PackRepository packs(f.db);
    ResearchPack bad;
    bad.docId = f.doc.id;
    bad.text = "x";
    bad.startOffset = 10;
    bad.endOffset = 5;
    EXPECT_FALSE(packs.insert(bad).ok());
}

EDEN_TEST(cards_keep_revision_history) {
    Fixture f;
    ResearchPack pack = f.makePack("text", 0, 0);
    CardRepository cards(f.db);
    auto created = cards.getOrCreateByPack(pack.id);
    EXPECT_TRUE(created.ok());
    ResearchCard card = created.value();
    card.summaryText = "first";
    card = cards.save(card).value();
    card.summaryText = "second";
    card = cards.save(card).value();
    EXPECT_EQ(card.revision, static_cast<std::int64_t>(3));
    auto revisions = cards.listRevisions(card.id);
    EXPECT_EQ(revisions.value().size(), static_cast<std::size_t>(2));
    auto restored = cards.restoreRevision(card.id, 2);
    EXPECT_TRUE(restored.ok());
    EXPECT_EQ(restored.value().summaryText, std::string("first"));
    EXPECT_EQ(cards.getOrCreateByPack(pack.id).value().id, card.id);
}

EDEN_TEST(card_sections_upsert) {
    Fixture f;
    ResearchPack pack = f.makePack("text", 0, 0);
    CardRepository cards(f.db);
    ResearchCard card = cards.create(pack.id).value();
    CardSection section;
    section.cardId = card.id;
    section.functionId = "summary.short";
    section.content = "one";
    EXPECT_TRUE(cards.putSection(section).ok());
    section.content = "two";
    EXPECT_TRUE(cards.putSection(section).ok());
    EXPECT_EQ(cards.listSections(card.id).value().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(cards.getSection(card.id, "summary.short").value().content, std::string("two"));
}

EDEN_TEST(writer_original_comments_are_pinned_and_immutable) {
    Fixture f;
    ResearchPack pack = f.makePack("text", 0, 0);
    CardRepository cards(f.db);
    ResearchCard card = cards.create(pack.id).value();
    CommentRepository comments(f.db);
    Comment reader;
    reader.cardId = card.id;
    reader.userId = f.user.id;
    reader.body = "reader note";
    Comment readerSaved = comments.insert(reader).value();
    Comment author;
    author.cardId = card.id;
    author.userId = f.user.id;
    author.body = "author note";
    author.isWriterOriginal = true;
    Comment authorSaved = comments.insert(author).value();
    auto listed = comments.listByCard(card.id);
    EXPECT_EQ(listed.value().front().id, authorSaved.id);
    EXPECT_FALSE(comments.editBody(authorSaved.id, f.user.id, "changed").ok());
    EXPECT_EQ(comments.remove(authorSaved.id, f.user.id).error().code, ErrorCode::Immutable);
    EXPECT_TRUE(comments.editBody(readerSaved.id, f.user.id, "edited").ok());
    EXPECT_TRUE(comments.remove(readerSaved.id, f.user.id).ok());
}

EDEN_TEST(comments_thread_and_react) {
    Fixture f;
    ResearchPack pack = f.makePack("text", 0, 0);
    CardRepository cards(f.db);
    ResearchCard card = cards.create(pack.id).value();
    CommentRepository comments(f.db);
    Comment root;
    root.cardId = card.id;
    root.userId = f.user.id;
    root.body = "root";
    root = comments.insert(root).value();
    Comment reply;
    reply.cardId = card.id;
    reply.userId = f.user.id;
    reply.body = "reply";
    reply.parentId = root.id;
    EXPECT_TRUE(comments.insert(reply).ok());
    EXPECT_EQ(comments.listReplies(root.id).value().size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(comments.addReaction(root.id, f.user.id, "ruby").ok());
    EXPECT_TRUE(comments.addReaction(root.id, f.user.id, "ruby").ok());
    auto counts = comments.reactionCounts(root.id);
    EXPECT_EQ(counts.value().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(counts.value().front().count, static_cast<std::int64_t>(1));
    Comment media;
    media.cardId = card.id;
    media.userId = f.user.id;
    media.kind = CommentKind::Voice;
    EXPECT_FALSE(comments.insert(media).ok());
}

EDEN_TEST(ai_sessions_cache_lookup_and_usage) {
    Fixture f;
    ResearchPack pack = f.makePack("text", 0, 0);
    AiSessionRepository sessions(f.db);
    AiSession session;
    session.userId = f.user.id;
    session.packId = pack.id;
    session.provider = "claude";
    session.functionId = "summary.short";
    session.prompt = "p";
    session.response = "r";
    session.inputTokens = 10;
    session.outputTokens = 20;
    EXPECT_TRUE(sessions.insert(session).ok());
    auto cached = sessions.latestSuccessful(pack.id, "summary.short", "claude");
    EXPECT_TRUE(cached.ok());
    EXPECT_EQ(cached.value().response, std::string("r"));
    EXPECT_FALSE(sessions.latestSuccessful(pack.id, "summary.short", "grok").ok());
    auto usage = sessions.usageByProvider(0);
    EXPECT_EQ(usage.value().front().outputTokens, static_cast<std::int64_t>(20));
}

EDEN_TEST(collections_reorder_and_list_packs) {
    Fixture f;
    ResearchPack a = f.makePack("alpha", 0, 0);
    ResearchPack b = f.makePack("beta", 10, 1);
    ResearchPack c = f.makePack("gamma", 20, 2);
    CollectionRepository collections(f.db);
    Collection collection;
    collection.ownerId = f.user.id;
    collection.name = "Journey";
    collection = collections.create(collection).value();
    EXPECT_TRUE(collections.addPack(collection.id, a.id).ok());
    EXPECT_TRUE(collections.addPack(collection.id, b.id).ok());
    EXPECT_TRUE(collections.addPack(collection.id, c.id).ok());
    EXPECT_TRUE(collections.movePack(collection.id, c.id, 0).ok());
    auto packs = collections.packs(collection.id);
    EXPECT_EQ(packs.value().front().text, std::string("gamma"));
    EXPECT_TRUE(collections.removePack(collection.id, a.id).ok());
    EXPECT_EQ(collections.itemCount(collection.id).value(), static_cast<std::int64_t>(2));
}

EDEN_TEST(source_rules_filter_domains) {
    Fixture f;
    SourceRepository sources(f.db);
    EXPECT_TRUE(sources.isAllowed("https://example.com/page"));
    EXPECT_TRUE(sources.addRule("spam.example", RuleKind::Block).ok());
    EXPECT_FALSE(sources.isAllowed("https://www.spam.example/x"));
    EXPECT_FALSE(sources.isAllowed("https://sub.spam.example/x"));
    EXPECT_TRUE(sources.addRule("scholar.org", RuleKind::Allow).ok());
    EXPECT_TRUE(sources.isAllowed("https://scholar.org/paper"));
    EXPECT_FALSE(sources.isAllowed("https://example.com/page"));
    EXPECT_EQ(extractDomain("https://User@WWW.Example.com:8080/a?b#c"), std::string("example.com"));
}

EDEN_TEST(settings_typed_accessors) {
    Fixture f;
    SettingsRepository settings(f.db);
    EXPECT_TRUE(settings.setInt("ui.fontSize", 14).ok());
    EXPECT_TRUE(settings.setBool("ui.rain", true).ok());
    EXPECT_EQ(settings.getIntOr("ui.fontSize", 0), static_cast<std::int64_t>(14));
    EXPECT_TRUE(settings.getBoolOr("ui.rain", false));
    EXPECT_EQ(settings.getOr("missing", "d"), std::string("d"));
    EXPECT_EQ(settings.withPrefix("ui.").value().size(), static_cast<std::size_t>(2));
    Json value = Json::object();
    value["a"] = 1;
    EXPECT_TRUE(settings.setJson("prefs", value).ok());
    EXPECT_EQ(settings.getJsonOr("prefs", Json()).getInt("a"), static_cast<std::int64_t>(1));
}

EDEN_TEST(bookmarks_and_reading_sessions) {
    Fixture f;
    BookmarkRepository bookmarks(f.db);
    Bookmark one;
    one.docId = f.doc.id;
    one.charOffset = 100;
    one.label = "start";
    Bookmark two = one;
    two.charOffset = 900;
    EXPECT_TRUE(bookmarks.add(one).ok());
    EXPECT_TRUE(bookmarks.add(two).ok());
    EXPECT_EQ(bookmarks.nearest(f.doc.id, 800).value().charOffset, static_cast<std::int64_t>(900));
    auto session = bookmarks.startReadingSession(f.doc.id, 0);
    EXPECT_TRUE(session.ok());
    EXPECT_TRUE(bookmarks.endReadingSession(session.value(), 50).ok());
    EXPECT_TRUE(bookmarks.totalReadingMillis(f.doc.id).ok());
}
