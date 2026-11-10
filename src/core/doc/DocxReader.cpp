#include "doc/DocxReader.hpp"

#include <map>

#include "common/Strings.hpp"
#include "doc/TextUtil.hpp"
#include "doc/Xml.hpp"
#include "doc/Zip.hpp"

namespace eden {

namespace {

class DocxExtractor {
public:
    DocxExtractor(ExtractedDocument& doc, std::map<std::string, std::string> styleNames)
        : doc_(doc), styleNames_(std::move(styleNames)) {}

    void extractBody(const XmlNode& body) {
        walkBlocks(body);
        finalizePages();
    }

private:
    int headingLevel(const XmlNode& paragraph) const {
        const XmlNode* properties = paragraph.firstChild("pPr");
        if (properties == nullptr) return 0;
        const XmlNode* style = properties->firstChild("pStyle");
        if (style == nullptr) return 0;
        const std::string id = style->attr("val");
        std::string name = toLowerAscii(id);
        auto it = styleNames_.find(id);
        if (it != styleNames_.end()) name = toLowerAscii(it->second);
        if (name == "title") return 1;
        if (startsWith(name, "heading")) {
            const std::string digits = trim(name.substr(7));
            if (!digits.empty() && digits[0] >= '1' && digits[0] <= '9') return digits[0] - '0';
        }
        return 0;
    }

    bool isListItem(const XmlNode& paragraph) const {
        const XmlNode* properties = paragraph.firstChild("pPr");
        return properties != nullptr && properties->firstChild("numPr") != nullptr;
    }

    void collectRuns(const XmlNode& node, std::string& out) {
        for (const XmlNode& child : node.children) {
            if (child.isText) continue;
            const std::string name = child.localName();
            if (name == "t") {
                out += child.textContent();
            } else if (name == "tab") {
                out.push_back('\t');
            } else if (name == "br" || name == "cr") {
                if (child.attr("type") == "page") {
                    pendingPageBreak_ = true;
                } else {
                    out.push_back('\n');
                }
            } else if (name == "noBreakHyphen") {
                out.push_back('-');
            } else if (name == "pPr" || name == "rPr" || name == "del" || name == "instrText") {
                continue;
            } else {
                collectRuns(child, out);
            }
        }
    }

    void appendBlock(const std::string& text, const std::string& prefix) {
        if (!doc_.text.empty()) doc_.text += "\n\n";
        if (pendingPageBreak_ && !doc_.text.empty()) {
            pageStarts_.push_back(static_cast<std::int64_t>(doc_.text.size()));
            pendingPageBreak_ = false;
        }
        doc_.text += prefix;
        doc_.text += text;
    }

    void paragraph(const XmlNode& node) {
        std::string text;
        collectRuns(node, text);
        text = trim(text);
        if (text.empty()) return;
        const int level = headingLevel(node);
        const bool listItem = isListItem(node);
        const std::string prefix = listItem && level == 0 ? "\xE2\x80\xA2 " : "";
        if (!doc_.text.empty() && listItem && lastWasList_) {
            doc_.text += "\n";
            doc_.text += prefix + text;
            lastWasList_ = true;
            return;
        }
        appendBlock(text, prefix);
        lastWasList_ = listItem;
        if (level > 0) {
            ChapterMark mark;
            mark.title = text;
            mark.offset = static_cast<std::int64_t>(doc_.text.size() - text.size());
            mark.level = level;
            doc_.chapters.push_back(std::move(mark));
        }
    }

    void table(const XmlNode& node) {
        std::string rendered;
        for (const XmlNode* row : node.childrenNamed("tr")) {
            std::vector<std::string> cells;
            for (const XmlNode* cell : row->childrenNamed("tc")) {
                std::vector<const XmlNode*> paragraphs;
                cell->collectDescendants("p", paragraphs);
                std::vector<std::string> parts;
                for (const XmlNode* p : paragraphs) {
                    std::string text;
                    collectRuns(*p, text);
                    text = trim(text);
                    if (!text.empty()) parts.push_back(text);
                }
                cells.push_back(join(parts, " "));
            }
            if (!rendered.empty()) rendered.push_back('\n');
            rendered += join(cells, " | ");
        }
        if (!trim(rendered).empty()) {
            appendBlock(rendered, "");
            lastWasList_ = false;
        }
    }

    void walkBlocks(const XmlNode& node) {
        for (const XmlNode& child : node.children) {
            if (child.isText) continue;
            const std::string name = child.localName();
            if (name == "p") {
                paragraph(child);
            } else if (name == "tbl") {
                table(child);
            } else if (name == "body" || name == "sdt" || name == "sdtContent" || name == "customXml" ||
                       name == "ins" || name == "txbxContent") {
                walkBlocks(child);
            }
        }
    }

    void finalizePages() {
        const std::int64_t total = static_cast<std::int64_t>(doc_.text.size());
        if (pageStarts_.empty()) {
            paginateByLength(doc_.text, 0, total, 3000, 0, doc_.pages);
            return;
        }
        std::int64_t start = 0;
        std::int64_t index = 0;
        for (std::int64_t boundary : pageStarts_) {
            if (boundary > start) {
                paginateByLength(doc_.text, start, boundary, 6000, index, doc_.pages);
                index = static_cast<std::int64_t>(doc_.pages.size());
                start = boundary;
            }
        }
        if (start < total) paginateByLength(doc_.text, start, total, 6000, index, doc_.pages);
    }

    ExtractedDocument& doc_;
    std::map<std::string, std::string> styleNames_;
    std::vector<std::int64_t> pageStarts_;
    bool pendingPageBreak_ = false;
    bool lastWasList_ = false;
};

std::map<std::string, std::string> readStyleNames(const ZipReader& zip) {
    std::map<std::string, std::string> names;
    auto styles = zip.readText("word/styles.xml");
    if (!styles.ok()) return names;
    auto parsed = parseXml(styles.value());
    if (!parsed.ok()) return names;
    std::vector<const XmlNode*> nodes;
    parsed.value().collectDescendants("style", nodes);
    for (const XmlNode* style : nodes) {
        const XmlNode* name = style->firstChild("name");
        if (name != nullptr) names[style->attr("styleId")] = name->attr("val");
    }
    return names;
}

void readCoreProperties(const ZipReader& zip, ExtractedDocument& doc) {
    auto core = zip.readText("docProps/core.xml");
    if (!core.ok()) return;
    auto parsed = parseXml(core.value());
    if (!parsed.ok()) return;
    if (const XmlNode* title = parsed.value().findDescendant("title")) doc.title = trim(title->textContent());
    if (const XmlNode* creator = parsed.value().findDescendant("creator")) doc.author = trim(creator->textContent());
    if (const XmlNode* language = parsed.value().findDescendant("language")) doc.language = trim(language->textContent());
}

}  // namespace

Result<ExtractedDocument> readDocxDocument(const Bytes& data, const std::string& fileName) {
    auto zip = ZipReader::open(data);
    if (!zip.ok()) return zip.error();
    auto documentXml = zip.value().readText("word/document.xml");
    if (!documentXml.ok()) return makeError(ErrorCode::Parse, "not a word document: " + documentXml.error().message);
    auto parsed = parseXml(documentXml.value());
    if (!parsed.ok()) return parsed.error();
    ExtractedDocument doc;
    doc.type = FileType::Docx;
    readCoreProperties(zip.value(), doc);
    const XmlNode* body = parsed.value().findDescendant("body");
    if (body == nullptr) return makeError(ErrorCode::Parse, "word document has no body");
    DocxExtractor extractor(doc, readStyleNames(zip.value()));
    extractor.extractBody(*body);
    doc.text = trim(doc.text);
    if (doc.text.empty()) return makeError(ErrorCode::InvalidArgument, "word document contains no text");
    if (doc.title.empty()) {
        doc.title = doc.chapters.empty() ? fileStem(fileName) : doc.chapters.front().title;
    }
    if (!doc.pages.empty() && doc.pages.back().end > doc.charCount()) doc.pages.back().end = doc.charCount();
    return doc;
}

}  // namespace eden
