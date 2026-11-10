#include "export/DocxExporter.hpp"

#include <sstream>

#include "common/Strings.hpp"
#include "doc/Zip.hpp"
#include "export/ImageInfo.hpp"

namespace eden {

namespace {

constexpr const char* kNs =
    "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
    "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"";

class DocxBuilder {
public:
    std::string body;
    std::vector<std::pair<std::string, Bytes>> media;
    std::vector<std::string> mediaRelationships;
    int numberedLists = 0;
    int imageCounter = 0;

    static std::string run(const std::string& text, bool bold = false, bool italic = false, const std::string& color = "",
                           int halfPoints = 0, const char* font = nullptr) {
        std::string props;
        if (font != nullptr) props += std::string("<w:rFonts w:ascii=\"") + font + "\" w:hAnsi=\"" + font + "\"/>";
        if (bold) props += "<w:b/>";
        if (italic) props += "<w:i/>";
        if (!color.empty()) props += "<w:color w:val=\"" + color + "\"/>";
        if (halfPoints > 0) props += "<w:sz w:val=\"" + std::to_string(halfPoints) + "\"/>";
        std::string out = "<w:r>";
        if (!props.empty()) out += "<w:rPr>" + props + "</w:rPr>";
        std::size_t start = 0;
        while (true) {
            const std::size_t newline = text.find('\n', start);
            const std::string piece = text.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
            out += "<w:t xml:space=\"preserve\">" + xmlSafe(piece) + "</w:t>";
            if (newline == std::string::npos) break;
            out += "<w:br/>";
            start = newline + 1;
        }
        return out + "</w:r>";
    }

    void paragraph(const std::string& style, const std::string& runs, const std::string& extraProps = "") {
        body += "<w:p><w:pPr>";
        if (!style.empty()) body += "<w:pStyle w:val=\"" + style + "\"/>";
        body += extraProps + "</w:pPr>" + runs + "</w:p>";
    }

    void pageBreak() { body += "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>"; }

    void bullet(const std::string& runs) { paragraph("ListParagraph", runs, "<w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/></w:numPr>"); }

    void numbered(const std::string& runs, int listId) {
        paragraph("ListParagraph", runs, "<w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"" + std::to_string(listId) + "\"/></w:numPr>");
    }

    bool image(const ExportImage& item) {
        auto loaded = loadImage(item.path);
        if (!loaded.ok()) return false;
        const ImageData& data = loaded.value();
        ++imageCounter;
        const std::string name = "image" + std::to_string(imageCounter) + "." + (data.extension == "jpeg" ? "jpg" : data.extension);
        const std::string rid = "rIdImg" + std::to_string(imageCounter);
        media.emplace_back("word/media/" + name, data.bytes);
        mediaRelationships.push_back("<Relationship Id=\"" + rid + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/" + name + "\"/>");
        const double maxWidth = 5.6 * 914400.0;
        double width = data.width * 9525.0;
        double height = data.height * 9525.0;
        if (width > maxWidth) {
            height *= maxWidth / width;
            width = maxWidth;
        }
        const long long cx = static_cast<long long>(width);
        const long long cy = static_cast<long long>(height);
        std::ostringstream drawing;
        drawing << "<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"" << cx << "\" cy=\"" << cy
                << "\"/><wp:docPr id=\"" << imageCounter << "\" name=\"Picture " << imageCounter << "\" descr=\"" << xmlSafe(item.caption)
                << "\"/><wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr><a:graphic><a:graphicData "
                   "uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic><pic:nvPicPr><pic:cNvPr id=\""
                << imageCounter << "\" name=\"" << name << "\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"" << rid
                << "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" << cx << "\" cy=\"" << cy
                << "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>";
        paragraph("", drawing.str(), "<w:jc w:val=\"center\"/>");
        if (!item.caption.empty()) paragraph("Caption", run(item.caption, false, true));
        return true;
    }
};

std::string stylesXml() {
    auto style = [](const std::string& id, const std::string& name, const std::string& basedOn, const std::string& ppr, const std::string& rpr, bool next = true) {
        return "<w:style w:type=\"paragraph\" w:styleId=\"" + id + "\"><w:name w:val=\"" + name + "\"/>" +
               (basedOn.empty() ? "" : "<w:basedOn w:val=\"" + basedOn + "\"/>") + (next ? "<w:next w:val=\"Normal\"/>" : "") +
               "<w:qFormat/><w:pPr>" + ppr + "</w:pPr><w:rPr>" + rpr + "</w:rPr></w:style>";
    };
    std::string out = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:styles ") + kNs + ">";
    out += "<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Georgia\" w:hAnsi=\"Georgia\" w:cs=\"Georgia\"/><w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/></w:rPr></w:rPrDefault>"
           "<w:pPrDefault><w:pPr><w:spacing w:after=\"120\" w:line=\"288\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults>";
    out += "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>";
    out += style("Title", "Title", "Normal", "<w:spacing w:before=\"2400\" w:after=\"240\"/><w:jc w:val=\"center\"/>", "<w:b/><w:color w:val=\"8A6A0A\"/><w:sz w:val=\"64\"/>");
    out += style("Subtitle", "Subtitle", "Normal", "<w:jc w:val=\"center\"/>", "<w:i/><w:color w:val=\"5C5240\"/><w:sz w:val=\"28\"/>");
    out += style("Heading1", "heading 1", "Normal", "<w:keepNext/><w:spacing w:before=\"360\" w:after=\"120\"/><w:pBdr><w:bottom w:val=\"single\" w:sz=\"8\" w:space=\"4\" w:color=\"8A6A0A\"/></w:pBdr><w:outlineLvl w:val=\"0\"/>", "<w:b/><w:color w:val=\"8A6A0A\"/><w:sz w:val=\"36\"/>");
    out += style("Heading2", "heading 2", "Normal", "<w:keepNext/><w:spacing w:before=\"240\" w:after=\"80\"/><w:outlineLvl w:val=\"1\"/>", "<w:b/><w:color w:val=\"1D4FB3\"/><w:sz w:val=\"28\"/>");
    out += style("Heading3", "heading 3", "Normal", "<w:keepNext/><w:spacing w:before=\"160\" w:after=\"60\"/><w:outlineLvl w:val=\"2\"/>", "<w:b/><w:color w:val=\"6A38B0\"/><w:sz w:val=\"24\"/>");
    out += style("Quote", "Quote", "Normal", "<w:pBdr><w:left w:val=\"single\" w:sz=\"24\" w:space=\"10\" w:color=\"A3173F\"/></w:pBdr><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"EFE8D8\"/><w:ind w:left=\"360\" w:right=\"240\"/>", "<w:i/>");
    out += style("ListParagraph", "List Paragraph", "Normal", "<w:spacing w:after=\"60\"/><w:ind w:left=\"720\"/>", "", false);
    out += style("Caption", "caption", "Normal", "<w:jc w:val=\"center\"/>", "<w:i/><w:color w:val=\"5C5240\"/><w:sz w:val=\"18\"/>", false);
    out += style("Meta", "Meta", "Normal", "<w:spacing w:after=\"60\"/>", "<w:color w:val=\"5C5240\"/><w:sz w:val=\"20\"/>", false);
    out += style("Code", "Code", "Normal", "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"EFE8D8\"/>", "<w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\"/><w:sz w:val=\"19\"/>", false);
    return out + "</w:styles>";
}

std::string numberingXml(int numberedLists) {
    std::string out = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:numbering ") + kNs + ">";
    out += "<w:abstractNum w:abstractNumId=\"0\"><w:multiLevelType w:val=\"hybridMultilevel\"/><w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"bullet\"/>"
           "<w:lvlText w:val=\"\xE2\x80\xA2\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr></w:lvl></w:abstractNum>";
    out += "<w:abstractNum w:abstractNumId=\"1\"><w:multiLevelType w:val=\"hybridMultilevel\"/><w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/>"
           "<w:lvlText w:val=\"%1.\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr></w:lvl></w:abstractNum>";
    out += "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>";
    for (int i = 0; i < numberedLists; ++i) {
        out += "<w:num w:numId=\"" + std::to_string(i + 2) + "\"><w:abstractNumId w:val=\"1\"/><w:lvlOverride w:ilvl=\"0\"><w:startOverride w:val=\"1\"/></w:lvlOverride></w:num>";
    }
    return out + "</w:numbering>";
}

void renderBlocks(DocxBuilder& doc, const std::vector<RenderBlock>& blocks) {
    int currentList = 0;
    for (const RenderBlock& block : blocks) {
        if (block.kind != BlockKind::Numbered) currentList = 0;
        switch (block.kind) {
            case BlockKind::Heading: doc.paragraph("Heading3", DocxBuilder::run(block.text)); break;
            case BlockKind::Paragraph: doc.paragraph("", DocxBuilder::run(block.text)); break;
            case BlockKind::Bullet: doc.bullet(DocxBuilder::run(block.text)); break;
            case BlockKind::Numbered: {
                if (currentList == 0) currentList = ++doc.numberedLists + 1;
                std::string runs = DocxBuilder::run(block.text);
                if (!block.detail.empty()) runs += DocxBuilder::run("  Hint: " + block.detail, false, true, "5C5240");
                doc.numbered(runs, currentList);
                break;
            }
            case BlockKind::Quote: {
                std::string runs = DocxBuilder::run(block.text);
                if (!block.detail.empty()) runs += DocxBuilder::run("\n" + block.detail, false, false, "5C5240", 20);
                doc.paragraph("Quote", runs);
                break;
            }
            case BlockKind::KeyValue: {
                std::string runs = DocxBuilder::run(block.text, true, false, "8A6A0A");
                if (!block.detail.empty()) runs += DocxBuilder::run("\n" + block.detail);
                doc.paragraph("", runs, "<w:ind w:left=\"360\"/>");
                break;
            }
            case BlockKind::Code: doc.paragraph("Code", DocxBuilder::run(block.text)); break;
            case BlockKind::Badge:
                doc.paragraph("", DocxBuilder::run("[" + block.text + "]", true, false, "0F7A50") + DocxBuilder::run("  " + block.detail, false, false, "5C5240"));
                break;
        }
    }
}

}  // namespace

Result<Bytes> exportDocx(const ExportBundle& bundle, const ExportOptions& options) {
    DocxBuilder doc;
    doc.paragraph("Title", DocxBuilder::run(bundle.title));
    if (!bundle.subtitle.empty()) doc.paragraph("Subtitle", DocxBuilder::run(bundle.subtitle));
    if (!bundle.author.empty()) doc.paragraph("Subtitle", DocxBuilder::run("Prepared by " + bundle.author));
    doc.paragraph("Meta", DocxBuilder::run("Generated " + bundle.generatedAt + " by Eden Research Reader"), "<w:jc w:val=\"center\"/>");
    doc.pageBreak();
    int index = 0;
    for (const ExportCard& card : bundle.cards) {
        doc.paragraph("Heading1", DocxBuilder::run(std::to_string(++index) + ". " + (card.chapter.empty() ? card.documentTitle : card.chapter)));
        doc.paragraph("Meta", DocxBuilder::run(card.documentTitle + " \xC2\xB7 page " + std::to_string(card.page)));
        doc.paragraph("Quote", DocxBuilder::run(card.passage));
        if (!card.summary.empty()) {
            doc.paragraph("Heading2", DocxBuilder::run("Summary"));
            doc.paragraph("", DocxBuilder::run(card.summary));
        }
        if (options.includeImages) {
            for (const ExportImage& image : card.images) doc.image(image);
        }
        for (const ExportSection& section : card.sections) {
            doc.paragraph("Heading2", DocxBuilder::run(section.title));
            renderBlocks(doc, section.blocks);
        }
        if (options.includeSources && !card.sources.empty()) {
            doc.paragraph("Heading2", DocxBuilder::run("Sources"));
            for (const Source& source : card.sources) {
                doc.bullet(DocxBuilder::run(source.title.empty() ? source.url : source.title, true) + DocxBuilder::run("  " + source.url, false, false, "1D4FB3", 20));
            }
        }
        if (!card.comments.empty()) {
            doc.paragraph("Heading2", DocxBuilder::run("Discussion"));
            for (const ExportComment& comment : card.comments) {
                std::string runs = DocxBuilder::run(comment.author, true);
                if (comment.writerOriginal) runs += DocxBuilder::run("  \xE2\x9C\xA6 Author", true, false, "8A6A0A");
                runs += DocxBuilder::run(": " + (comment.body.empty() ? "[" + comment.kind + " attachment]" : comment.body));
                doc.paragraph("", runs, "<w:ind w:left=\"" + std::to_string(360 + comment.depth * 360) + "\"/>");
            }
        }
        if (index < static_cast<int>(bundle.cards.size())) doc.pageBreak();
    }

    ZipWriter zip;
    zip.addText("[Content_Types].xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                "<Default Extension=\"xml\" ContentType=\"application/xml\"/><Default Extension=\"png\" ContentType=\"image/png\"/>"
                "<Default Extension=\"jpg\" ContentType=\"image/jpeg\"/>"
                "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
                "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
                "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>"
                "<Override PartName=\"/word/footer1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>"
                "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/></Types>");
    zip.addText("_rels/.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
                "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/></Relationships>");
    std::string relationships =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rIdStyles\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "<Relationship Id=\"rIdNumbering\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/>"
        "<Relationship Id=\"rIdFooter\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\" Target=\"footer1.xml\"/>";
    for (const std::string& item : doc.mediaRelationships) relationships += item;
    zip.addText("word/_rels/document.xml.rels", relationships + "</Relationships>");
    zip.addText("word/document.xml", std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:document ") + kNs + "><w:body>" + doc.body +
                                         "<w:sectPr><w:footerReference w:type=\"default\" r:id=\"rIdFooter\"/><w:pgSz w:w=\"12240\" w:h=\"15840\"/>"
                                         "<w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>");
    zip.addText("word/styles.xml", stylesXml());
    zip.addText("word/numbering.xml", numberingXml(doc.numberedLists));
    zip.addText("word/footer1.xml", std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:ftr ") + kNs +
                                        "><w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr><w:r><w:rPr><w:color w:val=\"5C5240\"/><w:sz w:val=\"18\"/></w:rPr><w:t xml:space=\"preserve\">Eden Research Reader \xC2\xB7 page </w:t></w:r>"
                                        "<w:r><w:rPr><w:color w:val=\"5C5240\"/><w:sz w:val=\"18\"/></w:rPr><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> PAGE </w:instrText></w:r>"
                                        "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t>1</w:t></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p></w:ftr>");
    zip.addText("docProps/core.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
                "<dc:title>" + xmlSafe(bundle.title) + "</dc:title><dc:creator>" + xmlSafe(bundle.author.empty() ? "Eden Research Reader" : bundle.author) +
                "</dc:creator><dcterms:created xsi:type=\"dcterms:W3CDTF\">" + xmlSafe(bundle.generatedAt) + "</dcterms:created></cp:coreProperties>");
    for (auto& [name, bytes] : doc.media) zip.add(name, bytes);
    return zip.finish();
}

}  // namespace eden
