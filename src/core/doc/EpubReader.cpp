#include "doc/EpubReader.hpp"

#include <map>

#include "common/Strings.hpp"
#include "doc/HtmlText.hpp"
#include "doc/TextUtil.hpp"
#include "doc/Xml.hpp"
#include "doc/Zip.hpp"

namespace eden {

namespace {

struct ManifestItem {
    std::string href;
    std::string mediaType;
    std::string properties;
};

std::string directoryOf(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

}  // namespace

std::string percentDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const char high = text[i + 1];
            const char low = text[i + 2];
            auto hexValue = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int a = hexValue(high);
            const int b = hexValue(low);
            if (a >= 0 && b >= 0) {
                out.push_back(static_cast<char>(a * 16 + b));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string resolveArchivePath(const std::string& baseDirectory, const std::string& reference) {
    std::string target = percentDecode(reference);
    const std::size_t fragment = target.find('#');
    if (fragment != std::string::npos) target = target.substr(0, fragment);
    std::string combined = (!target.empty() && target[0] == '/') ? target.substr(1) : baseDirectory + target;
    std::vector<std::string> parts;
    for (const std::string& piece : split(combined, '/', false)) {
        if (piece == ".") continue;
        if (piece == "..") {
            if (!parts.empty()) parts.pop_back();
            continue;
        }
        parts.push_back(piece);
    }
    return join(parts, "/");
}

Result<ExtractedDocument> readEpubDocument(const Bytes& data, const std::string& fileName) {
    auto zipResult = ZipReader::open(data);
    if (!zipResult.ok()) return zipResult.error();
    const ZipReader& zip = zipResult.value();

    auto container = zip.readText("META-INF/container.xml");
    if (!container.ok()) return makeError(ErrorCode::Parse, "epub is missing META-INF/container.xml");
    auto containerXml = parseXml(container.value());
    if (!containerXml.ok()) return containerXml.error();
    const XmlNode* rootfile = containerXml.value().findDescendant("rootfile");
    if (rootfile == nullptr) return makeError(ErrorCode::Parse, "epub container lists no package document");
    const std::string opfPath = resolveArchivePath("", rootfile->attr("full-path"));

    auto opfText = zip.readText(opfPath);
    if (!opfText.ok()) return makeError(ErrorCode::Parse, "epub package document missing: " + opfPath);
    auto opf = parseXml(opfText.value());
    if (!opf.ok()) return opf.error();
    const std::string baseDirectory = directoryOf(opfPath);

    ExtractedDocument doc;
    doc.type = FileType::Epub;
    if (const XmlNode* title = opf.value().findDescendant("title")) doc.title = collapseWhitespace(title->textContent());
    if (const XmlNode* creator = opf.value().findDescendant("creator")) {
        doc.author = collapseWhitespace(creator->textContent());
    }
    if (const XmlNode* language = opf.value().findDescendant("language")) {
        doc.language = collapseWhitespace(language->textContent());
    }

    std::map<std::string, ManifestItem> manifest;
    std::vector<const XmlNode*> items;
    opf.value().collectDescendants("item", items);
    for (const XmlNode* item : items) {
        ManifestItem entry;
        entry.href = item->attr("href");
        entry.mediaType = item->attr("media-type");
        entry.properties = item->attr("properties");
        manifest[item->attr("id")] = std::move(entry);
    }

    std::vector<std::string> spine;
    std::vector<const XmlNode*> refs;
    opf.value().collectDescendants("itemref", refs);
    for (const XmlNode* ref : refs) {
        if (ref->attr("linear") == "no") continue;
        spine.push_back(ref->attr("idref"));
    }
    if (spine.empty()) return makeError(ErrorCode::Parse, "epub has an empty reading order");

    std::size_t sectionNumber = 0;
    for (const std::string& id : spine) {
        auto found = manifest.find(id);
        if (found == manifest.end()) {
            doc.warnings.push_back("spine item missing from manifest: " + id);
            continue;
        }
        const ManifestItem& item = found->second;
        const bool markup = item.mediaType.find("html") != std::string::npos ||
                            item.mediaType.find("xml") != std::string::npos;
        if (!markup || item.properties.find("nav") != std::string::npos) continue;
        const std::string path = resolveArchivePath(baseDirectory, item.href);
        auto content = zip.readText(path);
        if (!content.ok()) {
            doc.warnings.push_back("could not read " + path + ": " + content.error().message);
            continue;
        }
        auto parsed = parseXml(content.value());
        if (!parsed.ok()) {
            doc.warnings.push_back("could not parse " + path + ": " + parsed.error().message);
            continue;
        }
        const XmlNode* body = parsed.value().findDescendant("body");
        HtmlTextResult extracted = extractHtmlText(body != nullptr ? *body : parsed.value());
        extracted.text = trim(extracted.text);
        if (extracted.text.empty()) continue;
        ++sectionNumber;

        if (!doc.text.empty()) doc.text += "\n\n";
        const std::int64_t sectionStart = static_cast<std::int64_t>(doc.text.size());
        doc.text += extracted.text;
        const std::int64_t sectionEnd = static_cast<std::int64_t>(doc.text.size());

        bool markedFirst = false;
        for (const HtmlHeading& heading : extracted.headings) {
            if (heading.offset > extracted.text.size()) continue;
            if (heading.level > 3 && markedFirst) continue;
            ChapterMark mark;
            mark.title = heading.title;
            mark.offset = sectionStart + static_cast<std::int64_t>(heading.offset);
            mark.level = heading.level;
            doc.chapters.push_back(std::move(mark));
            markedFirst = true;
        }
        if (!markedFirst) {
            ChapterMark mark;
            mark.title = "Section " + std::to_string(sectionNumber);
            mark.offset = sectionStart;
            mark.level = 1;
            doc.chapters.push_back(std::move(mark));
        }
        paginateByLength(doc.text, sectionStart, sectionEnd, 4000,
                         static_cast<std::int64_t>(doc.pages.size()), doc.pages);
    }
    if (doc.text.empty()) return makeError(ErrorCode::InvalidArgument, "epub contains no readable text");
    if (doc.title.empty()) doc.title = fileStem(fileName);
    return doc;
}

}  // namespace eden
