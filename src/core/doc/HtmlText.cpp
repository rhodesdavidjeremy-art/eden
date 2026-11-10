#include "doc/HtmlText.hpp"

#include <algorithm>
#include <set>

#include "common/Strings.hpp"

namespace eden {

namespace {

int breakWeight(const std::string& name) {
    static const std::set<std::string> strong = {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "blockquote",
                                                  "section", "article", "ul", "ol", "table", "pre", "figure",
                                                  "aside", "header", "footer", "dl"};
    static const std::set<std::string> weak = {"li", "tr", "br", "dt", "dd", "hr", "figcaption", "caption"};
    if (strong.count(name)) return 2;
    if (weak.count(name)) return 1;
    return 0;
}

bool isSkipped(const XmlNode& node, const std::string& name) {
    if (name == "script" || name == "style" || name == "head" || name == "title" || name == "svg" ||
        name == "math" || name == "noscript") {
        return true;
    }
    const std::string type = node.attr("type");
    if (name == "nav" && (type == "toc" || type == "landmarks" || type == "page-list")) return true;
    return false;
}

class Extractor {
public:
    HtmlTextResult run(const XmlNode& root) {
        visit(root, false);
        while (!result_.text.empty() && (result_.text.back() == '\n' || result_.text.back() == ' ')) {
            result_.text.pop_back();
        }
        return std::move(result_);
    }

private:
    void requestBreak(int weight) { pending_ = std::max(pending_, weight); }

    void flushBreak() {
        if (pending_ > 0 && !result_.text.empty()) {
            std::size_t trailing = 0;
            while (trailing < result_.text.size() && result_.text[result_.text.size() - 1 - trailing] == '\n') {
                ++trailing;
            }
            const std::size_t wanted = static_cast<std::size_t>(pending_);
            if (trailing < wanted) result_.text.append(wanted - trailing, '\n');
        }
        pending_ = 0;
    }

    void emitText(const std::string& raw, bool preformatted) {
        std::string text = raw;
        if (!preformatted) {
            std::string collapsed;
            collapsed.reserve(text.size());
            bool space = false;
            for (char c : text) {
                if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
                    space = true;
                } else {
                    if (space && !collapsed.empty()) collapsed.push_back(' ');
                    space = false;
                    collapsed.push_back(c);
                }
            }
            const bool leadingSpace = !text.empty() && (text.front() == ' ' || text.front() == '\n' ||
                                                        text.front() == '\t' || text.front() == '\r');
            const bool trailingSpace = !text.empty() && (text.back() == ' ' || text.back() == '\n' ||
                                                         text.back() == '\t' || text.back() == '\r');
            if (collapsed.empty()) {
                if (!text.empty() && pending_ == 0 && !result_.text.empty() && result_.text.back() != ' ' &&
                    result_.text.back() != '\n') {
                    result_.text.push_back(' ');
                }
                return;
            }
            if (leadingSpace && pending_ == 0 && !result_.text.empty() && result_.text.back() != ' ' &&
                result_.text.back() != '\n') {
                result_.text.push_back(' ');
            }
            flushBreak();
            result_.text += collapsed;
            if (trailingSpace) result_.text.push_back(' ');
            return;
        }
        flushBreak();
        result_.text += text;
    }

    void visit(const XmlNode& node, bool preformatted) {
        if (node.isText) {
            emitText(node.text, preformatted);
            return;
        }
        const std::string name = toLowerAscii(node.localName());
        if (isSkipped(node, name)) return;
        const int weight = breakWeight(name);
        const bool heading = name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
        if (weight > 0) requestBreak(weight);
        const bool pre = preformatted || name == "pre";
        if (name == "li") {
            flushBreak();
            result_.text += "\xE2\x80\xA2 ";
        }
        std::size_t headingOffset = 0;
        if (heading) {
            requestBreak(2);
            flushBreak();
            headingOffset = result_.text.size();
        }
        for (const XmlNode& child : node.children) visit(child, pre);
        if (heading) {
            const std::string title = collapseWhitespace(node.textContent());
            if (!title.empty()) result_.headings.push_back({title, headingOffset, name[1] - '0'});
        }
        if (weight > 0) requestBreak(weight);
    }

    HtmlTextResult result_;
    int pending_ = 0;
};

}  // namespace

HtmlTextResult extractHtmlText(const XmlNode& root) {
    Extractor extractor;
    return extractor.run(root);
}

}  // namespace eden
