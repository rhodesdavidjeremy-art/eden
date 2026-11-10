#include "doc/Xml.hpp"

#include <cstdlib>
#include <cstring>

#include "common/Strings.hpp"

namespace eden {

namespace {

struct NamedEntity {
    const char* name;
    char32_t value;
};

const NamedEntity kEntities[] = {
    {"amp", '&'},      {"lt", '<'},        {"gt", '>'},        {"quot", '"'},     {"apos", '\''},
    {"nbsp", 0xA0},    {"copy", 0xA9},     {"reg", 0xAE},      {"deg", 0xB0},     {"plusmn", 0xB1},
    {"middot", 0xB7},  {"laquo", 0xAB},    {"raquo", 0xBB},    {"sect", 0xA7},    {"para", 0xB6},
    {"eacute", 0xE9},  {"egrave", 0xE8},   {"agrave", 0xE0},   {"aacute", 0xE1},  {"uuml", 0xFC},
    {"ouml", 0xF6},    {"auml", 0xE4},     {"ccedil", 0xE7},   {"ntilde", 0xF1},  {"iacute", 0xED},
    {"oacute", 0xF3},  {"uacute", 0xFA},   {"ndash", 0x2013},  {"mdash", 0x2014}, {"lsquo", 0x2018},
    {"rsquo", 0x2019}, {"ldquo", 0x201C},  {"rdquo", 0x201D},  {"hellip", 0x2026}, {"bull", 0x2022},
    {"trade", 0x2122}, {"euro", 0x20AC},   {"times", 0xD7},    {"divide", 0xF7},  {"thinsp", 0x2009},
    {"ensp", 0x2002},  {"emsp", 0x2003},   {"dagger", 0x2020}, {"prime", 0x2032}, {"larr", 0x2190},
    {"rarr", 0x2192},
};

bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == ':' || c == '.' || (static_cast<unsigned char>(c) >= 0x80);
}

bool isVoidElement(const std::string& name) {
    static const char* names[] = {"br", "hr", "img", "meta", "link", "input", "col", "area", "base", "wbr"};
    if (name.find(':') != std::string::npos) return false;
    const std::string lower = toLowerAscii(name);
    for (const char* candidate : names) {
        if (lower == candidate) return true;
    }
    return false;
}

class Parser {
public:
    explicit Parser(std::string_view source) : source_(source) {}

    Result<XmlNode> parse() {
        XmlNode root;
        root.name = "#document";
        stack_.push_back(&root);
        while (position_ < source_.size()) {
            if (source_[position_] == '<') {
                if (!parseMarkup()) break;
            } else {
                parseText();
            }
            if (failed_) return makeError(ErrorCode::Parse, error_);
        }
        if (failed_) return makeError(ErrorCode::Parse, error_);
        return root;
    }

private:
    static constexpr std::size_t kMaxDepth = 512;

    void fail(const std::string& message) {
        failed_ = true;
        error_ = message + " at offset " + std::to_string(position_);
    }

    bool lookingAt(const char* text) const {
        const std::size_t length = std::strlen(text);
        return source_.compare(position_, length, text) == 0;
    }

    void skipPast(const char* terminator) {
        const std::size_t found = source_.find(terminator, position_);
        position_ = found == std::string_view::npos ? source_.size() : found + std::strlen(terminator);
    }

    void appendText(std::string text) {
        if (text.empty()) return;
        XmlNode& parent = *stack_.back();
        if (!parent.children.empty() && parent.children.back().isText) {
            parent.children.back().text += text;
            return;
        }
        XmlNode node;
        node.isText = true;
        node.text = std::move(text);
        parent.children.push_back(std::move(node));
    }

    void parseText() {
        const std::size_t end = source_.find('<', position_);
        const std::size_t stop = end == std::string_view::npos ? source_.size() : end;
        appendText(decodeXmlEntities(source_.substr(position_, stop - position_)));
        position_ = stop;
    }

    bool parseMarkup() {
        if (lookingAt("<!--")) {
            position_ += 4;
            skipPast("-->");
            return true;
        }
        if (lookingAt("<![CDATA[")) {
            position_ += 9;
            const std::size_t end = source_.find("]]>", position_);
            const std::size_t stop = end == std::string_view::npos ? source_.size() : end;
            appendText(std::string(source_.substr(position_, stop - position_)));
            position_ = end == std::string_view::npos ? source_.size() : end + 3;
            return true;
        }
        if (lookingAt("<?")) {
            position_ += 2;
            skipPast("?>");
            return true;
        }
        if (lookingAt("<!")) {
            skipDeclaration();
            return true;
        }
        if (lookingAt("</")) {
            parseClosingTag();
            return !failed_;
        }
        parseOpeningTag();
        return !failed_;
    }

    void skipDeclaration() {
        int depth = 0;
        while (position_ < source_.size()) {
            const char c = source_[position_++];
            if (c == '<') {
                ++depth;
            } else if (c == '>') {
                if (--depth <= 0) return;
            }
        }
    }

    void parseClosingTag() {
        position_ += 2;
        const std::size_t start = position_;
        while (position_ < source_.size() && isNameChar(source_[position_])) ++position_;
        const std::string name(source_.substr(start, position_ - start));
        skipPast(">");
        for (std::size_t i = stack_.size(); i-- > 1;) {
            if (stack_[i]->name == name) {
                stack_.resize(i);
                return;
            }
        }
    }

    void skipSpace() {
        while (position_ < source_.size()) {
            const char c = source_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++position_;
            } else {
                break;
            }
        }
    }

    void parseOpeningTag() {
        ++position_;
        const std::size_t start = position_;
        while (position_ < source_.size() && isNameChar(source_[position_])) ++position_;
        if (position_ == start) {
            appendText("<");
            return;
        }
        XmlNode node;
        node.name = std::string(source_.substr(start, position_ - start));
        bool selfClosing = false;
        while (true) {
            skipSpace();
            if (position_ >= source_.size()) {
                fail("unterminated tag <" + node.name);
                return;
            }
            const char c = source_[position_];
            if (c == '>') {
                ++position_;
                break;
            }
            if (c == '/') {
                selfClosing = true;
                ++position_;
                continue;
            }
            const std::size_t nameStart = position_;
            while (position_ < source_.size() && isNameChar(source_[position_])) ++position_;
            if (position_ == nameStart) {
                ++position_;
                continue;
            }
            std::string attributeName(source_.substr(nameStart, position_ - nameStart));
            skipSpace();
            std::string value;
            if (position_ < source_.size() && source_[position_] == '=') {
                ++position_;
                skipSpace();
                if (position_ < source_.size() && (source_[position_] == '"' || source_[position_] == '\'')) {
                    const char quote = source_[position_++];
                    const std::size_t valueStart = position_;
                    while (position_ < source_.size() && source_[position_] != quote) ++position_;
                    value = decodeXmlEntities(source_.substr(valueStart, position_ - valueStart));
                    if (position_ < source_.size()) ++position_;
                } else {
                    const std::size_t valueStart = position_;
                    while (position_ < source_.size() && source_[position_] != '>' &&
                           source_[position_] != ' ' && source_[position_] != '/') {
                        ++position_;
                    }
                    value = decodeXmlEntities(source_.substr(valueStart, position_ - valueStart));
                }
            }
            node.attributes.emplace_back(std::move(attributeName), std::move(value));
        }
        if (stack_.size() > kMaxDepth) {
            fail("element nesting too deep");
            return;
        }
        const bool isVoid = isVoidElement(node.name);
        XmlNode& parent = *stack_.back();
        parent.children.push_back(std::move(node));
        if (!selfClosing && !isVoid) stack_.push_back(&parent.children.back());
    }

    std::string_view source_;
    std::size_t position_ = 0;
    std::vector<XmlNode*> stack_;
    bool failed_ = false;
    std::string error_;
};

}  // namespace

std::string decodeXmlEntities(std::string_view text) {
    if (text.find('&') == std::string_view::npos) return std::string(text);
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '&') {
            out.push_back(text[i++]);
            continue;
        }
        const std::size_t semicolon = text.find(';', i + 1);
        if (semicolon == std::string_view::npos || semicolon - i > 12) {
            out.push_back(text[i++]);
            continue;
        }
        const std::string_view body = text.substr(i + 1, semicolon - i - 1);
        char32_t value = 0;
        bool resolved = false;
        if (!body.empty() && body[0] == '#') {
            const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
            const std::string digits(body.substr(hex ? 2 : 1));
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(digits.c_str(), &end, hex ? 16 : 10);
            if (!digits.empty() && end != nullptr && *end == '\0' && parsed > 0 && parsed <= 0x10FFFF) {
                value = static_cast<char32_t>(parsed);
                resolved = true;
            }
        } else {
            for (const NamedEntity& entity : kEntities) {
                if (body == entity.name) {
                    value = entity.value;
                    resolved = true;
                    break;
                }
            }
        }
        if (resolved) {
            out += encodeUtf8(value);
            i = semicolon + 1;
        } else {
            out.push_back(text[i++]);
        }
    }
    return out;
}

std::string XmlNode::localName() const {
    const std::size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

const std::string* XmlNode::attribute(std::string_view key) const {
    for (const auto& pair : attributes) {
        if (pair.first == key) return &pair.second;
    }
    for (const auto& pair : attributes) {
        const std::size_t colon = pair.first.find(':');
        if (colon != std::string::npos && std::string_view(pair.first).substr(colon + 1) == key) return &pair.second;
    }
    return nullptr;
}

std::string XmlNode::attr(std::string_view key, const std::string& fallback) const {
    const std::string* value = attribute(key);
    return value != nullptr ? *value : fallback;
}

const XmlNode* XmlNode::firstChild(std::string_view local) const {
    for (const XmlNode& child : children) {
        if (!child.isText && child.localName() == local) return &child;
    }
    return nullptr;
}

std::vector<const XmlNode*> XmlNode::childrenNamed(std::string_view local) const {
    std::vector<const XmlNode*> out;
    for (const XmlNode& child : children) {
        if (!child.isText && child.localName() == local) out.push_back(&child);
    }
    return out;
}

const XmlNode* XmlNode::findDescendant(std::string_view local) const {
    for (const XmlNode& child : children) {
        if (child.isText) continue;
        if (child.localName() == local) return &child;
        if (const XmlNode* nested = child.findDescendant(local)) return nested;
    }
    return nullptr;
}

void XmlNode::collectDescendants(std::string_view local, std::vector<const XmlNode*>& out) const {
    for (const XmlNode& child : children) {
        if (child.isText) continue;
        if (child.localName() == local) out.push_back(&child);
        child.collectDescendants(local, out);
    }
}

std::string XmlNode::textContent() const {
    if (isText) return text;
    std::string out;
    for (const XmlNode& child : children) out += child.textContent();
    return out;
}

Result<XmlNode> parseXml(std::string_view source) {
    Parser parser(source);
    return parser.parse();
}

}  // namespace eden
