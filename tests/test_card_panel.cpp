#include <cmath>
#include <fstream>

#include "TestFramework.hpp"
#include "common/Strings.hpp"
#include "app/AppPaths.hpp"
#include "app/BackgroundSimulation.hpp"
#include "app/CardPanelModel.hpp"
#include "app/CommentService.hpp"
#include "app/FunctionDirectory.hpp"
#include "app/RenderBlocks.hpp"
#include "db/Migrations.hpp"
#include "doc/DocumentImporter.hpp"
#include "repo/CardRepository.hpp"
#include "repo/PackRepository.hpp"
#include "repo/UserRepository.hpp"

using namespace eden;

namespace {

struct TempDir {
    std::string path;
    TempDir() { path = makeTemporaryDirectory("eden-panel-").value(); }
    ~TempDir() { removeDirectoryTree(path); }
};

void writeBytes(const std::string& path, const Bytes& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

Bytes pngBytes() {
    Bytes data = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    return data;
}

Bytes mp4Bytes() { return Bytes{0, 0, 0, 24, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 0, 0}; }

Bytes wavBytes() { return Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}; }

FunctionDirectory directory() {
    return FunctionDirectory::fromJson(Json::parse(
        R"({"functions":[
          {"id":"assist.critique","group":"assist","title":"Critique","output":"markdown"},
          {"id":"glossary","group":"understand","title":"Glossary","output":"json"},
          {"id":"keypoints","group":"understand","title":"Key points","output":"list"},
          {"id":"quotes.key","group":"evidence","title":"Quotes","output":"json"}]})").value());
}

struct Fixture {
    Database db = Database::openMemory();
    TempDir media;
    User author;
    User reader;
    Id cardId;
    Id packId;

    Fixture() {
        migrate(db);
        UserRepository users(db);
        User a;
        a.displayName = "Author";
        author = users.insert(a).value();
        User r;
        r.displayName = "Reader";
        reader = users.insert(r).value();
        DocumentImporter importer(db);
        ImportOptions options;
        options.ownerId = author.id;
        const std::string text = "THE GARDEN\n\nA river went out of Eden to water the garden. It divided into four rivers.";
        auto imported = importer.importBytes(Bytes(text.begin(), text.end()), "g.txt", options);
        packId = PackRepository(db).listByDocument(imported.value().document.id).value().back().id;
        cardId = CardRepository(db).getOrCreateByPack(packId).value().id;
    }
};

}  // namespace

EDEN_TEST(function_directory_orders_groups_and_round_trips) {
    const FunctionDirectory dir = directory();
    EXPECT_EQ(dir.all().front().group, std::string("understand"));
    EXPECT_EQ(dir.groups().size(), static_cast<std::size_t>(3));
    EXPECT_EQ(dir.inGroup("understand").size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(dir.find("quotes.key") != nullptr);
    EXPECT_TRUE(dir.find("missing") == nullptr);
    const FunctionDirectory again = FunctionDirectory::fromJson(dir.toJson());
    EXPECT_EQ(again.all().size(), dir.all().size());
    EXPECT_EQ(FunctionDirectory::groupTitle("assist"), std::string("AI Assistants"));
}

EDEN_TEST(markdown_blocks_and_html) {
    const auto blocks = markdownToBlocks("# Title\n\nFirst line\ncontinues here.\n\n- one\n- two\n1. step\n> quoted\n```\ncode()\n```");
    EXPECT_EQ(blocks.size(), static_cast<std::size_t>(7));
    EXPECT_TRUE(blocks[0].kind == BlockKind::Heading && blocks[0].level == 1);
    EXPECT_EQ(blocks[1].text, std::string("First line continues here."));
    EXPECT_TRUE(blocks[2].kind == BlockKind::Bullet && blocks[4].kind == BlockKind::Numbered);
    EXPECT_TRUE(blocks[5].kind == BlockKind::Quote && blocks[6].kind == BlockKind::Code);
    const std::string html = blocksToHtml(blocks, builtinPalettes().front());
    EXPECT_TRUE(html.find("<ul><li>one</li><li>two</li></ul>") != std::string::npos);
    EXPECT_TRUE(html.find("<blockquote") != std::string::npos);
    EXPECT_EQ(inlineToHtml("a **bold** <b> `x`"), std::string("a <b>bold</b> &lt;b&gt; <code>x</code>"));
}

EDEN_TEST(function_results_render_by_shape) {
    auto glossary = renderFunctionResult("glossary", "json", Json::parse(R"([{"term":"Eden","definition":"a garden"}])").value(), "");
    EXPECT_TRUE(glossary[0].kind == BlockKind::KeyValue && glossary[0].detail == "a garden");
    auto quotes = renderFunctionResult("quotes.key", "json",
                                       Json::parse(R"([{"quote":"A river","why":"opens","verified":true},{"quote":"Fake","why":"x","verified":false}])").value(), "");
    EXPECT_EQ(quotes[0].level, 1);
    EXPECT_TRUE(quotes[1].detail.find("Not found verbatim") != std::string::npos);
    auto level = renderFunctionResult("reading_level", "json", Json::parse(R"({"grade_level":9,"difficulty":"moderate","reasons":["long sentences"]})").value(), "");
    EXPECT_TRUE(level[0].kind == BlockKind::Badge && level[1].kind == BlockKind::Bullet);
    auto diagram = renderFunctionResult("concept_map", "mermaid", Json("graph TD\nA-->B"), "");
    EXPECT_TRUE(diagram[0].kind == BlockKind::Code && diagram[0].detail == "mermaid");
    auto list = renderFunctionResult("keypoints", "list", Json::parse(R"(["one","two"])").value(), "");
    EXPECT_EQ(list.size(), static_cast<std::size_t>(2));
    auto fallback = renderFunctionResult("assist.critique", "markdown", Json(), "## Strengths\n\nClear.");
    EXPECT_TRUE(fallback[0].kind == BlockKind::Heading);
    auto slides = renderFunctionResult("output.slides", "json", Json::parse(R"([{"title":"Intro","bullets":["a","b"],"notes":"say hi"}])").value(), "");
    EXPECT_EQ(slides.size(), static_cast<std::size_t>(4));
    auto generic = renderFunctionResult("unknown.fn", "json", Json::parse(R"([{"a":"first","b":"second"}])").value(), "");
    EXPECT_EQ(generic[0].detail, std::string("second"));
}

EDEN_TEST(media_sniffing_and_kind_matching) {
    EXPECT_TRUE(sniffMedia(pngBytes()).kind == MediaClass::Image);
    EXPECT_TRUE(sniffMedia(mp4Bytes()).kind == MediaClass::Video);
    EXPECT_TRUE(sniffMedia(wavBytes()).kind == MediaClass::Audio);
    EXPECT_TRUE(sniffMedia(Bytes(20, 0x41)).kind == MediaClass::Unknown);
    EXPECT_TRUE(mediaMatchesCommentKind(sniffMedia(mp4Bytes()), CommentKind::Camera));
    EXPECT_FALSE(mediaMatchesCommentKind(sniffMedia(pngBytes()), CommentKind::Video));
    Bytes webm = {0x1A, 0x45, 0xDF, 0xA3, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_TRUE(mediaMatchesCommentKind(sniffMedia(webm), CommentKind::Voice));
}

EDEN_TEST(comments_support_text_media_threads_and_reactions) {
    Fixture f;
    CommentService reader(f.db, f.media.path, f.reader.id);
    auto root = reader.addText(f.cardId, "  First thought  ", "", 5, false);
    EXPECT_TRUE(root.ok());
    EXPECT_EQ(root.value().body, std::string("First thought"));
    auto reply = reader.addText(f.cardId, "Reply", root.value().id, -1, false);
    EXPECT_TRUE(reply.ok());
    EXPECT_FALSE(reader.addText(f.cardId, "   ").ok());
    EXPECT_FALSE(reader.addText(f.cardId, "x", "missing-parent").ok());

    writeBytes(f.media.path + "/in.png", pngBytes());
    auto image = reader.addMedia(f.cardId, CommentKind::Image, f.media.path + "/in.png", "diagram", root.value().id, -1, 0, false);
    EXPECT_TRUE(image.ok());
    EXPECT_TRUE(image.value().mediaUri.size() > 60 && endsWith(image.value().mediaUri, ".png"));
    EXPECT_FALSE(reader.addMedia(f.cardId, CommentKind::Video, f.media.path + "/in.png").ok());
    EXPECT_FALSE(reader.addMedia(f.cardId, CommentKind::Image, f.media.path + "/missing.png").ok());
    writeBytes(f.media.path + "/in.mp4", mp4Bytes());
    EXPECT_TRUE(reader.addMedia(f.cardId, CommentKind::Camera, f.media.path + "/in.mp4", "selfie", "", 12, 4500, false).ok());
    writeBytes(f.media.path + "/in.wav", wavBytes());
    EXPECT_TRUE(reader.addMedia(f.cardId, CommentKind::Voice, f.media.path + "/in.wav").ok());

    EXPECT_TRUE(reader.react(root.value().id, "ruby").ok());
    EXPECT_TRUE(reader.react(root.value().id, "gold").ok());
    EXPECT_FALSE(reader.react(root.value().id, "heart").ok());
    auto thread = reader.thread(f.cardId);
    EXPECT_TRUE(thread.ok());
    EXPECT_EQ(thread.value().size(), static_cast<std::size_t>(5));
    EXPECT_EQ(thread.value()[0].comment.id, root.value().id);
    EXPECT_EQ(thread.value()[0].reactions.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(thread.value()[0].viewerReactions.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(thread.value()[1].depth, 1);
    EXPECT_EQ(thread.value()[0].authorName, std::string("Reader"));
    EXPECT_TRUE(reader.unreact(root.value().id, "ruby").ok());
    EXPECT_TRUE(reader.edit(root.value().id, "Edited thought").ok());
    EXPECT_TRUE(thread.value()[0].canEdit && thread.value()[0].canDelete);
}

EDEN_TEST(reply_depth_is_limited) {
    Fixture f;
    CommentService reader(f.db, f.media.path, f.reader.id);
    Id parent;
    int accepted = 0;
    for (int i = 0; i < 10; ++i) {
        auto next = reader.addText(f.cardId, "level " + std::to_string(i), parent, -1, false);
        if (!next.ok()) break;
        parent = next.value().id;
        ++accepted;
    }
    EXPECT_EQ(accepted, CommentService::kMaxDepth);
}

EDEN_TEST(private_hidden_and_writer_comments_follow_visibility_rules) {
    Fixture f;
    CommentService author(f.db, f.media.path, f.author.id);
    CommentService reader(f.db, f.media.path, f.reader.id);
    auto original = author.addWriterOriginal(f.cardId, "Author's note");
    EXPECT_TRUE(original.ok());
    EXPECT_TRUE(original.value().isWriterOriginal && !original.value().isPrivate);
    EXPECT_FALSE(reader.addWriterOriginal(f.cardId, "forged").ok());
    auto secret = reader.addText(f.cardId, "private note", "", -1, true);
    auto shared = reader.addText(f.cardId, "shared note", "", -1, false);
    EXPECT_TRUE(secret.ok() && shared.ok());

    EXPECT_EQ(author.thread(f.cardId).value().size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(author.thread(f.cardId).value()[0].comment.isWriterOriginal);
    EXPECT_EQ(reader.thread(f.cardId).value().size(), static_cast<std::size_t>(3));
    CommentService stranger(f.db, f.media.path, "someone-else");
    EXPECT_EQ(stranger.thread(f.cardId).value().size(), static_cast<std::size_t>(2));

    EXPECT_FALSE(reader.moderate(shared.value().id, true).ok());
    EXPECT_TRUE(author.moderate(shared.value().id, true).ok());
    EXPECT_FALSE(author.moderate(original.value().id, true).ok());
    EXPECT_EQ(stranger.thread(f.cardId).value().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(author.thread(f.cardId).value().size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(author.thread(f.cardId).value()[1].canModerate);
    EXPECT_TRUE(author.moderate(shared.value().id, false).ok());
    EXPECT_FALSE(reader.remove(original.value().id).ok());
    EXPECT_TRUE(reader.setPrivate(secret.value().id, false).ok());
    EXPECT_FALSE(author.setPrivate(secret.value().id, true).ok());
    EXPECT_EQ(stranger.thread(f.cardId).value().size(), static_cast<std::size_t>(3));
}

EDEN_TEST(card_panel_lists_every_function_and_renders_stored_results) {
    Fixture f;
    const FunctionDirectory dir = directory();
    CommentService comments(f.db, f.media.path, f.author.id);
    CardPanelModel panel(f.db, dir, comments);
    CardRepository cards(f.db);

    Json envelope = Json::object({{"text", Json("raw")}, {"model", Json("m1")},
                                  {"data", Json::parse(R"([{"term":"Eden","definition":"garden"}])").value()}});
    CardSection glossary;
    glossary.cardId = f.cardId;
    glossary.functionId = "glossary";
    glossary.content = envelope.dump();
    glossary.provider = "claude";
    EXPECT_TRUE(cards.putSection(glossary).ok());
    CardSection compared = glossary;
    compared.functionId = "assist.critique@grok";
    compared.content = Json::object({{"text", Json("## Verdict\n\nSolid.")}}).dump();
    compared.provider = "grok";
    EXPECT_TRUE(cards.putSection(compared).ok());
    CardSection stray = glossary;
    stray.functionId = "retired.function";
    stray.content = "not json at all";
    EXPECT_TRUE(cards.putSection(stray).ok());
    EXPECT_TRUE(comments.addText(f.cardId, "note", "", -1, false).ok());

    auto view = panel.load(f.packId);
    EXPECT_TRUE(view.ok());
    EXPECT_EQ(view.value().sections.size(), static_cast<std::size_t>(5));
    EXPECT_EQ(view.value().filledSections, static_cast<std::int64_t>(3));
    EXPECT_EQ(view.value().commentCount, static_cast<std::int64_t>(1));
    const auto understand = panel.sectionsInGroup(view.value(), "understand");
    EXPECT_EQ(understand.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(understand[0].functionId, std::string("glossary"));
    EXPECT_TRUE(understand[0].hasContent && understand[0].blocks[0].kind == BlockKind::KeyValue);
    EXPECT_EQ(understand[0].model, std::string("m1"));
    EXPECT_FALSE(understand[1].hasContent);
    const auto assist = panel.sectionsInGroup(view.value(), "assist");
    EXPECT_EQ(assist[0].title, std::string("Critique (grok)"));
    EXPECT_TRUE(assist[0].blocks[0].kind == BlockKind::Heading);
    EXPECT_EQ(panel.sectionsInGroup(view.value(), "other").size(), static_cast<std::size_t>(1));
    EXPECT_FALSE(panel.load("missing").ok());
}

EDEN_TEST(card_panel_handles_packs_without_cards) {
    Fixture f;
    const FunctionDirectory dir = directory();
    CommentService comments(f.db, f.media.path, f.author.id);
    CardPanelModel panel(f.db, dir, comments);
    const Id otherPack = PackRepository(f.db).listByDocument(PackRepository(f.db).get(f.packId).value().docId).value().front().id;
    auto view = panel.load(otherPack);
    EXPECT_TRUE(view.ok());
    EXPECT_TRUE(view.value().cardId.empty());
    EXPECT_EQ(view.value().filledSections, static_cast<std::int64_t>(0));
    EXPECT_EQ(view.value().sections.size(), static_cast<std::size_t>(4));
}

EDEN_TEST(background_simulation_is_deterministic_and_bounded) {
    const BackgroundPreset& river = *findBackground("golden-river");
    BackgroundSimulation a(river, 100, 7);
    BackgroundSimulation b(river, 100, 7);
    a.resize(1000, 700);
    b.resize(1000, 700);
    for (int i = 0; i < 120; ++i) {
        a.step(1.0 / 30.0);
        b.step(1.0 / 30.0);
    }
    EXPECT_EQ(a.frame().particles.size(), static_cast<std::size_t>(100));
    for (std::size_t i = 0; i < a.frame().particles.size(); ++i) {
        EXPECT_EQ(a.frame().particles[i].x, b.frame().particles[i].x);
        EXPECT_TRUE(a.frame().particles[i].alpha >= 0.0 && a.frame().particles[i].alpha <= 1.0);
        EXPECT_TRUE(a.frame().particles[i].y > 0.0 && a.frame().particles[i].y < 1000.0);
    }
    const SimPoint start = a.riverPoint(0.0);
    EXPECT_EQ(start.x, a.frame().logo.x);
    EXPECT_EQ(start.y, a.frame().logo.y);
    EXPECT_TRUE(a.riverPoint(1.0).y > 700.0);
}

EDEN_TEST(background_presets_produce_their_signature_effects) {
    BackgroundSimulation clouds(*findBackground("crimson-clouds"), 40, 3);
    clouds.resize(800, 600);
    bool sawBolt = false;
    double maxFlash = 0.0;
    for (int i = 0; i < 900 && !sawBolt; ++i) {
        clouds.step(1.0 / 30.0);
        sawBolt = !clouds.frame().bolts.empty();
        maxFlash = std::max(maxFlash, clouds.frame().flash);
    }
    EXPECT_TRUE(sawBolt);
    EXPECT_TRUE(maxFlash > 0.0 && maxFlash <= 0.25);
    EXPECT_TRUE(clouds.frame().bolts[0].points.size() >= 10);
    for (int i = 0; i < 200; ++i) clouds.step(1.0 / 30.0);

    BackgroundSimulation rain(*findBackground("jewel-rain"), 30, 3);
    rain.resize(800, 600);
    rain.step(0.05);
    EXPECT_EQ(rain.frame().streaks.size(), static_cast<std::size_t>(120));
    BackgroundSimulation aurora(*findBackground("aurora"), 60, 3);
    aurora.step(0.05);
    EXPECT_EQ(aurora.frame().bands.size(), static_cast<std::size_t>(3));
    EXPECT_FALSE(aurora.frame().stars.empty());
    BackgroundSimulation space(*findBackground("deep-space"), 220, 3);
    EXPECT_TRUE(space.frame().stars.size() >= 220);
}

EDEN_TEST(reduced_motion_freezes_and_clears_the_scene) {
    BackgroundSimulation sim(*findBackground("golden-river"), 80, 5);
    sim.step(0.05);
    EXPECT_FALSE(sim.frame().particles.empty());
    sim.setReducedMotion(true);
    EXPECT_TRUE(sim.frame().particles.empty() && sim.frame().stars.empty());
    const double before = sim.time();
    sim.step(0.05);
    EXPECT_EQ(sim.time(), before);
    sim.setReducedMotion(false);
    EXPECT_FALSE(sim.frame().particles.empty());
    BackgroundSimulation none(*findBackground("golden-river"), 0, 5);
    none.step(0.05);
    EXPECT_TRUE(none.frame().particles.empty());
}
