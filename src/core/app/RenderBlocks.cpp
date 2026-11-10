#include "app/RenderBlocks.hpp"

#include <cctype>

#include "common/Strings.hpp"

namespace eden {

namespace {

RenderBlock block(BlockKind kind, std::string text, std::string detail = "", int level = 0) {
    RenderBlock out;
    out.kind = kind;
    out.text = std::move(text);
    out.detail = std::move(detail);
    out.level = level;
    return out;
}

std::string stringOf(const Json& value) {
    if (value.isString()) return value.asString();
    if (value.isNumber() || value.isBool()) return value.dump();
    return std::string();
}

void appendBulletList(std::vector<RenderBlock>& out, const Json& list) {
    for (const Json& item : list.items()) {
        const std::string text = stringOf(item);
        if (!text.empty()) out.push_back(block(BlockKind::Bullet, text));
    }
}

void genericObject(std::vector<RenderBlock>& out, const Json& item) {
    std::string first;
    std::string second;
    std::string third;
    for (const auto& [key, value] : item.members()) {
        const std::string text = stringOf(value);
        if (text.empty()) continue;
        if (first.empty()) first = text;
        else if (second.empty()) second = text;
        else if (third.empty()) third = text;
    }
    if (!first.empty()) out.push_back(block(BlockKind::KeyValue, first, second + (third.empty() ? "" : "\n" + third)));
}

std::string headingFor(const std::string& key) {
    std::string title = key;
    for (char& c : title) {
        if (c == '_') c = ' ';
    }
    if (!title.empty()) title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
    return title;
}

}  // namespace

std::vector<RenderBlock> markdownToBlocks(const std::string& markdown) {
    std::vector<RenderBlock> out;
    std::string paragraph;
    auto flush = [&]() {
        if (!trim(paragraph).empty()) out.push_back(block(BlockKind::Paragraph, trim(paragraph)));
        paragraph.clear();
    };
    bool inCode = false;
    std::string code;
    for (const std::string& rawLine : splitLines(markdown)) {
        const std::string line = trim(rawLine);
        if (startsWith(line, "```")) {
            if (inCode) {
                out.push_back(block(BlockKind::Code, code));
                code.clear();
                inCode = false;
            } else {
                flush();
                inCode = true;
            }
            continue;
        }
        if (inCode) {
            if (!code.empty()) code.push_back('\n');
            code += rawLine;
            continue;
        }
        if (line.empty()) {
            flush();
            continue;
        }
        if (line[0] == '#') {
            flush();
            std::size_t level = 0;
            while (level < line.size() && line[level] == '#') ++level;
            out.push_back(block(BlockKind::Heading, trim(line.substr(level)), "", static_cast<int>(level)));
        } else if (startsWith(line, "- ") || startsWith(line, "* ") || startsWith(line, "\xE2\x80\xA2 ")) {
            flush();
            out.push_back(block(BlockKind::Bullet, trim(line.substr(line[0] == '-' || line[0] == '*' ? 2 : 4))));
        } else if (std::isdigit(static_cast<unsigned char>(line[0])) && line.find(". ") != std::string::npos &&
                   line.find(". ") <= 3) {
            flush();
            out.push_back(block(BlockKind::Numbered, trim(line.substr(line.find(". ") + 2))));
        } else if (line[0] == '>') {
            flush();
            out.push_back(block(BlockKind::Quote, trim(line.substr(1))));
        } else {
            if (!paragraph.empty()) paragraph.push_back(' ');
            paragraph += line;
        }
    }
    if (inCode && !code.empty()) out.push_back(block(BlockKind::Code, code));
    flush();
    return out;
}

std::vector<RenderBlock> renderFunctionResult(const std::string& functionId, const std::string& output,
                                              const Json& data, const std::string& text) {
    std::vector<RenderBlock> out;
    if (data.isNull()) return markdownToBlocks(text);

    if (output == "mermaid") {
        out.push_back(block(BlockKind::Code, data.isString() ? data.asString() : text, "mermaid"));
        return out;
    }
    if (output == "list" && data.isArray()) {
        appendBulletList(out, data);
        return out;
    }
    if (functionId == "reading_level" && data.isObject()) {
        out.push_back(block(BlockKind::Badge, data.getString("difficulty"), "grade " + stringOf(data.get("grade_level"))));
        appendBulletList(out, data.get("reasons"));
        return out;
    }
    if (functionId == "argument_structure" && data.isObject()) {
        out.push_back(block(BlockKind::Heading, "Conclusion", "", 3));
        out.push_back(block(BlockKind::Paragraph, data.getString("conclusion")));
        out.push_back(block(BlockKind::Heading, "Premises", "", 3));
        appendBulletList(out, data.get("premises"));
        if (!data.get("implicit_steps").empty()) {
            out.push_back(block(BlockKind::Heading, "Implicit steps", "", 3));
            appendBulletList(out, data.get("implicit_steps"));
        }
        out.push_back(block(BlockKind::Badge, data.getString("type")));
        return out;
    }
    if (functionId == "evidence.strength" && data.isObject()) {
        out.push_back(block(BlockKind::Badge, data.getString("rating"), "score " + stringOf(data.get("score"))));
        out.push_back(block(BlockKind::Paragraph, data.getString("justification")));
        if (!data.get("missing_evidence").empty()) {
            out.push_back(block(BlockKind::Heading, "Missing evidence", "", 3));
            appendBulletList(out, data.get("missing_evidence"));
        }
        return out;
    }
    if (functionId == "expand.papers" && data.isObject()) {
        for (const char* key : {"topics", "search_phrases", "fields"}) {
            if (data.get(key).empty()) continue;
            out.push_back(block(BlockKind::Heading, headingFor(key), "", 3));
            appendBulletList(out, data.get(key));
        }
        return out;
    }
    if ((functionId == "expand.news" || functionId == "expand.videos" || functionId == "expand.podcasts") && data.isObject()) {
        for (const auto& [key, value] : data.members()) {
            if (!value.isArray() || value.empty()) continue;
            out.push_back(block(BlockKind::Heading, headingFor(key), "", 3));
            appendBulletList(out, value);
        }
        return out;
    }
    if (!data.isArray()) return markdownToBlocks(text);

    for (const Json& item : data.items()) {
        if (!item.isObject()) {
            const std::string line = stringOf(item);
            if (!line.empty()) out.push_back(block(BlockKind::Bullet, line));
            continue;
        }
        if (functionId == "glossary") {
            out.push_back(block(BlockKind::KeyValue, item.getString("term"), item.getString("definition")));
        } else if (functionId == "study_questions") {
            out.push_back(block(BlockKind::Numbered, item.getString("question"), item.getString("answer_hint"),
                                0));
        } else if (functionId == "quotes.key") {
            const bool verified = item.getBool("verified");
            out.push_back(block(BlockKind::Quote, item.getString("quote"),
                                item.getString("why") + (verified ? "" : "\nNot found verbatim in the passage"), verified ? 1 : 0));
        } else if (functionId == "paraphrase.side_by_side") {
            out.push_back(block(BlockKind::KeyValue, item.getString("original"), item.getString("paraphrase")));
        } else if (functionId == "crossref.document") {
            out.push_back(block(BlockKind::KeyValue, item.getString("relation"), item.getString("note")));
        } else if (functionId == "rhetoric") {
            out.push_back(block(BlockKind::KeyValue, item.getString("device") + ": " + item.getString("quote"),
                                item.getString("effect")));
        } else if (functionId == "expand.timeline") {
            out.push_back(block(BlockKind::KeyValue, item.getString("when"), item.getString("event")));
        } else if (functionId == "expand.entities") {
            out.push_back(block(BlockKind::KeyValue, item.getString("name") + " (" + item.getString("kind") + ")",
                                item.getString("context")));
        } else if (functionId == "expand.further_reading") {
            out.push_back(block(BlockKind::KeyValue, item.getString("title") + " \xE2\x80\x94 " + item.getString("author"),
                                item.getString("why")));
        } else if (functionId == "expand.geography") {
            out.push_back(block(BlockKind::KeyValue, item.getString("place") + ", " + item.getString("region"),
                                item.getString("significance")));
        } else if (functionId == "expand.idea_timeline") {
            out.push_back(block(BlockKind::KeyValue, item.getString("period"), item.getString("development")));
        } else if (functionId == "expand.futures") {
            out.push_back(block(BlockKind::Heading, headingFor(item.getString("scenario")), "", 3));
            out.push_back(block(BlockKind::Paragraph, item.getString("description")));
            appendBulletList(out, item.get("assumptions"));
        } else if (functionId == "assist.safety_bias") {
            out.push_back(block(BlockKind::KeyValue, item.getString("issue"), item.getString("note") + "\n\"" + item.getString("quote") + "\""));
        } else if (functionId == "visual.timeline") {
            out.push_back(block(BlockKind::KeyValue, item.getString("label"), item.getString("detail")));
        } else if (functionId == "visual.set_plan") {
            out.push_back(block(BlockKind::KeyValue, item.getString("scene"), item.getString("prompt")));
        } else if (functionId == "output.flashcards") {
            out.push_back(block(BlockKind::KeyValue, item.getString("front"), item.getString("back")));
        } else if (functionId == "output.slides") {
            out.push_back(block(BlockKind::Heading, item.getString("title"), "", 3));
            appendBulletList(out, item.get("bullets"));
            if (!item.getString("notes").empty()) out.push_back(block(BlockKind::Quote, item.getString("notes")));
        } else {
            genericObject(out, item);
        }
    }
    return out;
}

std::string inlineToHtml(const std::string& text) {
    std::string escaped = escapeHtml(text);
    std::string out;
    std::size_t i = 0;
    bool bold = false;
    bool code = false;
    while (i < escaped.size()) {
        if (!code && escaped.compare(i, 2, "**") == 0) {
            out += bold ? "</b>" : "<b>";
            bold = !bold;
            i += 2;
        } else if (escaped[i] == '`') {
            out += code ? "</code>" : "<code>";
            code = !code;
            ++i;
        } else {
            out.push_back(escaped[i++]);
        }
    }
    if (bold) out += "</b>";
    if (code) out += "</code>";
    return out;
}

std::string blocksToHtml(const std::vector<RenderBlock>& blocks, const Palette& palette) {
    std::string html;
    enum class Open { None, Bullets, Numbers } open = Open::None;
    auto close = [&]() {
        if (open == Open::Bullets) html += "</ul>";
        if (open == Open::Numbers) html += "</ol>";
        open = Open::None;
    };
    for (const RenderBlock& item : blocks) {
        if (item.kind != BlockKind::Bullet && open == Open::Bullets) close();
        if (item.kind != BlockKind::Numbered && open == Open::Numbers) close();
        switch (item.kind) {
            case BlockKind::Heading:
                html += "<h3 style='color:" + palette.gold + ";margin-bottom:2px'>" + inlineToHtml(item.text) + "</h3>";
                break;
            case BlockKind::Paragraph:
                html += "<p>" + inlineToHtml(item.text) + "</p>";
                break;
            case BlockKind::Bullet:
                if (open != Open::Bullets) {
                    close();
                    html += "<ul>";
                    open = Open::Bullets;
                }
                html += "<li>" + inlineToHtml(item.text) + "</li>";
                break;
            case BlockKind::Numbered:
                if (open != Open::Numbers) {
                    close();
                    html += "<ol>";
                    open = Open::Numbers;
                }
                html += "<li>" + inlineToHtml(item.text);
                if (!item.detail.empty()) html += "<br><i style='color:" + palette.mutedText + "'>Hint: " + inlineToHtml(item.detail) + "</i>";
                html += "</li>";
                break;
            case BlockKind::Quote: {
                const std::string border = item.level == 0 && !item.detail.empty() ? palette.danger : palette.gold;
                html += "<blockquote style='border-left:3px solid " + border + ";margin-left:4px;padding-left:10px'>" +
                        inlineToHtml(item.text);
                if (!item.detail.empty()) {
                    html += "<br><span style='color:" + palette.mutedText + "'>" + replaceAll(inlineToHtml(item.detail), "\n", "<br>") + "</span>";
                }
                html += "</blockquote>";
                break;
            }
            case BlockKind::KeyValue:
                html += "<p><b style='color:" + palette.gold + "'>" + inlineToHtml(item.text) + "</b>";
                if (!item.detail.empty()) html += "<br>" + replaceAll(inlineToHtml(item.detail), "\n", "<br>");
                html += "</p>";
                break;
            case BlockKind::Code:
                html += "<pre style='background:" + palette.surfaceAlt + ";padding:8px'>" + escapeHtml(item.text) + "</pre>";
                break;
            case BlockKind::Badge:
                html += "<p><span style='background:" + palette.selection + ";color:" + palette.gold + ";padding:2px 10px'>&nbsp;" +
                        escapeHtml(item.text) + "&nbsp;</span>";
                if (!item.detail.empty()) html += " <span style='color:" + palette.mutedText + "'>" + escapeHtml(item.detail) + "</span>";
                html += "</p>";
                break;
        }
    }
    close();
    return html;
}

}  // namespace eden
