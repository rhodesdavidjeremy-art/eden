#pragma once

#include <string>
#include <vector>

#include "doc/ExtractedDocument.hpp"

namespace eden {

enum class SegmentMode { Sentence, Balanced, Paragraph };

struct SegmenterOptions {
    SegmentMode mode = SegmentMode::Balanced;
    std::size_t minChars = 60;
    std::size_t targetChars = 320;
    std::size_t maxChars = 700;
    int maxSentences = 7;
    bool headingsAsPacks = true;
    std::size_t maxPacks = 0;
};

struct TextSpan {
    std::int64_t start = 0;
    std::int64_t end = 0;
};

struct PackDraft {
    std::int64_t start = 0;
    std::int64_t end = 0;
    std::string text;
    std::int64_t pageIndex = 0;
    double significance = 0.0;
    bool isHeading = false;
};

std::vector<TextSpan> splitSentences(const std::string& text, std::int64_t begin, std::int64_t end);
std::vector<TextSpan> splitParagraphs(const std::string& text);
std::vector<PackDraft> segmentDocument(const ExtractedDocument& doc, const SegmenterOptions& options = {});
PackDraft draftFromSelection(const ExtractedDocument& doc, std::int64_t start, std::int64_t end, bool snapToSentences = true);
double scoreSignificance(const std::string& text, bool firstInParagraph, std::size_t targetChars);

}  // namespace eden
