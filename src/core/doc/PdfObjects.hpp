#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eden {

struct PdfObject;
using PdfArray = std::vector<PdfObject>;
using PdfDict = std::map<std::string, PdfObject>;

struct PdfObject {
    enum class Type { Null, Bool, Int, Real, String, Name, Array, Dict, Ref };

    Type type = Type::Null;
    bool boolean = false;
    std::int64_t integer = 0;
    double real = 0.0;
    std::string text;
    std::shared_ptr<PdfArray> array;
    std::shared_ptr<PdfDict> dict;
    int refNumber = 0;

    bool isNull() const { return type == Type::Null; }
    bool isNumber() const { return type == Type::Int || type == Type::Real; }
    bool isName() const { return type == Type::Name; }
    bool isName(const char* value) const { return type == Type::Name && text == value; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array && array != nullptr; }
    bool isDict() const { return type == Type::Dict && dict != nullptr; }
    bool isRef() const { return type == Type::Ref; }

    double number(double fallback = 0.0) const {
        if (type == Type::Int) return static_cast<double>(integer);
        if (type == Type::Real) return real;
        return fallback;
    }

    std::int64_t asInt(std::int64_t fallback = 0) const {
        if (type == Type::Int) return integer;
        if (type == Type::Real) return static_cast<std::int64_t>(real);
        return fallback;
    }

    const PdfObject* get(const std::string& key) const {
        if (!isDict()) return nullptr;
        auto it = dict->find(key);
        return it == dict->end() ? nullptr : &it->second;
    }

    static PdfObject makeInt(std::int64_t value);
    static PdfObject makeReal(double value);
    static PdfObject makeName(std::string value);
    static PdfObject makeString(std::string value);
    static PdfObject makeDict();
    static PdfObject makeArray();
};

class PdfLexer {
public:
    PdfLexer(std::string_view data, std::size_t position = 0) : data_(data), position_(position) {}

    std::size_t position() const { return position_; }
    void seek(std::size_t position) { position_ = position; }
    bool atEnd() const { return position_ >= data_.size(); }
    char peek() const { return position_ < data_.size() ? data_[position_] : '\0'; }
    std::string_view data() const { return data_; }

    void skipSpace();
    bool parseObject(PdfObject& out, bool allowRefs = true, int depth = 0);
    std::string readKeyword();
    bool startsWithKeyword(const char* keyword);

    static bool isWhite(char c);
    static bool isDelimiter(char c);

private:
    bool parseNumber(PdfObject& out, bool allowRefs);
    bool parseName(PdfObject& out);
    bool parseLiteralString(PdfObject& out);
    bool parseHexString(PdfObject& out);
    bool parseArray(PdfObject& out, bool allowRefs, int depth);
    bool parseDict(PdfObject& out, bool allowRefs, int depth);

    std::string_view data_;
    std::size_t position_;
};

}  // namespace eden
