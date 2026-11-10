#include "doc/PdfFont.hpp"

#include <cstdlib>
#include <unordered_map>

#include "common/Strings.hpp"
#include "doc/TextUtil.hpp"

namespace eden {

namespace {

const char32_t kMacRomanHigh[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7};

const std::unordered_map<std::string, std::string>& glyphTable() {
    static const std::unordered_map<std::string, std::string> table = [] {
        std::unordered_map<std::string, std::string> built;
        const char* names[] = {"space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand",
                               "quotesingle", "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen",
                               "period", "slash"};
        for (int i = 0; i < 16; ++i) built[names[i]] = std::string(1, static_cast<char>(0x20 + i));
        const char* digits[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
        for (int i = 0; i < 10; ++i) built[digits[i]] = std::string(1, static_cast<char>('0' + i));
        const char* punctuation[] = {"colon", "semicolon", "less", "equal", "greater", "question", "at"};
        for (int i = 0; i < 7; ++i) built[punctuation[i]] = std::string(1, static_cast<char>(0x3A + i));
        const char* brackets[] = {"bracketleft", "backslash", "bracketright", "asciicircum", "underscore", "grave"};
        for (int i = 0; i < 6; ++i) built[brackets[i]] = std::string(1, static_cast<char>(0x5B + i));
        built["braceleft"] = "{";
        built["bar"] = "|";
        built["braceright"] = "}";
        built["asciitilde"] = "~";
        built["bullet"] = "\xE2\x80\xA2";
        built["endash"] = "\xE2\x80\x93";
        built["emdash"] = "\xE2\x80\x94";
        built["quoteleft"] = "\xE2\x80\x98";
        built["quoteright"] = "\xE2\x80\x99";
        built["quotedblleft"] = "\xE2\x80\x9C";
        built["quotedblright"] = "\xE2\x80\x9D";
        built["quotesinglbase"] = "\xE2\x80\x9A";
        built["quotedblbase"] = "\xE2\x80\x9E";
        built["ellipsis"] = "\xE2\x80\xA6";
        built["fi"] = "fi";
        built["fl"] = "fl";
        built["ff"] = "ff";
        built["ffi"] = "ffi";
        built["ffl"] = "ffl";
        built["nbspace"] = " ";
        built["sfthyphen"] = "-";
        built["minus"] = "\xE2\x88\x92";
        built["multiply"] = "\xC3\x97";
        built["divide"] = "\xC3\xB7";
        built["degree"] = "\xC2\xB0";
        built["copyright"] = "\xC2\xA9";
        built["registered"] = "\xC2\xAE";
        built["trademark"] = "\xE2\x84\xA2";
        built["section"] = "\xC2\xA7";
        built["paragraph"] = "\xC2\xB6";
        built["dagger"] = "\xE2\x80\xA0";
        built["Euro"] = "\xE2\x82\xAC";
        built["sterling"] = "\xC2\xA3";
        built["yen"] = "\xC2\xA5";
        built["cent"] = "\xC2\xA2";
        built["guillemotleft"] = "\xC2\xAB";
        built["guillemotright"] = "\xC2\xBB";
        const struct {
            const char* name;
            char32_t cp;
        } accented[] = {{"eacute", 0xE9},   {"egrave", 0xE8},   {"ecircumflex", 0xEA}, {"edieresis", 0xEB},
                        {"aacute", 0xE1},   {"agrave", 0xE0},   {"acircumflex", 0xE2}, {"adieresis", 0xE4},
                        {"atilde", 0xE3},   {"aring", 0xE5},    {"ccedilla", 0xE7},    {"iacute", 0xED},
                        {"igrave", 0xEC},   {"icircumflex", 0xEE}, {"idieresis", 0xEF}, {"ntilde", 0xF1},
                        {"oacute", 0xF3},   {"ograve", 0xF2},   {"ocircumflex", 0xF4}, {"odieresis", 0xF6},
                        {"otilde", 0xF5},   {"uacute", 0xFA},   {"ugrave", 0xF9},      {"ucircumflex", 0xFB},
                        {"udieresis", 0xFC}, {"germandbls", 0xDF}, {"Eacute", 0xC9},   {"Adieresis", 0xC4},
                        {"Odieresis", 0xD6}, {"Udieresis", 0xDC}, {"AE", 0xC6},        {"ae", 0xE6},
                        {"Oslash", 0xD8},   {"oslash", 0xF8}};
        for (const auto& entry : accented) built[entry.name] = encodeUtf8(entry.cp);
        return built;
    }();
    return table;
}

std::string hexToUtf8(const std::string& raw) {
    if (raw.empty()) return std::string();
    if (raw.size() == 1) return encodeUtf8(static_cast<unsigned char>(raw[0]));
    return utf16ToUtf8(Bytes(raw.begin(), raw.end()), false, 0);
}

std::uint32_t codeOf(const std::string& raw) {
    std::uint32_t code = 0;
    for (unsigned char c : raw) code = (code << 8) | c;
    return code;
}

}  // namespace

std::string glyphNameToUtf8(const std::string& name) {
    auto it = glyphTable().find(name);
    if (it != glyphTable().end()) return it->second;
    if (name.size() == 1) return name;
    if (startsWith(name, "uni") && name.size() >= 7) {
        const unsigned long value = std::strtoul(name.substr(3, 4).c_str(), nullptr, 16);
        if (value > 0) return encodeUtf8(static_cast<char32_t>(value));
    }
    if (name.size() >= 5 && name[0] == 'u' && name != "uacute" && name != "ugrave") {
        char* end = nullptr;
        const unsigned long value = std::strtoul(name.c_str() + 1, &end, 16);
        if (end != nullptr && *end == '\0' && value > 0 && value <= 0x10FFFF) return encodeUtf8(static_cast<char32_t>(value));
    }
    return std::string();
}

std::shared_ptr<PdfFont> PdfFont::load(const PdfDocument& doc, const PdfObject& fontObject) {
    auto font = std::make_shared<PdfFont>();
    const PdfObject dict = doc.resolve(fontObject);
    if (!dict.isDict()) return font;
    const PdfObject* subtype = dict.get("Subtype");
    const bool composite = subtype != nullptr && subtype->isName("Type0");
    font->twoByte_ = composite;
    if (const PdfObject* toUnicode = dict.get("ToUnicode")) font->loadToUnicode(doc, *toUnicode);
    if (composite) {
        const PdfObject* encoding = dict.get("Encoding");
        if (encoding != nullptr) {
            const PdfObject resolved = doc.resolve(*encoding);
            if (resolved.isName() && !(endsWith(resolved.text, "-H") || endsWith(resolved.text, "-V") ||
                                       startsWith(resolved.text, "Identity"))) {
                font->twoByte_ = font->twoByte_ && true;
            }
        }
        const PdfObject* descendants = dict.get("DescendantFonts");
        if (descendants != nullptr) {
            const PdfObject list = doc.resolve(*descendants);
            if (list.isArray() && !list.array->empty()) {
                const PdfObject descendant = doc.resolve(list.array->front());
                font->loadCidWidths(doc, descendant);
            }
        }
    } else {
        for (int code = 0; code < 256; ++code) {
            if (code >= 0x20 && code < 0x7F) {
                font->simple_[static_cast<std::size_t>(code)] = std::string(1, static_cast<char>(code));
            } else if (code >= 0xA0) {
                font->simple_[static_cast<std::size_t>(code)] = encodeUtf8(static_cast<char32_t>(code));
            }
        }
        for (int code = 0x80; code < 0xA0; ++code) {
            font->simple_[static_cast<std::size_t>(code)] =
                windows1252ToUtf8(Bytes{static_cast<std::uint8_t>(code)});
        }
        if (const PdfObject* encoding = dict.get("Encoding")) font->loadSimpleEncoding(doc, *encoding);
        font->loadSimpleWidths(doc, dict);
    }
    return font;
}

void PdfFont::loadToUnicode(const PdfDocument& doc, const PdfObject& stream) {
    if (!stream.isRef()) return;
    auto data = doc.streamData(stream.refNumber);
    if (!data.ok()) return;
    const std::string_view text(reinterpret_cast<const char*>(data.value().data()), data.value().size());
    PdfLexer lexer(text, 0);
    std::vector<PdfObject> operands;
    while (true) {
        lexer.skipSpace();
        if (lexer.atEnd()) break;
        PdfObject value;
        const std::size_t before = lexer.position();
        const char c = lexer.peek();
        if (c == '<' || c == '[' || c == '/' || c == '(' || (c >= '0' && c <= '9') || c == '-') {
            if (lexer.parseObject(value, false)) {
                operands.push_back(std::move(value));
            } else if (lexer.position() == before) {
                lexer.seek(before + 1);
            }
            continue;
        }
        const std::string keyword = lexer.readKeyword();
        if (keyword.empty()) {
            lexer.seek(before + 1);
            continue;
        }
        if (keyword == "endcodespacerange") {
            if (!operands.empty() && operands.front().isString() && operands.front().text.size() >= 2) twoByte_ = true;
            if (!operands.empty() && operands.front().isString() && operands.front().text.size() == 1) twoByte_ = false;
        } else if (keyword == "endbfchar") {
            for (std::size_t i = 0; i + 1 < operands.size(); i += 2) {
                if (!operands[i].isString() || !operands[i + 1].isString()) continue;
                toUnicode_[codeOf(operands[i].text)] = hexToUtf8(operands[i + 1].text);
            }
        } else if (keyword == "endbfrange") {
            for (std::size_t i = 0; i + 2 < operands.size(); i += 3) {
                if (!operands[i].isString() || !operands[i + 1].isString()) continue;
                const std::uint32_t low = codeOf(operands[i].text);
                const std::uint32_t high = codeOf(operands[i + 1].text);
                if (high < low || high - low > 0xFFFF) continue;
                const PdfObject& destination = operands[i + 2];
                if (destination.isString()) {
                    const std::string base = hexToUtf8(destination.text);
                    std::size_t lastStart = base.size();
                    while (lastStart > 0 && (static_cast<unsigned char>(base[lastStart - 1]) & 0xC0) == 0x80) --lastStart;
                    if (lastStart > 0) --lastStart;
                    std::size_t cursor = lastStart;
                    char32_t lastCodepoint = 0;
                    if (!decodeUtf8(base, cursor, lastCodepoint)) continue;
                    const std::string prefix = base.substr(0, lastStart);
                    for (std::uint32_t code = low; code <= high; ++code) {
                        toUnicode_[code] = prefix + encodeUtf8(lastCodepoint + (code - low));
                    }
                } else if (destination.isArray()) {
                    std::uint32_t code = low;
                    for (const PdfObject& item : *destination.array) {
                        if (code > high) break;
                        if (item.isString()) toUnicode_[code] = hexToUtf8(item.text);
                        ++code;
                    }
                }
            }
        }
        operands.clear();
    }
    hasToUnicode_ = !toUnicode_.empty();
}

void PdfFont::loadSimpleEncoding(const PdfDocument& doc, const PdfObject& encodingObject) {
    const PdfObject encoding = doc.resolve(encodingObject);
    auto applyBase = [&](const std::string& name) {
        if (name == "MacRomanEncoding") {
            for (int code = 0x80; code <= 0xFF; ++code) {
                simple_[static_cast<std::size_t>(code)] = encodeUtf8(kMacRomanHigh[code - 0x80]);
            }
        }
    };
    if (encoding.isName()) {
        applyBase(encoding.text);
        return;
    }
    if (!encoding.isDict()) return;
    if (const PdfObject* base = encoding.get("BaseEncoding")) {
        const PdfObject resolved = doc.resolve(*base);
        if (resolved.isName()) applyBase(resolved.text);
    }
    const PdfObject* differences = encoding.get("Differences");
    if (differences == nullptr) return;
    const PdfObject list = doc.resolve(*differences);
    if (!list.isArray()) return;
    int code = 0;
    for (const PdfObject& item : *list.array) {
        const PdfObject resolved = doc.resolve(item);
        if (resolved.isNumber()) {
            code = static_cast<int>(resolved.asInt());
        } else if (resolved.isName()) {
            if (code >= 0 && code < 256) simple_[static_cast<std::size_t>(code)] = glyphNameToUtf8(resolved.text);
            ++code;
        }
    }
}

void PdfFont::loadSimpleWidths(const PdfDocument& doc, const PdfObject& font) {
    int firstChar = 0;
    if (const PdfObject* first = font.get("FirstChar")) firstChar = static_cast<int>(doc.resolve(*first).asInt());
    if (const PdfObject* widths = font.get("Widths")) {
        const PdfObject list = doc.resolve(*widths);
        if (list.isArray()) {
            int code = firstChar;
            for (const PdfObject& item : *list.array) {
                widths_[static_cast<std::uint32_t>(code++)] = doc.resolve(item).number(500.0);
            }
        }
    }
    if (const PdfObject* descriptor = font.get("FontDescriptor")) {
        const PdfObject resolved = doc.resolve(*descriptor);
        if (const PdfObject* missing = resolved.get("MissingWidth")) defaultWidth_ = doc.resolve(*missing).number(500.0);
    }
}

void PdfFont::loadCidWidths(const PdfDocument& doc, const PdfObject& descendant) {
    defaultWidth_ = 1000.0;
    if (const PdfObject* dw = descendant.get("DW")) defaultWidth_ = doc.resolve(*dw).number(1000.0);
    const PdfObject* wEntry = descendant.get("W");
    if (wEntry == nullptr) return;
    const PdfObject list = doc.resolve(*wEntry);
    if (!list.isArray()) return;
    const PdfArray& items = *list.array;
    std::size_t i = 0;
    while (i < items.size()) {
        const PdfObject first = doc.resolve(items[i]);
        if (i + 1 >= items.size()) break;
        const PdfObject second = doc.resolve(items[i + 1]);
        if (second.isArray()) {
            std::uint32_t code = static_cast<std::uint32_t>(first.asInt());
            for (const PdfObject& width : *second.array) widths_[code++] = doc.resolve(width).number(1000.0);
            i += 2;
        } else if (i + 2 < items.size()) {
            const std::uint32_t low = static_cast<std::uint32_t>(first.asInt());
            const std::uint32_t high = static_cast<std::uint32_t>(second.asInt());
            const double width = doc.resolve(items[i + 2]).number(1000.0);
            if (high >= low && high - low < 0x10000) {
                for (std::uint32_t code = low; code <= high; ++code) widths_[code] = width;
            }
            i += 3;
        } else {
            break;
        }
    }
}

void PdfFont::decode(const std::string& bytes, std::vector<PdfGlyph>& out) const {
    const std::size_t step = twoByte_ ? 2 : 1;
    for (std::size_t i = 0; i + step <= bytes.size(); i += step) {
        std::uint32_t code = static_cast<unsigned char>(bytes[i]);
        if (twoByte_) code = (code << 8) | static_cast<unsigned char>(bytes[i + 1]);
        PdfGlyph glyph;
        auto width = widths_.find(code);
        glyph.width = width != widths_.end() ? width->second : defaultWidth_;
        glyph.isSingleByteSpace = !twoByte_ && code == 32;
        auto mapped = toUnicode_.find(code);
        if (mapped != toUnicode_.end()) {
            glyph.text = mapped->second;
        } else if (!twoByte_) {
            glyph.text = simple_[code & 0xFF];
        } else if (!hasToUnicode_ && code >= 0x20 && code < 0x7F) {
            glyph.text = std::string(1, static_cast<char>(code));
        }
        out.push_back(std::move(glyph));
    }
}

}  // namespace eden
