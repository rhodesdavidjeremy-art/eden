#include "doc/PackSegmenter.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "common/Strings.hpp"

namespace eden {

namespace {

bool isSpaceChar(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool isClosing(const std::string& text, std::size_t i, std::size_t& length) {
    const char c = text[i];
    if (c == '"' || c == '\'' || c == ')' || c == ']' || c == '*') {
        length = 1;
        return true;
    }
    if (text.compare(i, 3, "\xE2\x80\x9D") == 0 || text.compare(i, 3, "\xE2\x80\x99") == 0) {
        length = 3;
        return true;
    }
    return false;
}

bool isAbbreviation(const std::string& word) {
    static const std::set<std::string> known = {
        "mr", "mrs", "ms", "dr", "prof", "sr", "jr", "st", "mt", "vs", "etc", "e.g", "i.e", "cf", "fig", "no",
        "vol", "pp", "ch", "sec", "al", "inc", "ltd", "co", "approx", "dept", "est", "ed", "eds", "gen", "col",
        "lt", "capt", "sgt", "rev", "hon", "gov", "sen", "rep", "jan", "feb", "mar", "apr", "jun", "jul", "aug",
        "sep", "sept", "oct", "nov", "dec", "ca", "viz", "ibid", "op", "cit", "p", "ff"};
    return known.count(toLowerAscii(word)) > 0;
}

std::string wordBefore(const std::string& text, std::size_t dot, std::size_t lineStart) {
    std::size_t begin = dot;
    while (begin > lineStart) {
        const char c = text[begin - 1];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.') {
            --begin;
        } else {
            break;
        }
    }
    return text.substr(begin, dot - begin);
}

bool startsSentence(const std::string& text, std::size_t i, char terminator) {
    if (i >= text.size()) return true;
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c >= 0x80) return true;
    if (std::isupper(c) || std::isdigit(c) || c == '"' || c == '\'' || c == '(' || c == '[') return true;
    if (std::islower(c)) return false;
    return terminator != '.';
}

void splitLineSentences(const std::string& text, std::size_t lineStart, std::size_t lineEnd, std::vector<TextSpan>& out) {
    std::size_t sentenceStart = lineStart;
    std::size_t i = lineStart;
    auto emit = [&](std::size_t end) {
        std::size_t s = sentenceStart;
        std::size_t e = end;
        while (s < e && isSpaceChar(text[s])) ++s;
        while (e > s && isSpaceChar(text[e - 1])) --e;
        if (e > s) out.push_back({static_cast<std::int64_t>(s), static_cast<std::int64_t>(e)});
    };
    while (i < lineEnd) {
        const char c = text[i];
        const bool ellipsis = text.compare(i, 3, "\xE2\x80\xA6") == 0;
        if (c != '.' && c != '!' && c != '?' && !ellipsis) {
            ++i;
            continue;
        }
        std::size_t j = i + (ellipsis ? 3 : 1);
        while (j < lineEnd && (text[j] == '.' || text[j] == '!' || text[j] == '?')) ++j;
        std::size_t closing = 0;
        while (j < lineEnd && isClosing(text, j, closing)) j += closing;
        if (j < lineEnd && !isSpaceChar(text[j])) {
            i = j;
            continue;
        }
        std::size_t next = j;
        while (next < lineEnd && isSpaceChar(text[next])) ++next;
        bool boundary = next >= lineEnd || startsSentence(text, next, c);
        if (boundary && c == '.' && !ellipsis) {
            const std::string word = wordBefore(text, i, lineStart);
            if (isAbbreviation(word)) {
                boundary = false;
            } else if (word.size() == 1 && std::isupper(static_cast<unsigned char>(word[0])) && next < lineEnd &&
                       std::isupper(static_cast<unsigned char>(text[next]))) {
                boundary = false;
            }
        }
        if (boundary) {
            emit(j);
            sentenceStart = next;
        }
        i = j;
    }
    emit(lineEnd);
}

bool isAllCapsLine(const std::string& line) {
    bool letters = false;
    for (char c : line) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            letters = true;
            if (!std::isupper(static_cast<unsigned char>(c))) return false;
        }
    }
    return letters;
}

struct Unit {
    std::int64_t start = 0;
    std::int64_t end = 0;
    bool heading = false;
    bool firstInParagraph = false;
    std::int64_t paragraph = 0;
};

}  // namespace

std::vector<TextSpan> splitSentences(const std::string& text, std::int64_t begin, std::int64_t end) {
    std::vector<TextSpan> out;
    std::size_t position = static_cast<std::size_t>(std::max<std::int64_t>(0, begin));
    const std::size_t limit = static_cast<std::size_t>(std::min<std::int64_t>(end, static_cast<std::int64_t>(text.size())));
    while (position < limit) {
        std::size_t lineEnd = text.find('\n', position);
        if (lineEnd == std::string::npos || lineEnd > limit) lineEnd = limit;
        splitLineSentences(text, position, lineEnd, out);
        position = lineEnd + 1;
    }
    return out;
}

std::vector<TextSpan> splitParagraphs(const std::string& text) {
    std::vector<TextSpan> out;
    std::size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() && isSpaceChar(text[position])) ++position;
        if (position >= text.size()) break;
        std::size_t end = text.find("\n\n", position);
        if (end == std::string::npos) end = text.size();
        std::size_t trimmedEnd = end;
        while (trimmedEnd > position && isSpaceChar(text[trimmedEnd - 1])) --trimmedEnd;
        if (trimmedEnd > position) out.push_back({static_cast<std::int64_t>(position), static_cast<std::int64_t>(trimmedEnd)});
        position = end + 2;
    }
    return out;
}

double scoreSignificance(const std::string& text, bool firstInParagraph, std::size_t targetChars) {
    static const char* cues[] = {"therefore", "thus", "because", "however", "must", "shall", "should", "means",
                                 "defined", "important", "conclude", "essential", "primary", "result", "hence"};
    double score = 0.3;
    score += 0.2 * std::min(1.0, static_cast<double>(text.size()) / static_cast<double>(std::max<std::size_t>(1, targetChars)));
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            score += 0.1;
            break;
        }
    }
    const std::string lowered = toLowerAscii(text);
    double cueScore = 0;
    for (const char* cue : cues) {
        if (lowered.find(cue) != std::string::npos) cueScore += 0.1;
    }
    score += std::min(0.3, cueScore);
    if (firstInParagraph) score += 0.1;
    return std::min(1.0, score);
}

std::vector<PackDraft> segmentDocument(const ExtractedDocument& doc, const SegmenterOptions& options) {
    std::vector<PackDraft> drafts;
    const std::string& text = doc.text;
    std::vector<Unit> units;
    std::int64_t paragraphNumber = 0;
    for (const TextSpan& paragraph : splitParagraphs(text)) {
        bool heading = false;
        for (const ChapterMark& mark : doc.chapters) {
            if (mark.offset >= paragraph.start && mark.offset < paragraph.end &&
                paragraph.end - paragraph.start <= 200) {
                heading = true;
                break;
            }
        }
        const std::string body = text.substr(static_cast<std::size_t>(paragraph.start),
                                             static_cast<std::size_t>(paragraph.end - paragraph.start));
        if (!heading && body.size() <= 60 && body.find('\n') == std::string::npos && isAllCapsLine(body) &&
            wordCount(body) <= 8) {
            heading = true;
        }
        if (heading) {
            units.push_back({paragraph.start, paragraph.end, true, true, paragraphNumber++});
            continue;
        }
        bool first = true;
        if (options.mode == SegmentMode::Paragraph && body.size() <= options.maxChars) {
            units.push_back({paragraph.start, paragraph.end, false, true, paragraphNumber});
        } else {
            for (const TextSpan& sentence : splitSentences(text, paragraph.start, paragraph.end)) {
                units.push_back({sentence.start, sentence.end, false, first, paragraphNumber});
                first = false;
            }
        }
        ++paragraphNumber;
    }

    auto emit = [&](std::int64_t start, std::int64_t end, bool heading, bool firstInParagraph) {
        PackDraft draft;
        draft.start = start;
        draft.end = end;
        draft.text = text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
        draft.pageIndex = doc.pageIndexAt(start);
        draft.isHeading = heading;
        draft.significance = heading ? 0.9 : scoreSignificance(draft.text, firstInParagraph, options.targetChars);
        drafts.push_back(std::move(draft));
    };

    std::size_t i = 0;
    while (i < units.size()) {
        const Unit& unit = units[i];
        if (unit.heading) {
            if (options.headingsAsPacks) emit(unit.start, unit.end, true, true);
            ++i;
            continue;
        }
        if (options.mode == SegmentMode::Sentence) {
            std::int64_t end = unit.end;
            std::size_t consumed = 1;
            while (static_cast<std::size_t>(end - unit.start) < options.minChars && i + consumed < units.size() &&
                   units[i + consumed].paragraph == unit.paragraph && !units[i + consumed].heading) {
                end = units[i + consumed].end;
                ++consumed;
            }
            emit(unit.start, end, false, unit.firstInParagraph);
            i += consumed;
            continue;
        }
        std::int64_t start = unit.start;
        std::int64_t end = unit.end;
        int count = 1;
        std::size_t consumed = 1;
        while (i + consumed < units.size()) {
            const Unit& next = units[i + consumed];
            if (next.heading || next.paragraph != unit.paragraph) break;
            const std::size_t length = static_cast<std::size_t>(end - start);
            const std::size_t combined = static_cast<std::size_t>(next.end - start);
            if (options.mode == SegmentMode::Paragraph) {
                if (combined > options.maxChars && length >= options.minChars) break;
            } else {
                if (length >= options.targetChars || count >= options.maxSentences) break;
                if (combined > options.maxChars && length >= options.minChars) break;
            }
            end = next.end;
            ++count;
            ++consumed;
        }
        emit(start, end, false, unit.firstInParagraph);
        i += consumed;
    }

    std::vector<PackDraft> merged;
    for (PackDraft& draft : drafts) {
        const bool tiny = !draft.isHeading && draft.text.size() < options.minChars;
        if (tiny && !merged.empty() && !merged.back().isHeading &&
            static_cast<std::size_t>(draft.end - merged.back().start) <= options.maxChars + options.maxChars / 4 &&
            text.compare(static_cast<std::size_t>(merged.back().end), 2, "\n\n") != 0 &&
            draft.start - merged.back().end < 4) {
            merged.back().end = draft.end;
            merged.back().text = text.substr(static_cast<std::size_t>(merged.back().start),
                                             static_cast<std::size_t>(merged.back().end - merged.back().start));
            merged.back().significance = std::max(merged.back().significance, draft.significance);
            continue;
        }
        merged.push_back(std::move(draft));
    }
    if (options.maxPacks > 0 && merged.size() > options.maxPacks) {
        std::vector<std::size_t> order(merged.size());
        for (std::size_t k = 0; k < order.size(); ++k) order[k] = k;
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return merged[a].significance > merged[b].significance; });
        std::set<std::size_t> keep(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(options.maxPacks));
        std::vector<PackDraft> limited;
        for (std::size_t k = 0; k < merged.size(); ++k) {
            if (keep.count(k)) limited.push_back(std::move(merged[k]));
        }
        return limited;
    }
    return merged;
}

PackDraft draftFromSelection(const ExtractedDocument& doc, std::int64_t start, std::int64_t end, bool snapToSentences) {
    const std::int64_t total = doc.charCount();
    start = std::max<std::int64_t>(0, std::min(start, total));
    end = std::max(start, std::min(end, total));
    PackDraft draft;
    if (snapToSentences && total > 0) {
        const std::size_t regionStartHint = start == 0 ? 0 : doc.text.rfind("\n\n", static_cast<std::size_t>(start - 1));
        const std::size_t regionStart = regionStartHint == std::string::npos ? 0 : regionStartHint;
        std::size_t regionEnd = doc.text.find("\n\n", static_cast<std::size_t>(end));
        if (regionEnd == std::string::npos) regionEnd = doc.text.size();
        std::int64_t snappedStart = start;
        std::int64_t snappedEnd = end;
        bool first = true;
        for (const TextSpan& span : splitSentences(doc.text, static_cast<std::int64_t>(regionStart),
                                                   static_cast<std::int64_t>(regionEnd))) {
            const bool overlaps = span.end > start && span.start < std::max(end, start + 1);
            if (!overlaps) continue;
            if (first) {
                snappedStart = span.start;
                first = false;
            }
            snappedEnd = std::max(snappedEnd, span.end);
        }
        if (!first) {
            start = snappedStart;
            end = snappedEnd;
        }
    }
    while (start < end && isSpaceChar(doc.text[static_cast<std::size_t>(start)])) ++start;
    while (end > start && isSpaceChar(doc.text[static_cast<std::size_t>(end - 1)])) --end;
    draft.start = start;
    draft.end = end;
    draft.text = doc.text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
    draft.pageIndex = doc.pageIndexAt(start);
    draft.significance = scoreSignificance(draft.text, true, 320);
    return draft;
}

}  // namespace eden
