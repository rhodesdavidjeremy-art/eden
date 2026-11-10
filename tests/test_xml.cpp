#include "TestFramework.hpp"
#include "doc/HtmlText.hpp"
#include "doc/Xml.hpp"

using namespace eden;

EDEN_TEST(xml_parses_nested_elements_and_attributes) {
    auto result = parseXml("<?xml version=\"1.0\"?><root a=\"1\" b='two'><child>text</child><child/></root>");
    EXPECT_TRUE(result.ok());
    const XmlNode* root = result.value().firstChild("root");
    EXPECT_TRUE(root != nullptr);
    EXPECT_EQ(root->attr("a"), std::string("1"));
    EXPECT_EQ(root->attr("b"), std::string("two"));
    EXPECT_EQ(root->childrenNamed("child").size(), static_cast<std::size_t>(2));
    EXPECT_EQ(root->firstChild("child")->textContent(), std::string("text"));
}

EDEN_TEST(xml_decodes_entities_cdata_and_comments) {
    auto result = parseXml("<a>x &amp; y &lt;z&gt; &#65;&#x42; &mdash;<!-- hidden --><![CDATA[<raw>]]></a>");
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.value().firstChild("a")->textContent(), std::string("x & y <z> AB \xE2\x80\x94<raw>"));
    EXPECT_EQ(decodeXmlEntities("&unknown; &amp"), std::string("&unknown; &amp"));
}

EDEN_TEST(xml_namespaces_resolve_by_local_name) {
    auto result = parseXml("<w:doc xmlns:w=\"ns\"><w:p w:val=\"x\"><w:t>hi</w:t></w:p></w:doc>");
    EXPECT_TRUE(result.ok());
    const XmlNode* paragraph = result.value().findDescendant("p");
    EXPECT_TRUE(paragraph != nullptr);
    EXPECT_EQ(paragraph->attr("val"), std::string("x"));
    EXPECT_EQ(result.value().findDescendant("t")->textContent(), std::string("hi"));
}

EDEN_TEST(xml_tolerates_html_void_elements_and_stray_closers) {
    auto result = parseXml("<body><p>one<br>two</p></span><p>three</p></body>");
    EXPECT_TRUE(result.ok());
    std::vector<const XmlNode*> paragraphs;
    result.value().collectDescendants("p", paragraphs);
    EXPECT_EQ(paragraphs.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(paragraphs[0]->textContent(), std::string("onetwo"));
}

EDEN_TEST(xml_rejects_runaway_nesting) {
    std::string text;
    for (int i = 0; i < 700; ++i) text += "<a>";
    EXPECT_FALSE(parseXml(text).ok());
}

EDEN_TEST(html_extractor_separates_blocks_and_records_headings) {
    auto parsed = parseXml(
        "<body><h1>Title Here</h1><p>First   paragraph\n with  spacing.</p><p>Second <b>bold</b> text.</p>"
        "<ul><li>alpha</li><li>beta</li></ul><script>var x = 1;</script></body>");
    EXPECT_TRUE(parsed.ok());
    const HtmlTextResult result = extractHtmlText(*parsed.value().findDescendant("body"));
    EXPECT_EQ(result.text,
              std::string("Title Here\n\nFirst paragraph with spacing.\n\nSecond bold text.\n\n"
                          "\xE2\x80\xA2 alpha\n\xE2\x80\xA2 beta"));
    EXPECT_EQ(result.headings.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(result.headings[0].title, std::string("Title Here"));
    EXPECT_EQ(result.headings[0].offset, static_cast<std::size_t>(0));
}
