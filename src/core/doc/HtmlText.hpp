#pragma once

#include <string>
#include <vector>

#include "doc/ExtractedDocument.hpp"
#include "doc/Xml.hpp"

namespace eden {

struct HtmlHeading {
    std::string title;
    std::size_t offset = 0;
    int level = 1;
};

struct HtmlTextResult {
    std::string text;
    std::vector<HtmlHeading> headings;
};

HtmlTextResult extractHtmlText(const XmlNode& root);

}  // namespace eden
