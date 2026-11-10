#include "common/Json.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "common/Strings.hpp"

namespace eden {

namespace {

const Json kNullJson;
const std::string kEmptyString;
const Json::Array kEmptyArray;
const Json::Object kEmptyObject;

void writeEscaped(std::string& out, const std::string& text) {
    out.push_back('"');
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    Json parseDocument() {
        skipSpace();
        Json value = parseValue(0);
        skipSpace();
        if (position_ != text_.size()) fail("unexpected trailing characters");
        return value;
    }

private:
    static constexpr int kMaxDepth = 256;

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(message + " at offset " + std::to_string(position_));
    }

    bool atEnd() const { return position_ >= text_.size(); }

    char peek() const { return atEnd() ? '\0' : text_[position_]; }

    void skipSpace() {
        while (!atEnd()) {
            const char c = text_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++position_;
            } else {
                break;
            }
        }
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++position_;
    }

    bool consumeLiteral(const char* literal) {
        std::size_t length = 0;
        while (literal[length] != '\0') ++length;
        if (text_.compare(position_, length, literal) == 0) {
            position_ += length;
            return true;
        }
        return false;
    }

    Json parseValue(int depth) {
        if (depth > kMaxDepth) fail("nesting too deep");
        skipSpace();
        if (atEnd()) fail("unexpected end of input");
        const char c = peek();
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') return Json(parseString());
        if (c == 't') {
            if (!consumeLiteral("true")) fail("invalid literal");
            return Json(true);
        }
        if (c == 'f') {
            if (!consumeLiteral("false")) fail("invalid literal");
            return Json(false);
        }
        if (c == 'n') {
            if (!consumeLiteral("null")) fail("invalid literal");
            return Json();
        }
        return parseNumber();
    }

    Json parseObject(int depth) {
        expect('{');
        Json::Object members;
        skipSpace();
        if (peek() == '}') {
            ++position_;
            return Json(std::move(members));
        }
        while (true) {
            skipSpace();
            if (peek() != '"') fail("expected string key");
            std::string key = parseString();
            skipSpace();
            expect(':');
            Json value = parseValue(depth + 1);
            members[std::move(key)] = std::move(value);
            skipSpace();
            if (peek() == ',') {
                ++position_;
                continue;
            }
            expect('}');
            break;
        }
        return Json(std::move(members));
    }

    Json parseArray(int depth) {
        expect('[');
        Json::Array items;
        skipSpace();
        if (peek() == ']') {
            ++position_;
            return Json(std::move(items));
        }
        while (true) {
            items.push_back(parseValue(depth + 1));
            skipSpace();
            if (peek() == ',') {
                ++position_;
                continue;
            }
            expect(']');
            break;
        }
        return Json(std::move(items));
    }

    unsigned parseHex4() {
        if (position_ + 4 > text_.size()) fail("truncated unicode escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                fail("invalid unicode escape");
            }
        }
        return value;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (atEnd()) fail("unterminated string");
            const char c = text_[position_++];
            if (c == '"') break;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (atEnd()) fail("unterminated escape");
            const char escape = text_[position_++];
            switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned unit = parseHex4();
                    char32_t codepoint = unit;
                    if (unit >= 0xD800 && unit <= 0xDBFF) {
                        if (!consumeLiteral("\\u")) fail("missing low surrogate");
                        const unsigned low = parseHex4();
                        if (low < 0xDC00 || low > 0xDFFF) fail("invalid low surrogate");
                        codepoint = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                        fail("unexpected low surrogate");
                    }
                    out += encodeUtf8(codepoint);
                    break;
                }
                default: fail("invalid escape sequence");
            }
        }
        return out;
    }

    Json parseNumber() {
        const std::size_t start = position_;
        bool isReal = false;
        if (peek() == '-') ++position_;
        if (peek() == '0') {
            ++position_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') ++position_;
        } else {
            fail("invalid number");
        }
        if (peek() == '.') {
            isReal = true;
            ++position_;
            if (!(peek() >= '0' && peek() <= '9')) fail("invalid fraction");
            while (peek() >= '0' && peek() <= '9') ++position_;
        }
        if (peek() == 'e' || peek() == 'E') {
            isReal = true;
            ++position_;
            if (peek() == '+' || peek() == '-') ++position_;
            if (!(peek() >= '0' && peek() <= '9')) fail("invalid exponent");
            while (peek() >= '0' && peek() <= '9') ++position_;
        }
        const std::string token = text_.substr(start, position_ - start);
        if (!isReal) {
            errno = 0;
            char* end = nullptr;
            const long long parsed = std::strtoll(token.c_str(), &end, 10);
            if (errno == 0 && end != nullptr && *end == '\0') {
                return Json(static_cast<std::int64_t>(parsed));
            }
        }
        return Json(std::strtod(token.c_str(), nullptr));
    }

    const std::string& text_;
    std::size_t position_ = 0;
};

}  // namespace

Json Json::object(std::initializer_list<std::pair<const std::string, Json>> items) {
    Object members;
    for (const auto& item : items) members[item.first] = item.second;
    return Json(std::move(members));
}

Json Json::arrayOf(std::initializer_list<Json> items) { return Json(Array(items)); }

Json Json::arrayOfStrings(const std::vector<std::string>& items) {
    Array out;
    out.reserve(items.size());
    for (const auto& item : items) out.emplace_back(item);
    return Json(std::move(out));
}

Json::Type Json::type() const {
    switch (data_.index()) {
        case 0: return Type::Null;
        case 1: return Type::Bool;
        case 2: return Type::Integer;
        case 3: return Type::Real;
        case 4: return Type::String;
        case 5: return Type::Array;
        default: return Type::Object;
    }
}

bool Json::asBool(bool fallback) const {
    if (const auto* value = std::get_if<bool>(&data_)) return *value;
    return fallback;
}

std::int64_t Json::asInt(std::int64_t fallback) const {
    if (const auto* value = std::get_if<std::int64_t>(&data_)) return *value;
    if (const auto* real = std::get_if<double>(&data_)) return static_cast<std::int64_t>(*real);
    return fallback;
}

double Json::asDouble(double fallback) const {
    if (const auto* real = std::get_if<double>(&data_)) return *real;
    if (const auto* value = std::get_if<std::int64_t>(&data_)) return static_cast<double>(*value);
    return fallback;
}

const std::string& Json::asString() const {
    if (const auto* value = std::get_if<std::string>(&data_)) return *value;
    return kEmptyString;
}

std::string Json::asString(const std::string& fallback) const {
    if (const auto* value = std::get_if<std::string>(&data_)) return *value;
    return fallback;
}

const Json::Array& Json::items() const {
    if (const auto* value = std::get_if<Array>(&data_)) return *value;
    return kEmptyArray;
}

const Json::Object& Json::members() const {
    if (const auto* value = std::get_if<Object>(&data_)) return *value;
    return kEmptyObject;
}

std::size_t Json::size() const {
    if (const auto* value = std::get_if<Array>(&data_)) return value->size();
    if (const auto* value = std::get_if<Object>(&data_)) return value->size();
    if (const auto* value = std::get_if<std::string>(&data_)) return value->size();
    return 0;
}

bool Json::contains(const std::string& key) const {
    if (const auto* value = std::get_if<Object>(&data_)) return value->count(key) > 0;
    return false;
}

const Json& Json::at(const std::string& key) const {
    const auto* object = std::get_if<Object>(&data_);
    if (object == nullptr) throw std::out_of_range("json value is not an object");
    auto it = object->find(key);
    if (it == object->end()) throw std::out_of_range("missing key: " + key);
    return it->second;
}

const Json& Json::at(std::size_t index) const {
    const auto* array = std::get_if<Array>(&data_);
    if (array == nullptr) throw std::out_of_range("json value is not an array");
    return array->at(index);
}

const Json& Json::get(const std::string& key) const {
    if (const auto* object = std::get_if<Object>(&data_)) {
        auto it = object->find(key);
        if (it != object->end()) return it->second;
    }
    return kNullJson;
}

std::string Json::getString(const std::string& key, const std::string& fallback) const {
    const Json& value = get(key);
    return value.isString() ? value.asString() : fallback;
}

std::int64_t Json::getInt(const std::string& key, std::int64_t fallback) const {
    const Json& value = get(key);
    return value.isNumber() ? value.asInt() : fallback;
}

double Json::getDouble(const std::string& key, double fallback) const {
    const Json& value = get(key);
    return value.isNumber() ? value.asDouble() : fallback;
}

bool Json::getBool(const std::string& key, bool fallback) const {
    const Json& value = get(key);
    return value.isBool() ? value.asBool() : fallback;
}

std::vector<std::string> Json::getStringList(const std::string& key) const {
    std::vector<std::string> out;
    for (const Json& item : get(key).items()) {
        if (item.isString()) out.push_back(item.asString());
    }
    return out;
}

Json& Json::operator[](const std::string& key) {
    if (!isObject()) data_ = Object{};
    return std::get<Object>(data_)[key];
}

Json& Json::push(Json value) {
    if (!isArray()) data_ = Array{};
    auto& array = std::get<Array>(data_);
    array.push_back(std::move(value));
    return array.back();
}

bool Json::erase(const std::string& key) {
    if (auto* object = std::get_if<Object>(&data_)) return object->erase(key) > 0;
    return false;
}

bool Json::operator==(const Json& other) const {
    if (isNumber() && other.isNumber()) {
        if (isInteger() && other.isInteger()) return asInt() == other.asInt();
        return asDouble() == other.asDouble();
    }
    return data_ == other.data_;
}

void Json::write(std::string& out, int indent, int depth) const {
    const bool pretty = indent >= 0;
    auto newline = [&](int level) {
        if (!pretty) return;
        out.push_back('\n');
        out.append(static_cast<std::size_t>(level * indent), ' ');
    };
    switch (type()) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += asBool() ? "true" : "false"; break;
        case Type::Integer: out += std::to_string(asInt()); break;
        case Type::Real: {
            const double value = asDouble();
            if (!std::isfinite(value)) {
                out += "null";
            } else {
                char buffer[40];
                std::snprintf(buffer, sizeof(buffer), "%.17g", value);
                std::string text = buffer;
                if (text.find_first_of(".eE") == std::string::npos) text += ".0";
                out += text;
            }
            break;
        }
        case Type::String: writeEscaped(out, asString()); break;
        case Type::Array: {
            const auto& array = items();
            if (array.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (std::size_t i = 0; i < array.size(); ++i) {
                if (i > 0) out.push_back(',');
                newline(depth + 1);
                array[i].write(out, indent, depth + 1);
            }
            newline(depth);
            out.push_back(']');
            break;
        }
        case Type::Object: {
            const auto& object = members();
            if (object.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            bool first = true;
            for (const auto& [key, value] : object) {
                if (!first) out.push_back(',');
                first = false;
                newline(depth + 1);
                writeEscaped(out, key);
                out.push_back(':');
                if (pretty) out.push_back(' ');
                value.write(out, indent, depth + 1);
            }
            newline(depth);
            out.push_back('}');
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    write(out, indent, 0);
    return out;
}

Result<Json> Json::parse(const std::string& text) {
    try {
        Parser parser(text);
        return parser.parseDocument();
    } catch (const std::exception& error) {
        return makeError(ErrorCode::Parse, error.what());
    }
}

}  // namespace eden
