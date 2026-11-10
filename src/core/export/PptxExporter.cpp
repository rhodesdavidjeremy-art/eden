#include "export/PptxExporter.hpp"

#include <sstream>

#include "app/ThemeManager.hpp"
#include "common/Strings.hpp"
#include "doc/Zip.hpp"
#include "export/ImageInfo.hpp"
#include "export/SlideDeck.hpp"

namespace eden {

namespace {

constexpr const char* kPptxNs =
    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
    "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"";
constexpr long long kSlideWidth = 12192000;
constexpr long long kSlideHeight = 6858000;

std::string hexNoHash(const std::string& color) { return color.size() == 7 ? toUpperAscii(color.substr(1)) : "000000"; }

std::string textBox(int id, const std::string& name, long long x, long long y, long long cx, long long cy, const std::string& paragraphs) {
    std::ostringstream out;
    out << "<p:sp><p:nvSpPr><p:cNvPr id=\"" << id << "\" name=\"" << name << "\"/><p:cNvSpPr txBox=\"1\"/><p:nvPr/></p:nvSpPr><p:spPr><a:xfrm><a:off x=\"" << x
        << "\" y=\"" << y << "\"/><a:ext cx=\"" << cx << "\" cy=\"" << cy << "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:noFill/></p:spPr>"
        << "<p:txBody><a:bodyPr wrap=\"square\" rtlCol=\"0\"><a:normAutofit/></a:bodyPr><a:lstStyle/>" << paragraphs << "</p:txBody></p:sp>";
    return out.str();
}

std::string paragraph(const std::string& text, int sizeHundredths, const std::string& color, bool bold, bool bullet, const std::string& align = "l") {
    std::string out = "<a:p><a:pPr algn=\"" + align + "\"";
    if (bullet) out += " marL=\"342900\" indent=\"-342900\"";
    out += ">";
    if (bullet) out += "<a:buClr><a:srgbClr val=\"" + color + "\"/></a:buClr><a:buFont typeface=\"Arial\"/><a:buChar char=\"&#8226;\"/>";
    else out += "<a:buNone/>";
    out += "</a:pPr><a:r><a:rPr lang=\"en-US\" sz=\"" + std::to_string(sizeHundredths) + "\" b=\"" + (bold ? "1" : "0") + "\" dirty=\"0\"><a:solidFill><a:srgbClr val=\"" + color +
           "\"/></a:solidFill><a:latin typeface=\"Georgia\"/></a:rPr><a:t>" + xmlSafe(text) + "</a:t></a:r></a:p>";
    return out;
}

std::string themeXml() {
    std::string out = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"Eden\">"
                                  "<a:themeElements><a:clrScheme name=\"Eden\">");
    const char* names[] = {"dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink"};
    const char* values[] = {"12060A", "FFFFFF", "2B1019", "F7F3EA", "E3B94A", "E0486F", "3FBF8A", "5B8DEF", "A77BE0", "F4EFE6", "5B8DEF", "A77BE0"};
    for (int i = 0; i < 12; ++i) out += std::string("<a:") + names[i] + "><a:srgbClr val=\"" + values[i] + "\"/></a:" + names[i] + ">";
    out += "</a:clrScheme><a:fontScheme name=\"Eden\"><a:majorFont><a:latin typeface=\"Georgia\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont>"
           "<a:minorFont><a:latin typeface=\"Georgia\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont></a:fontScheme><a:fmtScheme name=\"Eden\"><a:fillStyleLst>"
           "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:fillStyleLst>"
           "<a:lnStyleLst><a:ln w=\"6350\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln><a:ln w=\"12700\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln>"
           "<a:ln w=\"19050\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln></a:lnStyleLst><a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle>"
           "<a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst><a:bgFillStyleLst>"
           "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
           "</a:bgFillStyleLst></a:fmtScheme></a:themeElements></a:theme>";
    return out;
}

}  // namespace

Result<Bytes> exportPptx(const ExportBundle& bundle, const ExportOptions& options) {
    const Palette* found = findPalette(options.palette == "pearl-light" ? "ruby-night" : options.palette);
    const Palette& pal = found != nullptr ? *found : builtinPalettes().front();
    const std::vector<Slide> deck = buildSlideDeck(bundle, options);
    if (deck.empty()) return makeError(ErrorCode::InvalidArgument, "no slides to export");

    ZipWriter zip;
    std::string contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/><Default Extension=\"jpg\" ContentType=\"image/jpeg\"/>"
        "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
        "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
        "<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/>"
        "<Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>"
        "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>";
    std::string presentationRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster\" Target=\"slideMasters/slideMaster1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" Target=\"theme/theme1.xml\"/>";
    std::string slideIds;

    int imageCounter = 0;
    for (std::size_t i = 0; i < deck.size(); ++i) {
        const Slide& slide = deck[i];
        const int number = static_cast<int>(i) + 1;
        std::string shapes;
        std::string slideRels =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>";
        const std::string gold = hexNoHash(pal.gold);
        const std::string text = hexNoHash(pal.text);
        if (slide.isTitle) {
            shapes += textBox(2, "Title", 914400, 2057400, kSlideWidth - 1828800, 1371600, paragraph(slide.title, 4800, gold, true, false, "ctr"));
            std::string subs;
            for (const std::string& line : slide.bullets) subs += paragraph(line, 2200, hexNoHash(pal.mutedText), false, false, "ctr");
            if (!subs.empty()) shapes += textBox(3, "Subtitle", 914400, 3657600, kSlideWidth - 1828800, 1371600, subs);
        } else {
            shapes += textBox(2, "Title", 609600, 365760, kSlideWidth - 1219200, 914400, paragraph(slide.title, 3200, gold, true, false));
            std::string bullets;
            for (const std::string& line : slide.bullets) bullets += paragraph(line, 2000, text, false, true);
            const bool hasImage = !slide.imagePath.empty();
            auto loaded = hasImage ? loadImage(slide.imagePath) : Result<ImageData>(makeError(ErrorCode::NotFound, ""));
            const long long bodyWidth = loaded.ok() ? 6400800 : kSlideWidth - 1219200;
            shapes += textBox(3, "Body", 609600, 1371600, bodyWidth, 4800600, bullets.empty() ? "<a:p><a:endParaRPr lang=\"en-US\"/></a:p>" : bullets);
            if (loaded.ok()) {
                ++imageCounter;
                const std::string name = "image" + std::to_string(imageCounter) + "." + (loaded.value().extension == "jpeg" ? "jpg" : loaded.value().extension);
                zip.add("ppt/media/" + name, loaded.value().bytes);
                slideRels += "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"../media/" + name + "\"/>";
                const long long boxW = 4572000;
                const long long boxH = 4114800;
                double w = loaded.value().width;
                double h = loaded.value().height;
                const double scale = std::min(boxW / w, boxH / h);
                const long long cx = static_cast<long long>(w * scale);
                const long long cy = static_cast<long long>(h * scale);
                shapes += "<p:pic><p:nvPicPr><p:cNvPr id=\"4\" name=\"Picture\"/><p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr><p:nvPr/></p:nvPicPr>"
                          "<p:blipFill><a:blip r:embed=\"rId2\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr><a:xfrm><a:off x=\"7010400\" y=\"1371600\"/><a:ext cx=\"" +
                          std::to_string(cx) + "\" cy=\"" + std::to_string(cy) + "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr></p:pic>";
            }
        }
        const std::string background = hexNoHash(slide.isTitle ? pal.surfaceAlt : pal.background);
        zip.addText("ppt/slides/slide" + std::to_string(number) + ".xml",
                    std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><p:sld ") + kPptxNs + "><p:cSld><p:bg><p:bgPr><a:solidFill><a:srgbClr val=\"" + background +
                        "\"/></a:solidFill><a:effectLst/></p:bgPr></p:bg><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
                        "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>" +
                        shapes + "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>");
        zip.addText("ppt/slides/_rels/slide" + std::to_string(number) + ".xml.rels", slideRels + "</Relationships>");
        contentTypes += "<Override PartName=\"/ppt/slides/slide" + std::to_string(number) +
                        ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>";
        presentationRels += "<Relationship Id=\"rId" + std::to_string(number + 2) + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" Target=\"slides/slide" +
                            std::to_string(number) + ".xml\"/>";
        slideIds += "<p:sldId id=\"" + std::to_string(255 + number) + "\" r:id=\"rId" + std::to_string(number + 2) + "\"/>";
    }

    zip.addText("[Content_Types].xml", contentTypes + "</Types>");
    zip.addText("_rels/.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"ppt/presentation.xml\"/>"
                "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/></Relationships>");
    zip.addText("ppt/_rels/presentation.xml.rels", presentationRels + "</Relationships>");
    zip.addText("ppt/presentation.xml", std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><p:presentation ") + kPptxNs +
                                            "><p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst><p:sldIdLst>" + slideIds +
                                            "</p:sldIdLst><p:sldSz cx=\"12192000\" cy=\"6858000\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/></p:presentation>");
    zip.addText("ppt/slideMasters/slideMaster1.xml",
                std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><p:sldMaster ") + kPptxNs +
                    "><p:cSld><p:bg><p:bgPr><a:solidFill><a:srgbClr val=\"" + hexNoHash(pal.background) +
                    "\"/></a:solidFill><a:effectLst/></p:bgPr></p:bg><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
                    "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr></p:spTree></p:cSld>"
                    "<p:clrMap bg1=\"dk1\" tx1=\"lt1\" bg2=\"dk2\" tx2=\"lt2\" accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" "
                    "accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/><p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/></p:sldLayoutIdLst></p:sldMaster>");
    zip.addText("ppt/slideMasters/_rels/slideMaster1.xml.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>"
                "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" Target=\"../theme/theme1.xml\"/></Relationships>");
    zip.addText("ppt/slideLayouts/slideLayout1.xml",
                std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><p:sldLayout ") + kPptxNs +
                    " type=\"blank\" preserve=\"1\"><p:cSld name=\"Blank\"><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
                    "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr></p:spTree></p:cSld>"
                    "<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>");
    zip.addText("ppt/slideLayouts/_rels/slideLayout1.xml.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster\" Target=\"../slideMasters/slideMaster1.xml\"/></Relationships>");
    zip.addText("ppt/theme/theme1.xml", themeXml());
    zip.addText("docProps/core.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                "xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>" + xmlSafe(bundle.title) + "</dc:title><dc:creator>" +
                    xmlSafe(bundle.author.empty() ? "Eden Research Reader" : bundle.author) + "</dc:creator></cp:coreProperties>");
    return zip.finish();
}

}  // namespace eden
