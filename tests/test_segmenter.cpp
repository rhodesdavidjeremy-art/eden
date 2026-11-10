#include "TestFramework.hpp"
#include "doc/PackSegmenter.hpp"
#include "doc/TextUtil.hpp"

using namespace eden;

namespace {

std::vector<std::string> sentencesOf(const std::string& text) {
    std::vector<std::string> out;
    for (const TextSpan& span : splitSentences(text, 0, static_cast<std::int64_t>(text.size()))) {
        out.push_back(text.substr(static_cast<std::size_t>(span.start), static_cast<std::size_t>(span.end - span.start)));
    }
    return out;
}

ExtractedDocument makeDoc(const std::string& text) {
    ExtractedDocument doc;
    doc.text = text;
    paginateByLength(doc.text, 0, doc.charCount(), 800, 0, doc.pages);
    return doc;
}

}  // namespace

EDEN_TEST(sentences_respect_abbreviations_and_initials) {
    const auto parts = sentencesOf("Dr. Smith met Mr. Jones at 3.5 p.m. on Tuesday. J. R. R. Tolkien wrote it. Really? Yes!");
    EXPECT_EQ(parts.size(), static_cast<std::size_t>(4));
    EXPECT_EQ(parts[0], std::string("Dr. Smith met Mr. Jones at 3.5 p.m. on Tuesday."));
    EXPECT_EQ(parts[1], std::string("J. R. R. Tolkien wrote it."));
    EXPECT_EQ(parts[2], std::string("Really?"));
}

EDEN_TEST(sentences_handle_quotes_and_lowercase_continuations) {
    const auto parts = sentencesOf("He said \"stop.\" Then he left. e.g. this stays together. Done.");
    EXPECT_EQ(parts.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(parts[0], std::string("He said \"stop.\""));
}

EDEN_TEST(sentences_split_on_line_breaks) {
    const auto parts = sentencesOf("In the beginning\nwas the Word\nand the Word was with God.");
    EXPECT_EQ(parts.size(), static_cast<std::size_t>(3));
}

EDEN_TEST(paragraph_splitting_trims_whitespace) {
    const std::string text = "\n\nfirst para  \n\n\n\nsecond para\nstill second\n\n";
    const auto spans = splitParagraphs(text);
    EXPECT_EQ(spans.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(text.substr(static_cast<std::size_t>(spans[0].start), static_cast<std::size_t>(spans[0].end - spans[0].start)),
              std::string("first para"));
}

EDEN_TEST(balanced_segmentation_produces_ordered_non_overlapping_packs) {
    std::string text = "THE BEGINNING\n\n";
    for (int paragraph = 0; paragraph < 6; ++paragraph) {
        for (int sentence = 0; sentence < 6; ++sentence) {
            text += "This is sentence " + std::to_string(sentence) + " of paragraph " + std::to_string(paragraph) + ". ";
        }
        text += "\n\n";
    }
    ExtractedDocument doc = makeDoc(text);
    SegmenterOptions options;
    options.targetChars = 160;
    options.maxChars = 320;
    const auto packs = segmentDocument(doc, options);
    EXPECT_TRUE(packs.size() > 6);
    EXPECT_TRUE(packs.front().isHeading);
    std::int64_t previousEnd = 0;
    for (const PackDraft& pack : packs) {
        EXPECT_TRUE(pack.start >= previousEnd);
        EXPECT_TRUE(pack.end > pack.start);
        EXPECT_EQ(pack.text, doc.text.substr(static_cast<std::size_t>(pack.start), static_cast<std::size_t>(pack.end - pack.start)));
        EXPECT_TRUE(pack.text.size() <= options.maxChars + 80);
        EXPECT_TRUE(pack.significance > 0.0 && pack.significance <= 1.0);
        previousEnd = pack.end;
    }
}

EDEN_TEST(sentence_mode_merges_tiny_sentences) {
    ExtractedDocument doc = makeDoc("Yes. No. Maybe so, said the long winded narrator who never stops talking about rivers.");
    SegmenterOptions options;
    options.mode = SegmentMode::Sentence;
    options.minChars = 40;
    const auto packs = segmentDocument(doc, options);
    EXPECT_EQ(packs.size(), static_cast<std::size_t>(1));
}

EDEN_TEST(paragraph_mode_keeps_short_paragraphs_whole) {
    ExtractedDocument doc = makeDoc("One short paragraph here. With two sentences.\n\nAnother paragraph follows. It also has two.");
    SegmenterOptions options;
    options.mode = SegmentMode::Paragraph;
    const auto packs = segmentDocument(doc, options);
    EXPECT_EQ(packs.size(), static_cast<std::size_t>(2));
}

EDEN_TEST(chapter_marks_become_heading_packs_and_can_be_skipped) {
    ExtractedDocument doc = makeDoc("Chapter One\n\nThe story begins on a quiet morning in the old town.");
    doc.chapters.push_back({"Chapter One", 0, 1});
    auto packs = segmentDocument(doc);
    EXPECT_EQ(packs.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(packs[0].isHeading);
    SegmenterOptions options;
    options.headingsAsPacks = false;
    EXPECT_EQ(segmentDocument(doc, options).size(), static_cast<std::size_t>(1));
}

EDEN_TEST(max_packs_keeps_most_significant) {
    std::string text;
    for (int i = 0; i < 20; ++i) text += "Plain filler sentence number " + std::to_string(i) + " goes on and on for a while.\n\n";
    text += "Therefore the total must equal 42 because the result is essential and defined.\n\n";
    ExtractedDocument doc = makeDoc(text);
    SegmenterOptions options;
    options.maxPacks = 3;
    const auto packs = segmentDocument(doc, options);
    EXPECT_EQ(packs.size(), static_cast<std::size_t>(3));
    bool kept = false;
    for (const PackDraft& pack : packs) kept = kept || pack.text.find("Therefore") != std::string::npos;
    EXPECT_TRUE(kept);
}

EDEN_TEST(selection_snaps_to_sentence_boundaries) {
    const std::string text = "First sentence here. Second sentence is longer. Third one ends.";
    ExtractedDocument doc = makeDoc(text);
    const std::int64_t from = static_cast<std::int64_t>(text.find("sentence is"));
    const PackDraft draft = draftFromSelection(doc, from, from + 4, true);
    EXPECT_EQ(draft.text, std::string("Second sentence is longer."));
    const PackDraft exact = draftFromSelection(doc, from, from + 8, false);
    EXPECT_EQ(exact.text, std::string("sentence"));
    const PackDraft clamped = draftFromSelection(doc, -5, 9999, false);
    EXPECT_EQ(clamped.text, text);
}
