#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "common/Result.hpp"

namespace eden {

class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    enum class Type { Null, Bool, Integer, Real, String, Array, Object };

    Json() : data_(std::monostate{}) {}
    Json(std::nullptr_t) : data_(std::monostate{}) {}
    Json(bool value) : data_(value) {}
    Json(int value) : data_(static_cast<std::int64_t>(value)) {}
    Json(long value) : data_(static_cast<std::int64_t>(value)) {}
    Json(long long value) : data_(static_cast<std::int64_t>(value)) {}
    Json(unsigned value) : data_(static_cast<std::int64_t>(value)) {}
    Json(unsigned long value) : data_(static_cast<std::int64_t>(value)) {}
    Json(unsigned long long value) : data_(static_cast<std::int64_t>(value)) {}
    Json(double value) : data_(value) {}
    Json(const char* value) : data_(std::string(value)) {}
    Json(std::string value) : data_(std::move(value)) {}
    Json(Array value) : data_(std::move(value)) {}
    Json(Object value) : data_(std::move(value)) {}

    static Json array() { return Json(Array{}); }
    static Json object() { return Json(Object{}); }
    static Json object(std::initializer_list<std::pair<const std::string, Json>> items);
    static Json arrayOf(std::initializer_list<Json> items);
    static Json arrayOfStrings(const std::vector<std::string>& items);

    Type type() const;
    bool isNull() const { return type() == Type::Null; }
    bool isBool() const { return type() == Type::Bool; }
    bool isInteger() const { return type() == Type::Integer; }
    bool isReal() const { return type() == Type::Real; }
    bool isNumber() const { return isInteger() || isReal(); }
    bool isString() const { return type() == Type::String; }
    bool isArray() const { return type() == Type::Array; }
    bool isObject() const { return type() == Type::Object; }

    bool asBool(bool fallback = false) const;
    std::int64_t asInt(std::int64_t fallback = 0) const;
    double asDouble(double fallback = 0.0) const;
    const std::string& asString() const;
    std::string asString(const std::string& fallback) const;
    const Array& items() const;
    const Object& members() const;

    std::size_t size() const;
    bool empty() const { return size() == 0; }
    bool contains(const std::string& key) const;
    const Json& at(const std::string& key) const;
    const Json& at(std::size_t index) const;
    const Json& get(const std::string& key) const;
    std::string getString(const std::string& key, const std::string& fallback = "") const;
    std::int64_t getInt(const std::string& key, std::int64_t fallback = 0) const;
    double getDouble(const std::string& key, double fallback = 0.0) const;
    bool getBool(const std::string& key, bool fallback = false) const;
    std::vector<std::string> getStringList(const std::string& key) const;

    Json& operator[](const std::string& key);
    Json& operator[](const char* key) { return (*this)[std::string(key)]; }
    Json& push(Json value);
    bool erase(const std::string& key);

    std::string dump(int indent = -1) const;
    static Result<Json> parse(const std::string& text);

    bool operator==(const Json& other) const;
    bool operator!=(const Json& other) const { return !(*this == other); }

private:
    void write(std::string& out, int indent, int depth) const;

    std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object> data_;
};

}  // namespace eden
