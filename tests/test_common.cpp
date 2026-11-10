#include "TestFramework.hpp"
#include "common/Clock.hpp"
#include "common/Hash.hpp"
#include "common/Ids.hpp"
#include "common/Strings.hpp"

using namespace eden;

EDEN_TEST(sha256_known_vectors) {
    EXPECT_EQ(sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    EXPECT_EQ(sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    EXPECT_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

EDEN_TEST(sha256_incremental_matches_single_shot) {
    Sha256 hasher;
    hasher.update("hello ");
    hasher.update("world");
    EXPECT_EQ(hasher.finishHex(), sha256Hex("hello world"));
}

EDEN_TEST(ids_are_unique_and_well_formed) {
    const Id a = newId();
    const Id b = newId();
    EXPECT_TRUE(looksLikeId(a));
    EXPECT_TRUE(looksLikeId(b));
    EXPECT_TRUE(a != b);
    EXPECT_EQ(randomHex(8).size(), static_cast<std::size_t>(16));
}

EDEN_TEST(iso8601_round_trip) {
    const Millis stamp = 1735689600123;
    const std::string text = formatIso8601(stamp);
    EXPECT_EQ(text, std::string("2025-01-01T00:00:00.123Z"));
    auto parsed = parseIso8601(text);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, stamp);
    EXPECT_FALSE(parseIso8601("not a date").has_value());
}

EDEN_TEST(strings_trim_split_join) {
    EXPECT_EQ(trim("  hi there \n"), std::string("hi there"));
    const auto parts = split("a,b,,c", ',');
    EXPECT_EQ(parts.size(), static_cast<std::size_t>(4));
    EXPECT_EQ(split("a,b,,c", ',', false).size(), static_cast<std::size_t>(3));
    EXPECT_EQ(join({"x", "y", "z"}, "-"), std::string("x-y-z"));
    EXPECT_EQ(collapseWhitespace("  a   b \t c  "), std::string("a b c"));
    EXPECT_EQ(wordCount("one two  three\nfour"), static_cast<std::size_t>(4));
}

EDEN_TEST(strings_lines_handle_crlf) {
    const auto lines = splitLines("first\r\nsecond\nthird");
    EXPECT_EQ(lines.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(lines[0], std::string("first"));
    EXPECT_EQ(lines[2], std::string("third"));
}

EDEN_TEST(strings_case_and_search) {
    EXPECT_EQ(toLowerAscii("MiXeD"), std::string("mixed"));
    EXPECT_TRUE(containsIgnoreCase("The Garden of Eden", "garden"));
    EXPECT_TRUE(startsWith("research", "res"));
    EXPECT_TRUE(endsWith("research", "arch"));
    EXPECT_EQ(replaceAll("a.b.c", ".", "::"), std::string("a::b::c"));
    EXPECT_EQ(slugify("Garden of Eden: Part 1!"), std::string("garden-of-eden-part-1"));
}

EDEN_TEST(strings_escaping) {
    EXPECT_EQ(escapeHtml("<a href=\"x\">&'"), std::string("&lt;a href=&quot;x&quot;&gt;&amp;&#39;"));
    EXPECT_EQ(escapeLikePattern("50%_off\\"), std::string("50\\%\\_off\\\\"));
}

EDEN_TEST(utf8_validation_and_truncation) {
    EXPECT_TRUE(isValidUtf8("caf\xC3\xA9"));
    EXPECT_FALSE(isValidUtf8("bad\xC3"));
    EXPECT_FALSE(isValidUtf8("\xED\xA0\x80"));
    EXPECT_EQ(utf8Length("caf\xC3\xA9"), static_cast<std::size_t>(4));
    EXPECT_EQ(truncateUtf8("caf\xC3\xA9", 4), std::string("caf"));
    EXPECT_EQ(truncateWithEllipsis("abcdefghij", 8), std::string("abcde..."));
    EXPECT_EQ(sanitizeUtf8("ok\xFF" "end"), std::string("ok\xEF\xBF\xBD" "end"));
}

EDEN_TEST(utf8_encode_decode_round_trip) {
    const char32_t samples[] = {0x41, 0xE9, 0x20AC, 0x1F48E};
    for (char32_t sample : samples) {
        const std::string encoded = encodeUtf8(sample);
        std::size_t position = 0;
        char32_t decoded = 0;
        EXPECT_TRUE(decodeUtf8(encoded, position, decoded));
        EXPECT_EQ(static_cast<unsigned>(decoded), static_cast<unsigned>(sample));
        EXPECT_EQ(position, encoded.size());
    }
}

EDEN_TEST(base64_round_trip) {
    const std::vector<std::uint8_t> data = {0, 1, 2, 250, 251, 252, 253};
    const std::string encoded = base64Encode(data);
    EXPECT_EQ(base64Decode(encoded), data);
    EXPECT_EQ(base64Encode({'f', 'o', 'o', 'b', 'a'}), std::string("Zm9vYmE="));
}

EDEN_TEST(format_bytes) {
    EXPECT_EQ(formatBytes(512), std::string("512 B"));
    EXPECT_EQ(formatBytes(2048), std::string("2.0 KB"));
}
