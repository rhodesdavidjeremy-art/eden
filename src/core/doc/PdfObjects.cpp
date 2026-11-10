#include "doc/PdfObjects.hpp"

#include <cstdlib>

namespace eden {

PdfObject PdfObject::makeInt(std::int64_t value) {
    PdfObject object;
    object.type = Type::Int;
    object.integer = value;
    return object;
}

PdfObject PdfObject::makeReal(double value) {
    PdfObject object;
    object.type = Type::Real;
    object.real = value;
    return object;
}

PdfObject PdfObject::makeName(std::string value) {
    PdfObject object;
    object.type = Type::Name;
    object.text = std::move(value);
    return object;
}

PdfObject PdfObject::makeString(std::string value) {
    PdfObject object;
    object.type = Type::String;
    object.text = std::move(value);
    return object;
}

PdfObject PdfObject::makeDict() {
    PdfObject object;
    object.type = Type::Dict;
    object.dict = std::make_shared<PdfDict>();
    return object;
}

PdfObject PdfObject::makeArray() {
    PdfObject object;
    object.type = Type::Array;
    object.array = std::make_shared<PdfArray>();
    return object;
}

bool PdfLexer::isWhite(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\0';
}

bool PdfLexer::isDelimiter(char c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' ||
           c == '/' || c == '%';
}

void PdfLexer::skipSpace() {
    while (position_ < data_.size()) {
        const char c = data_[position_];
        if (isWhite(c)) {
            ++position_;
        } else if (c == '%') {
            while (position_ < data_.size() && data_[position_] != '\n' && data_[position_] != '\r') ++position_;
        } else {
            break;
        }
    }
}

std::string PdfLexer::readKeyword() {
    skipSpace();
    const std::size_t start = position_;
    while (position_ < data_.size() && !isWhite(data_[position_]) && !isDelimiter(data_[position_])) ++position_;
    return std::string(data_.substr(start, position_ - start));
}

bool PdfLexer::startsWithKeyword(const char* keyword) {
    skipSpace();
    std::size_t length = 0;
    while (keyword[length] != '\0') ++length;
    if (data_.compare(position_, length, keyword) != 0) return false;
    const std::size_t after = position_ + length;
    return after >= data_.size() || isWhite(data_[after]) || isDelimiter(data_[after]);
}

bool PdfLexer::parseNumber(PdfObject& out, bool allowRefs) {
    const std::size_t start = position_;
    bool isReal = false;
    while (position_ < data_.size()) {
        const char c = data_[position_];
        if ((c >= '0' && c <= '9') || c == '+' || c == '-') {
            ++position_;
        } else if (c == '.') {
            isReal = true;
            ++position_;
        } else {
            break;
        }
    }
    const std::string token(data_.substr(start, position_ - start));
    if (token.empty()) return false;
    if (isReal) {
        out = PdfObject::makeReal(std::strtod(token.c_str(), nullptr));
        return true;
    }
    const long long value = std::strtoll(token.c_str(), nullptr, 10);
    out = PdfObject::makeInt(value);
    if (allowRefs && value >= 0 && token[0] != '-' && token[0] != '+') {
        const std::size_t saved = position_;
        skipSpace();
        const std::size_t genStart = position_;
        while (position_ < data_.size() && data_[position_] >= '0' && data_[position_] <= '9') ++position_;
        if (position_ > genStart) {
            skipSpace();
            if (position_ < data_.size() && data_[position_] == 'R' &&
                (position_ + 1 >= data_.size() || isWhite(data_[position_ + 1]) || isDelimiter(data_[position_ + 1]))) {
                ++position_;
                PdfObject ref;
                ref.type = PdfObject::Type::Ref;
                ref.refNumber = static_cast<int>(value);
                out = ref;
                return true;
            }
        }
        position_ = saved;
    }
    return true;
}

bool PdfLexer::parseName(PdfObject& out) {
    ++position_;
    std::string name;
    while (position_ < data_.size() && !isWhite(data_[position_]) && !isDelimiter(data_[position_])) {
        const char c = data_[position_];
        if (c == '#' && position_ + 2 < data_.size() + 0) {
            auto hexValue = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return -1;
            };
            const int high = hexValue(data_[position_ + 1]);
            const int low = hexValue(data_[position_ + 2]);
            if (high >= 0 && low >= 0) {
                name.push_back(static_cast<char>(high * 16 + low));
                position_ += 3;
                continue;
            }
        }
        name.push_back(c);
        ++position_;
    }
    out = PdfObject::makeName(std::move(name));
    return true;
}

bool PdfLexer::parseLiteralString(PdfObject& out) {
    ++position_;
    std::string value;
    int depth = 1;
    while (position_ < data_.size()) {
        const char c = data_[position_++];
        if (c == '\\') {
            if (position_ >= data_.size()) break;
            const char escape = data_[position_++];
            switch (escape) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case '\r':
                    if (position_ < data_.size() && data_[position_] == '\n') ++position_;
                    break;
                case '\n': break;
                default:
                    if (escape >= '0' && escape <= '7') {
                        int code = escape - '0';
                        for (int i = 0; i < 2 && position_ < data_.size() && data_[position_] >= '0' &&
                                        data_[position_] <= '7';
                             ++i) {
                            code = code * 8 + (data_[position_++] - '0');
                        }
                        value.push_back(static_cast<char>(code & 0xFF));
                    } else {
                        value.push_back(escape);
                    }
            }
        } else if (c == '(') {
            ++depth;
            value.push_back(c);
        } else if (c == ')') {
            if (--depth == 0) {
                out = PdfObject::makeString(std::move(value));
                return true;
            }
            value.push_back(c);
        } else {
            value.push_back(c);
        }
    }
    return false;
}

bool PdfLexer::parseHexString(PdfObject& out) {
    ++position_;
    std::string value;
    int pending = -1;
    while (position_ < data_.size()) {
        const char c = data_[position_++];
        if (c == '>') {
            if (pending >= 0) value.push_back(static_cast<char>(pending << 4));
            out = PdfObject::makeString(std::move(value));
            return true;
        }
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit < 0) continue;
        if (pending < 0) {
            pending = digit;
        } else {
            value.push_back(static_cast<char>((pending << 4) | digit));
            pending = -1;
        }
    }
    return false;
}

bool PdfLexer::parseArray(PdfObject& out, bool allowRefs, int depth) {
    ++position_;
    PdfObject array = PdfObject::makeArray();
    while (true) {
        skipSpace();
        if (position_ >= data_.size()) return false;
        if (data_[position_] == ']') {
            ++position_;
            break;
        }
        PdfObject item;
        if (!parseObject(item, allowRefs, depth + 1)) return false;
        array.array->push_back(std::move(item));
    }
    out = std::move(array);
    return true;
}

bool PdfLexer::parseDict(PdfObject& out, bool allowRefs, int depth) {
    position_ += 2;
    PdfObject dict = PdfObject::makeDict();
    while (true) {
        skipSpace();
        if (position_ >= data_.size()) return false;
        if (data_[position_] == '>') {
            if (position_ + 1 < data_.size() && data_[position_ + 1] == '>') position_ += 2;
            else ++position_;
            break;
        }
        if (data_[position_] != '/') {
            ++position_;
            continue;
        }
        PdfObject key;
        parseName(key);
        skipSpace();
        if (position_ < data_.size() && data_[position_] == '>') {
            (*dict.dict)[key.text] = PdfObject();
            continue;
        }
        PdfObject value;
        if (!parseObject(value, allowRefs, depth + 1)) return false;
        (*dict.dict)[key.text] = std::move(value);
    }
    out = std::move(dict);
    return true;
}

bool PdfLexer::parseObject(PdfObject& out, bool allowRefs, int depth) {
    if (depth > 64) return false;
    skipSpace();
    if (position_ >= data_.size()) return false;
    const char c = data_[position_];
    if (c == '/') return parseName(out);
    if (c == '(') return parseLiteralString(out);
    if (c == '[') return parseArray(out, allowRefs, depth);
    if (c == '<') {
        if (position_ + 1 < data_.size() && data_[position_ + 1] == '<') return parseDict(out, allowRefs, depth);
        return parseHexString(out);
    }
    if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') return parseNumber(out, allowRefs);
    if (c >= 'a' && c <= 'z') {
        const std::size_t saved = position_;
        const std::string word = readKeyword();
        if (word == "true" || word == "false") {
            out = PdfObject();
            out.type = PdfObject::Type::Bool;
            out.boolean = word == "true";
            return true;
        }
        if (word == "null") {
            out = PdfObject();
            return true;
        }
        position_ = saved;
    }
    return false;
}

}  // namespace eden
