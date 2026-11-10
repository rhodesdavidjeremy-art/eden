#include "doc/PdfText.hpp"

#include <algorithm>
#include <cmath>

#include "common/Strings.hpp"
#include "doc/TextUtil.hpp"

namespace eden {

namespace {

struct Matrix {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
};

Matrix multiply(const Matrix& m, const Matrix& n) {
    Matrix r;
    r.a = m.a * n.a + m.b * n.c;
    r.b = m.a * n.b + m.b * n.d;
    r.c = m.c * n.a + m.d * n.c;
    r.d = m.c * n.b + m.d * n.d;
    r.e = m.e * n.a + m.f * n.c + n.e;
    r.f = m.e * n.b + m.f * n.d + n.f;
    return r;
}

Matrix translation(double tx, double ty) {
    Matrix m;
    m.e = tx;
    m.f = ty;
    return m;
}

struct GraphicsState {
    Matrix ctm;
    double charSpacing = 0;
    double wordSpacing = 0;
    double horizontalScale = 1;
    double leading = 0;
    double rise = 0;
    double fontSize = 0;
    std::shared_ptr<PdfFont> font;
};

struct TextRun {
    double x = 0;
    double y = 0;
    double endX = 0;
    double fontSize = 0;
    std::string text;
};

class Interpreter {
public:
    Interpreter(const PdfDocument& doc, PdfFontCache& fonts, std::vector<std::string>& warnings)
        : doc_(doc), fonts_(fonts), warnings_(warnings) {}

    std::vector<TextRun> runs;

    void run(std::string_view content, const PdfObject& resources, int depth) {
        if (depth > 8) return;
        const PdfObject resolvedResources = doc_.resolve(resources);
        PdfLexer lexer(content, 0);
        std::vector<PdfObject> operands;
        while (true) {
            lexer.skipSpace();
            if (lexer.atEnd()) break;
            const char c = lexer.peek();
            const std::size_t before = lexer.position();
            const bool startsOperand = c == '/' || c == '(' || c == '<' || c == '[' || (c >= '0' && c <= '9') ||
                                       c == '+' || c == '-' || c == '.';
            if (startsOperand) {
                PdfObject operand;
                if (lexer.parseObject(operand, false)) {
                    operands.push_back(std::move(operand));
                    if (operands.size() > 96) operands.erase(operands.begin(), operands.begin() + 32);
                } else {
                    lexer.seek(before + 1);
                }
                continue;
            }
            if (c == ']' || c == '>' || c == ')' || c == '{' || c == '}') {
                lexer.seek(before + 1);
                continue;
            }
            const std::string op = lexer.readKeyword();
            if (op.empty()) {
                lexer.seek(before + 1);
                continue;
            }
            if (op == "BI") {
                skipInlineImage(lexer);
            } else {
                execute(op, operands, resolvedResources, depth);
            }
            operands.clear();
        }
    }

private:
    static double num(const std::vector<PdfObject>& operands, std::size_t index) {
        return index < operands.size() ? operands[index].number(0.0) : 0.0;
    }

    void skipInlineImage(PdfLexer& lexer) {
        const std::string_view data = lexer.data();
        std::size_t position = lexer.position();
        const std::size_t id = data.find("ID", position);
        if (id == std::string_view::npos) {
            lexer.seek(data.size());
            return;
        }
        position = id + 2;
        while (true) {
            const std::size_t marker = data.find("EI", position);
            if (marker == std::string_view::npos) {
                lexer.seek(data.size());
                return;
            }
            const bool before = marker == 0 || PdfLexer::isWhite(data[marker - 1]);
            const bool after = marker + 2 >= data.size() || PdfLexer::isWhite(data[marker + 2]);
            if (before && after) {
                lexer.seek(marker + 2);
                return;
            }
            position = marker + 2;
        }
    }

    std::shared_ptr<PdfFont> lookupFont(const PdfObject& resources, const std::string& name) {
        const PdfObject* fontDict = resources.get("Font");
        if (fontDict == nullptr) return nullptr;
        const PdfObject dict = doc_.resolve(*fontDict);
        const PdfObject* entry = dict.get(name);
        if (entry == nullptr) return nullptr;
        return fonts_.get(doc_, *entry);
    }

    void startRun(TextRun& run) {
        const Matrix trm = multiply(tm_, gs_.ctm);
        run.x = trm.e;
        run.y = trm.f;
        run.fontSize = gs_.fontSize * std::sqrt(trm.c * trm.c + trm.d * trm.d);
    }

    void finishRun(TextRun& run) {
        const Matrix trm = multiply(tm_, gs_.ctm);
        run.endX = trm.e;
        if (!run.text.empty() && run.fontSize > 0.0) runs.push_back(std::move(run));
    }

    void showBytes(const std::string& bytes, std::string& out) {
        if (!gs_.font) return;
        std::vector<PdfGlyph> glyphs;
        gs_.font->decode(bytes, glyphs);
        for (const PdfGlyph& glyph : glyphs) {
            out += glyph.text;
            double advance = glyph.width / 1000.0 * gs_.fontSize + gs_.charSpacing;
            if (glyph.isSingleByteSpace) advance += gs_.wordSpacing;
            tm_ = multiply(translation(advance * gs_.horizontalScale, 0), tm_);
        }
    }

    void showText(const std::string& bytes) {
        TextRun run;
        startRun(run);
        showBytes(bytes, run.text);
        finishRun(run);
    }

    void showArray(const PdfObject& array) {
        if (!array.isArray()) return;
        TextRun run;
        startRun(run);
        for (const PdfObject& item : *array.array) {
            if (item.isString()) {
                showBytes(item.text, run.text);
            } else if (item.isNumber()) {
                const double shift = -item.number() / 1000.0 * gs_.fontSize * gs_.horizontalScale;
                if (item.number() < -180.0 && !run.text.empty() && run.text.back() != ' ') run.text.push_back(' ');
                tm_ = multiply(translation(shift, 0), tm_);
            }
        }
        finishRun(run);
    }

    void nextLine(double tx, double ty) {
        tlm_ = multiply(translation(tx, ty), tlm_);
        tm_ = tlm_;
    }

    void paintForm(const PdfObject& resources, const std::string& name, int depth) {
        const PdfObject* xobjects = resources.get("XObject");
        if (xobjects == nullptr) return;
        const PdfObject dict = doc_.resolve(*xobjects);
        const PdfObject* entry = dict.get(name);
        if (entry == nullptr || !entry->isRef()) return;
        const PdfObject form = doc_.resolve(*entry);
        const PdfObject* subtype = form.get("Subtype");
        if (subtype == nullptr || !subtype->isName("Form")) return;
        auto data = doc_.streamData(entry->refNumber);
        if (!data.ok()) {
            warnings_.push_back("form xobject unreadable: " + data.error().message);
            return;
        }
        const GraphicsState savedState = gs_;
        const Matrix savedTm = tm_;
        const Matrix savedTlm = tlm_;
        if (const PdfObject* matrix = form.get("Matrix")) {
            const PdfObject values = doc_.resolve(*matrix);
            if (values.isArray() && values.array->size() == 6) {
                Matrix m;
                m.a = (*values.array)[0].number(1);
                m.b = (*values.array)[1].number(0);
                m.c = (*values.array)[2].number(0);
                m.d = (*values.array)[3].number(1);
                m.e = (*values.array)[4].number(0);
                m.f = (*values.array)[5].number(0);
                gs_.ctm = multiply(m, gs_.ctm);
            }
        }
        const PdfObject* ownResources = form.get("Resources");
        const std::string_view body(reinterpret_cast<const char*>(data.value().data()), data.value().size());
        run(body, ownResources != nullptr ? *ownResources : resources, depth + 1);
        gs_ = savedState;
        tm_ = savedTm;
        tlm_ = savedTlm;
    }

    void execute(const std::string& op, const std::vector<PdfObject>& operands, const PdfObject& resources, int depth) {
        if (op == "q") {
            stack_.push_back(gs_);
        } else if (op == "Q") {
            if (!stack_.empty()) {
                gs_ = stack_.back();
                stack_.pop_back();
            }
        } else if (op == "cm") {
            if (operands.size() >= 6) {
                Matrix m;
                m.a = num(operands, 0);
                m.b = num(operands, 1);
                m.c = num(operands, 2);
                m.d = num(operands, 3);
                m.e = num(operands, 4);
                m.f = num(operands, 5);
                gs_.ctm = multiply(m, gs_.ctm);
            }
        } else if (op == "BT") {
            tm_ = Matrix();
            tlm_ = Matrix();
        } else if (op == "Tf") {
            if (!operands.empty() && operands[0].isName()) gs_.font = lookupFont(resources, operands[0].text);
            gs_.fontSize = num(operands, 1);
        } else if (op == "Tc") {
            gs_.charSpacing = num(operands, 0);
        } else if (op == "Tw") {
            gs_.wordSpacing = num(operands, 0);
        } else if (op == "Tz") {
            gs_.horizontalScale = num(operands, 0) / 100.0;
        } else if (op == "TL") {
            gs_.leading = num(operands, 0);
        } else if (op == "Ts") {
            gs_.rise = num(operands, 0);
        } else if (op == "Td") {
            nextLine(num(operands, 0), num(operands, 1));
        } else if (op == "TD") {
            gs_.leading = -num(operands, 1);
            nextLine(num(operands, 0), num(operands, 1));
        } else if (op == "Tm") {
            if (operands.size() >= 6) {
                Matrix m;
                m.a = num(operands, 0);
                m.b = num(operands, 1);
                m.c = num(operands, 2);
                m.d = num(operands, 3);
                m.e = num(operands, 4);
                m.f = num(operands, 5);
                tm_ = m;
                tlm_ = m;
            }
        } else if (op == "T*") {
            nextLine(0, -gs_.leading);
        } else if (op == "Tj") {
            if (!operands.empty() && operands[0].isString()) showText(operands[0].text);
        } else if (op == "TJ") {
            if (!operands.empty()) showArray(operands[0]);
        } else if (op == "'") {
            nextLine(0, -gs_.leading);
            if (!operands.empty() && operands[0].isString()) showText(operands[0].text);
        } else if (op == "\"") {
            if (operands.size() >= 3) {
                gs_.wordSpacing = num(operands, 0);
                gs_.charSpacing = num(operands, 1);
                nextLine(0, -gs_.leading);
                if (operands[2].isString()) showText(operands[2].text);
            }
        } else if (op == "Do") {
            if (!operands.empty() && operands[0].isName()) paintForm(resources, operands[0].text, depth);
        }
    }

    const PdfDocument& doc_;
    PdfFontCache& fonts_;
    std::vector<std::string>& warnings_;
    GraphicsState gs_;
    std::vector<GraphicsState> stack_;
    Matrix tm_;
    Matrix tlm_;
};

struct Line {
    double y = 0;
    double x0 = 0;
    double x1 = 0;
    double fontSize = 0;
    std::string text;
};

bool endsSentence(const std::string& text) {
    if (text.empty()) return false;
    const char last = text.back();
    if (last == '.' || last == '?' || last == '!' || last == ':' || last == ';') return true;
    if (text.size() >= 3) {
        const std::string tail = text.substr(text.size() - 3);
        if (tail == "\xE2\x80\x9D" || tail == "\xE2\x80\x99") return true;
    }
    return last == '"' || last == ')' || last == '\'';
}

std::vector<Line> buildLines(const std::vector<TextRun>& runs) {
    std::vector<Line> lines;
    for (const TextRun& run : runs) {
        const std::string trimmed = run.text;
        if (!lines.empty()) {
            Line& current = lines.back();
            const double tolerance = 0.5 * std::max(run.fontSize, current.fontSize);
            if (std::fabs(run.y - current.y) <= tolerance && run.x >= current.x1 - run.fontSize) {
                const double gap = run.x - current.x1;
                const bool currentEndsSpace = !current.text.empty() && current.text.back() == ' ';
                const bool runStartsSpace = !trimmed.empty() && trimmed.front() == ' ';
                if (gap > 0.18 * run.fontSize && !currentEndsSpace && !runStartsSpace) current.text.push_back(' ');
                current.text += trimmed;
                current.x1 = std::max(current.x1, run.endX);
                current.fontSize = std::max(current.fontSize, run.fontSize);
                continue;
            }
        }
        Line line;
        line.y = run.y;
        line.x0 = run.x;
        line.x1 = run.endX;
        line.fontSize = run.fontSize;
        line.text = trimmed;
        lines.push_back(std::move(line));
    }
    for (Line& line : lines) line.text = collapseWhitespace(line.text);
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const Line& line) { return line.text.empty(); }),
                lines.end());
    return lines;
}

double dominantFontSize(const std::vector<Line>& lines) {
    std::map<long, std::size_t> weights;
    for (const Line& line : lines) weights[std::lround(line.fontSize * 2.0)] += line.text.size();
    long best = 0;
    std::size_t bestWeight = 0;
    for (const auto& [key, weight] : weights) {
        if (weight > bestWeight) {
            best = key;
            bestWeight = weight;
        }
    }
    return static_cast<double>(best) / 2.0;
}

bool isPageNumber(const std::string& text) {
    if (text.empty() || text.size() > 4) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

}  // namespace

std::shared_ptr<PdfFont> PdfFontCache::get(const PdfDocument& doc, const PdfObject& fontObject) {
    if (fontObject.isRef()) {
        auto it = cache_.find(fontObject.refNumber);
        if (it != cache_.end()) return it->second;
        auto font = PdfFont::load(doc, fontObject);
        cache_[fontObject.refNumber] = font;
        return font;
    }
    return PdfFont::load(doc, fontObject);
}

PdfPageText extractPdfPageText(const PdfDocument& doc, const PdfPage& page, PdfFontCache& fonts,
                               std::vector<std::string>& warnings) {
    PdfPageText result;
    Interpreter interpreter(doc, fonts, warnings);
    const PdfObject* contents = page.dict.get("Contents");
    std::string combined;
    if (contents != nullptr) {
        const PdfObject resolved = doc.resolve(*contents);
        auto append = [&](const PdfObject& reference) {
            if (!reference.isRef()) return;
            auto data = doc.streamData(reference.refNumber);
            if (!data.ok()) {
                warnings.push_back("page content unreadable: " + data.error().message);
                return;
            }
            combined.append(reinterpret_cast<const char*>(data.value().data()), data.value().size());
            combined.push_back('\n');
        };
        if (contents->isRef() && resolved.isArray()) {
            for (const PdfObject& item : *resolved.array) append(item);
        } else if (contents->isRef()) {
            append(*contents);
        } else if (contents->isArray()) {
            for (const PdfObject& item : *contents->array) append(item);
        }
    }
    if (combined.empty()) return result;
    interpreter.run(combined, page.resources, 0);

    std::vector<Line> lines = buildLines(interpreter.runs);
    if (lines.empty()) return result;
    if (isPageNumber(lines.back().text)) lines.pop_back();
    if (!lines.empty() && isPageNumber(lines.front().text) && lines.size() > 1) lines.erase(lines.begin());
    if (lines.empty()) return result;

    const double body = dominantFontSize(lines);
    double maxWidth = 0;
    for (const Line& line : lines) {
        if (line.fontSize <= body * 1.1) maxWidth = std::max(maxWidth, line.x1 - line.x0);
    }

    struct Paragraph {
        std::string text;
        bool heading = false;
        double size = 0;
    };
    std::vector<Paragraph> paragraphs;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];
        const bool heading = body > 0 && line.fontSize >= body * 1.25 && line.text.size() <= 140;
        bool startNew = i == 0;
        if (i > 0) {
            const Line& previous = lines[i - 1];
            const double drop = previous.y - line.y;
            const bool bigGap = drop > 1.7 * previous.fontSize || drop < -3.0 * previous.fontSize;
            const bool headingChange = heading != paragraphs.back().heading;
            const bool previousShort = maxWidth > 0 && (previous.x1 - previous.x0) < 0.72 * maxWidth &&
                                       endsSentence(previous.text);
            const bool indented = line.x0 > previous.x0 + 1.2 * previous.fontSize && endsSentence(previous.text);
            startNew = bigGap || headingChange || previousShort || indented ||
                       (heading && std::fabs(line.fontSize - paragraphs.back().size) > 0.5);
        }
        if (startNew) {
            paragraphs.push_back({line.text, heading, line.fontSize});
            continue;
        }
        std::string& current = paragraphs.back().text;
        const bool hyphenated = current.size() >= 2 && current.back() == '-' &&
                                std::isalpha(static_cast<unsigned char>(current[current.size() - 2])) &&
                                !line.text.empty() && std::islower(static_cast<unsigned char>(line.text[0]));
        if (hyphenated) {
            current.pop_back();
            current += line.text;
        } else {
            current.push_back(' ');
            current += line.text;
        }
    }

    for (std::size_t i = 0; i < paragraphs.size(); ++i) {
        if (i > 0) result.text += "\n\n";
        if (paragraphs[i].heading) {
            PdfHeading heading;
            heading.offset = result.text.size();
            heading.title = paragraphs[i].text;
            heading.level = paragraphs[i].size >= body * 1.6 ? 1 : 2;
            result.headings.push_back(std::move(heading));
        }
        result.text += paragraphs[i].text;
    }
    return result;
}

}  // namespace eden
