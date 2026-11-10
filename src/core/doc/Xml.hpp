#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/Result.hpp"

namespace eden {

struct XmlNode {
    bool isText = false;
    std::string name;
    std::string text;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<XmlNode> children;

    std::string localName() const;
    const std::string* attribute(std::string_view name) const;
    std::string attr(std::string_view name, const std::string& fallback = "") const;
    const XmlNode* firstChild(std::string_view localName) const;
    std::vector<const XmlNode*> childrenNamed(std::string_view localName) const;
    const XmlNode* findDescendant(std::string_view localName) const;
    void collectDescendants(std::string_view localName, std::vector<const XmlNode*>& out) const;
    std::string textContent() const;
};

std::string decodeXmlEntities(std::string_view text);
Result<XmlNode> parseXml(std::string_view source);

}  // namespace eden
