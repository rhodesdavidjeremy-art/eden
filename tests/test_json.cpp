#include "TestFramework.hpp"
#include "common/Json.hpp"

using namespace eden;

EDEN_TEST(json_parses_scalars_and_containers) {
    auto result = Json::parse(R"({"a":1,"b":[true,null,2.5],"c":{"d":"x"}})");
    EXPECT_TRUE(result.ok());
    const Json& doc = result.value();
    EXPECT_EQ(doc.getInt("a"), static_cast<std::int64_t>(1));
    EXPECT_EQ(doc.at("b").size(), static_cast<std::size_t>(3));
    EXPECT_TRUE(doc.at("b").at(static_cast<std::size_t>(0)).asBool());
    EXPECT_TRUE(doc.at("b").at(static_cast<std::size_t>(1)).isNull());
    EXPECT_EQ(doc.at("b").at(static_cast<std::size_t>(2)).asDouble(), 2.5);
    EXPECT_EQ(doc.at("c").getString("d"), std::string("x"));
}

EDEN_TEST(json_round_trips_through_dump) {
    Json doc = Json::object();
    doc["name"] = "Eden";
    doc["count"] = 42;
    doc["ratio"] = 0.25;
    doc["tags"] = Json::arrayOfStrings({"a", "b"});
    doc["nested"]["flag"] = true;
    const std::string compact = doc.dump();
    auto parsed = Json::parse(compact);
    EXPECT_TRUE(parsed.ok());
    EXPECT_TRUE(parsed.value() == doc);
    auto pretty = Json::parse(doc.dump(2));
    EXPECT_TRUE(pretty.ok());
    EXPECT_TRUE(pretty.value() == doc);
}

EDEN_TEST(json_string_escapes_and_unicode) {
    auto result = Json::parse(R"("line\nbreak \"q\" \u00e9 \ud83d\udc8e")");
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.value().asString(), std::string("line\nbreak \"q\" \xC3\xA9 \xF0\x9F\x92\x8E"));
    Json value(std::string("tab\there"));
    EXPECT_EQ(value.dump(), std::string("\"tab\\there\""));
}

EDEN_TEST(json_rejects_malformed_input) {
    EXPECT_FALSE(Json::parse("{").ok());
    EXPECT_FALSE(Json::parse("[1,]").ok());
    EXPECT_FALSE(Json::parse("{\"a\":}").ok());
    EXPECT_FALSE(Json::parse("tru").ok());
    EXPECT_FALSE(Json::parse("\"unterminated").ok());
    EXPECT_FALSE(Json::parse("1 2").ok());
    EXPECT_FALSE(Json::parse("\"\\ud800\"").ok());
}

EDEN_TEST(json_numbers_keep_integer_precision) {
    auto result = Json::parse("[9007199254740993, -5, 1e3, 0.5]");
    EXPECT_TRUE(result.ok());
    const Json& list = result.value();
    EXPECT_TRUE(list.at(static_cast<std::size_t>(0)).isInteger());
    EXPECT_EQ(list.at(static_cast<std::size_t>(0)).asInt(), static_cast<std::int64_t>(9007199254740993LL));
    EXPECT_EQ(list.at(static_cast<std::size_t>(1)).asInt(), static_cast<std::int64_t>(-5));
    EXPECT_TRUE(list.at(static_cast<std::size_t>(2)).isReal());
    EXPECT_EQ(list.at(static_cast<std::size_t>(2)).asDouble(), 1000.0);
}

EDEN_TEST(json_accessors_fall_back_safely) {
    Json doc = Json::object();
    doc["n"] = 7;
    EXPECT_EQ(doc.getString("missing", "fallback"), std::string("fallback"));
    EXPECT_EQ(doc.getInt("n"), static_cast<std::int64_t>(7));
    EXPECT_EQ(doc.getInt("missing", 3), static_cast<std::int64_t>(3));
    EXPECT_TRUE(doc.get("missing").isNull());
    EXPECT_TRUE(doc.erase("n"));
    EXPECT_FALSE(doc.contains("n"));
}

EDEN_TEST(json_deep_nesting_is_rejected) {
    std::string text(400, '[');
    text += std::string(400, ']');
    EXPECT_FALSE(Json::parse(text).ok());
}
