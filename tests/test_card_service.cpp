#include "TestFramework.hpp"
#include "ai/CardService.hpp"
#include "db/Database.hpp"
#include "db/Migrations.hpp"
#include "doc/DocumentImporter.hpp"
#include "repo/AiSessionRepository.hpp"
#include "repo/CardRepository.hpp"
#include "repo/PackRepository.hpp"
#include "repo/SourceRepository.hpp"
#include "repo/UserRepository.hpp"

using namespace eden;

namespace {

SidecarOptions fakeOptions() {
    SidecarOptions options;
    options.command = {"python3", EDEN_FAKE_SIDECAR};
    options.defaultTimeoutMs = 10000;
    return options;
}

std::string sampleText() {
    std::string text = "CHAPTER ONE\n\n";
    for (int i = 0; i < 6; ++i) {
        text += "A river went out of Eden to water the garden. From there it divided into four rivers. ";
        text += "The name of the first is Pishon, which flows around the land of Havilah.\n\n";
    }
    return text;
}

struct Fixture {
    Database db = Database::openMemory();
    SidecarClient sidecar{fakeOptions()};
    CardService service{db, sidecar};
    User user;
    Id docId;
    std::vector<ResearchPack> packs;

    Fixture() {
        migrate(db);
        User candidate;
        candidate.displayName = "Reader";
        user = UserRepository(db).insert(candidate).value();
        DocumentImporter importer(db);
        ImportOptions options;
        options.ownerId = user.id;
        const std::string text = sampleText();
        auto imported = importer.importBytes(Bytes(text.begin(), text.end()), "genesis.txt", options);
        docId = imported.value().document.id;
        packs = PackRepository(db).listByDocument(docId).value();
    }
};

}  // namespace

EDEN_TEST(card_service_builds_context_with_neighbours_and_chapter) {
    Fixture f;
    const ResearchPack& pack = f.packs[2];
    auto context = f.service.buildContext(pack.id);
    EXPECT_TRUE(context.ok());
    EXPECT_EQ(context.value().getString("text"), pack.text);
    EXPECT_EQ(context.value().getString("doc_title"), std::string("CHAPTER ONE"));
    EXPECT_EQ(context.value().getString("chapter"), std::string("CHAPTER ONE"));
    EXPECT_FALSE(context.value().getString("before").empty());
    EXPECT_FALSE(context.value().getString("after").empty());
    EXPECT_FALSE(f.service.buildContext("missing").ok());
}

EDEN_TEST(card_service_runs_function_and_persists_everything) {
    Fixture f;
    const ResearchPack& pack = f.packs[1];
    std::vector<std::string> chunks;
    auto result = f.service.run(f.user.id, pack.id, "keypoints", {}, [&](const std::string& c) { chunks.push_back(c); });
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(chunks.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(result.value().provider, std::string("claude"));
    EXPECT_EQ(result.value().data.size(), static_cast<std::size_t>(2));
    CardRepository cards(f.db);
    auto section = cards.getSection(result.value().cardId, "keypoints");
    EXPECT_TRUE(section.ok());
    auto envelope = Json::parse(section.value().content);
    EXPECT_EQ(envelope.value().getString("text"), std::string("- first\n- second"));
    auto sessions = AiSessionRepository(f.db).listByPack(pack.id);
    EXPECT_EQ(sessions.value().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(sessions.value()[0].outputTokens, static_cast<std::int64_t>(5));
}

EDEN_TEST(card_service_stores_first_summary_on_the_card) {
    Fixture f;
    const ResearchPack& pack = f.packs[1];
    auto result = f.service.run(f.user.id, pack.id, "summary.plain");
    EXPECT_TRUE(result.ok());
    CardRepository cards(f.db);
    auto card = cards.get(result.value().cardId);
    EXPECT_TRUE(card.value().summaryText.find("Summary: ") == 0);
    const std::string first = card.value().summaryText;
    EXPECT_TRUE(f.service.run(f.user.id, pack.id, "summary.tweet").ok());
    EXPECT_EQ(cards.get(result.value().cardId).value().summaryText, first);
}

EDEN_TEST(card_service_surfaces_sidecar_errors) {
    Fixture f;
    EXPECT_FALSE(f.service.run("", f.packs[0].id, "keypoints").ok());
    auto missing = f.service.run(f.user.id, "no-pack", "keypoints");
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}

EDEN_TEST(card_service_compare_saves_each_provider_section) {
    Fixture f;
    auto outcome = f.service.compare(f.user.id, f.packs[1].id, "summary.plain", std::vector<std::string>{std::string("claude"), std::string("grok")});
    EXPECT_TRUE(outcome.ok());
    EXPECT_EQ(outcome.value().size(), static_cast<std::size_t>(2));
    CardRepository cards(f.db);
    auto card = cards.getByPack(f.packs[1].id);
    EXPECT_EQ(cards.listSections(card.value().id).value().size(), static_cast<std::size_t>(2));
}

EDEN_TEST(card_service_chat_keeps_history_between_turns) {
    Fixture f;
    auto first = f.service.chat(f.user.id, f.packs[1].id, "What is Pishon?");
    EXPECT_TRUE(first.ok());
    EXPECT_TRUE(first.value().text.find("(history 0)") != std::string::npos);
    auto second = f.service.chat(f.user.id, f.packs[1].id, "And Havilah?");
    EXPECT_TRUE(second.value().text.find("(history 2)") != std::string::npos);
}

EDEN_TEST(card_service_debate_records_turns) {
    Fixture f;
    auto outcome = f.service.debate(f.user.id, f.packs[1].id, "claude", "grok", 1, "it is true");
    EXPECT_TRUE(outcome.ok());
    auto sessions = AiSessionRepository(f.db).listByPackAndFunction(f.packs[1].id, "debate");
    EXPECT_EQ(sessions.value().size(), static_cast<std::size_t>(2));
}

EDEN_TEST(card_service_saves_only_allowed_search_results) {
    Fixture f;
    SourceRepository sources(f.db);
    EXPECT_TRUE(sources.addRule("bad.test", RuleKind::Block).ok());
    auto found = f.service.search("eden", "web", 5);
    EXPECT_TRUE(found.ok());
    CardRepository cards(f.db);
    ResearchCard card = cards.getOrCreateByPack(f.packs[1].id).value();
    auto saved = f.service.saveSearchResults(card.id, found.value(), {0, 1});
    EXPECT_TRUE(saved.ok());
    EXPECT_EQ(saved.value().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(saved.value()[0].url, std::string("https://example.org/eden"));
    EXPECT_FALSE(f.service.saveSearchResults(card.id, found.value(), {9}).ok());
}

EDEN_TEST(card_service_citation_and_quote_helpers) {
    Fixture f;
    auto citations = f.service.formatCitations(Json::object({{"title", Json("T")}}), std::vector<std::string>{std::string("apa"), std::string("mla")});
    EXPECT_TRUE(citations.ok());
    EXPECT_EQ(citations.value().get("citations").getString("apa"), std::string("CITE-apa"));
    auto located = f.service.locateQuote(f.packs[1].id, "water the garden");
    EXPECT_TRUE(located.ok());
    EXPECT_TRUE(located.value().getBool("found"));
    EXPECT_TRUE(f.service.configureSidecar(Json::object()).ok());
    EXPECT_TRUE(f.service.setProviderKey("claude", "sk").ok());
    EXPECT_EQ(f.service.providers().value().size(), static_cast<std::size_t>(1));
}
