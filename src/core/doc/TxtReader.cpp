#include "doc/TxtReader.hpp"

#include <cctype>

#include "common/Strings.hpp"
#include "doc/TextUtil.hpp"

namespace eden {

namespace {

bool isRomanOrNumber(const std::string& token) {
    if (token.empty()) return false;
    bool digits = true;
    bool roman = true;
    for (char c : token) {
        if (!std::isdigit(static_cast<unsigned char>(c))) digits = false;
        const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (u != 'I' && u != 'V' && u != 'X' && u != 'L' && u != 'C' && u != 'D' && u != 'M') roman = false;
    }
    return digits || roman;
}

int headingLevelOf(const std::string& line, const std::string& previous, const std::string& next) {
    if (line.empty() || line.size() > 90) return 0;
    if (line[0] == '#') {
        int level = 0;
        while (level < static_cast<int>(line.size()) && line[static_cast<std::size_t>(level)] == '#') ++level;
        if (level <= 6 && static_cast<std::size_t>(level) < line.size() && line[static_cast<std::size_t>(level)] == ' ') {
            return level;
        }
        return 0;
    }
    const std::vector<std::string> words = split(collapseWhitespace(line), ' ', false);
    if (words.empty()) return 0;
    const std::string first = toLowerAscii(words[0]);
    const bool structural = first == "chapter" || first == "book" || first == "part" || first == "section" ||
                            first == "canto" || first == "act" || first == "psalm";
    if (structural && words.size() >= 2 && isRomanOrNumber(words[1].substr(0, words[1].find_first_of(".:")))) {
        return 1;
    }
    if (previous.empty() && next.empty() && line.size() <= 60) {
        bool hasLetter = false;
        bool allUpper = true;
        for (char c : line) {
            if (std::isalpha(static_cast<unsigned char>(c))) {
                hasLetter = true;
                if (!std::isupper(static_cast<unsigned char>(c))) allUpper = false;
            }
        }
        if (hasLetter && allUpper && words.size() <= 8) return 2;
    }
    return 0;
}

}  // namespace

Result<ExtractedDocument> readTxtDocument(const Bytes& data, const std::string& fileName) {
    if (data.empty()) return makeError(ErrorCode::InvalidArgument, "text file is empty");
    ExtractedDocument doc;
    doc.type = FileType::Txt;
    std::string encoding;
    std::string decoded = decodeToUtf8(data, &encoding);
    if (encoding == "windows-1252") doc.warnings.push_back("file was not valid UTF-8; decoded as Windows-1252");
    std::string text = normalizeNewlines(decoded);
    text = stripControlCharacters(text);
    text = trimLineEnds(text);
    text = reflowHardWrapped(text);
    text = collapseBlankLines(text, 1);
    doc.text = trim(text);
    if (doc.text.empty()) return makeError(ErrorCode::InvalidArgument, "text file contains no readable text");

    const std::vector<std::string> lines = splitLines(doc.text);
    std::int64_t offset = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string previous = i > 0 ? lines[i - 1] : std::string();
        const std::string next = i + 1 < lines.size() ? lines[i + 1] : std::string();
        const int level = headingLevelOf(lines[i], previous, next);
        if (level > 0) {
            ChapterMark mark;
            mark.title = level > 0 && lines[i][0] == '#' ? trim(lines[i].substr(lines[i].find(' '))) : trim(lines[i]);
            mark.offset = offset;
            mark.level = level;
            doc.chapters.push_back(std::move(mark));
        }
        offset += static_cast<std::int64_t>(lines[i].size()) + 1;
    }
    for (const std::string& line : lines) {
        const std::string candidate = trim(line);
        if (!candidate.empty() && candidate.size() <= 100) {
            doc.title = candidate[0] == '#' ? trim(candidate.substr(candidate.find_first_not_of("# "))) : candidate;
        }
        break;
    }
    if (doc.title.empty()) doc.title = fileStem(fileName);
    paginateByLength(doc.text, 0, doc.charCount(), 3000, 0, doc.pages);
    return doc;
}

}  // namespace eden
