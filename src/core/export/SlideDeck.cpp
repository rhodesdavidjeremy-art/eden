#include "export/SlideDeck.hpp"

#include <sstream>

#include "app/ThemeManager.hpp"
#include "common/Strings.hpp"
#include "export/SimpleExporters.hpp"

namespace eden {

namespace {

std::string firstSentences(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text;
    const std::size_t cut = text.rfind(' ', limit);
    return text.substr(0, cut == std::string::npos ? limit : cut) + "...";
}

void addBullets(Slide& slide, const ExportSection& section, std::size_t maxBullets) {
    for (const RenderBlock& block : section.blocks) {
        if (slide.bullets.size() >= maxBullets) break;
        if (block.kind == BlockKind::Bullet || block.kind == BlockKind::Numbered) slide.bullets.push_back(block.text);
        else if (block.kind == BlockKind::KeyValue) slide.bullets.push_back(block.text + (block.detail.empty() ? "" : ": " + firstSentences(replaceAll(block.detail, "\n", " "), 110)));
    }
}

}  // namespace

std::vector<Slide> buildSlideDeck(const ExportBundle& bundle, const ExportOptions& options) {
    std::vector<Slide> deck;
    Slide cover;
    cover.title = bundle.title;
    cover.isTitle = true;
    if (!bundle.subtitle.empty()) cover.bullets.push_back(bundle.subtitle);
    if (!bundle.author.empty()) cover.bullets.push_back("Prepared by " + bundle.author);
    deck.push_back(std::move(cover));
    for (const ExportCard& card : bundle.cards) {
        const std::string heading = card.chapter.empty() ? card.documentTitle : card.chapter;
        const ExportSection* authored = nullptr;
        for (const ExportSection& section : card.sections) {
            if (section.functionId == "output.slides" && section.data.isArray()) authored = &section;
        }
        if (authored != nullptr) {
            for (const Json& item : authored->data.items()) {
                Slide slide;
                slide.title = item.getString("title");
                slide.bullets = item.getStringList("bullets");
                slide.notes = item.getString("notes");
                deck.push_back(std::move(slide));
            }
            continue;
        }
        Slide passage;
        passage.title = heading;
        passage.bullets.push_back("\"" + firstSentences(collapseWhitespace(card.passage), 260) + "\"");
        passage.notes = card.passage;
        if (options.includeImages && !card.images.empty()) passage.imagePath = card.images.front().path;
        deck.push_back(std::move(passage));
        if (!card.summary.empty()) {
            Slide summary;
            summary.title = "Summary";
            summary.bullets.push_back(firstSentences(card.summary, 320));
            summary.notes = card.summary;
            deck.push_back(std::move(summary));
        }
        for (const ExportSection& section : card.sections) {
            if (section.functionId == "keypoints" || section.functionId == "glossary" || section.functionId == "expand.opposing" ||
                section.functionId == "assumptions" || section.functionId == "implications") {
                Slide slide;
                slide.title = section.title;
                addBullets(slide, section, 6);
                if (slide.bullets.empty()) slide.bullets.push_back(firstSentences(section.text, 300));
                deck.push_back(std::move(slide));
            }
        }
    }
    return deck;
}

std::string exportHtmlSlides(const ExportBundle& bundle, const ExportOptions& options) {
    const Palette* found = findPalette(options.palette == "pearl-light" ? "ruby-night" : options.palette);
    const Palette& p = found != nullptr ? *found : builtinPalettes().front();
    const std::vector<Slide> deck = buildSlideDeck(bundle, options);
    std::ostringstream out;
    out << "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><title>" << escapeHtml(bundle.title) << "</title>\n<style>\n"
        << "html,body{margin:0;height:100%;background:" << p.background << ";color:" << p.text << ";font-family:Georgia,serif;overflow:hidden}\n"
        << ".slide{display:none;position:absolute;inset:0;padding:6vh 8vw;box-sizing:border-box;background:linear-gradient(160deg," << p.surfaceAlt << "," << p.background << ");}\n"
        << ".slide.active{display:flex;flex-direction:column;justify-content:center}.slide h1,.slide h2{color:" << p.gold << ";margin:0 0 3vh}\n"
        << ".slide h1{font-size:5vw;border-bottom:3px solid " << p.gold << ";padding-bottom:2vh}.slide h2{font-size:3.4vw}\n"
        << "li{font-size:2.2vw;margin:1.4vh 0}img{max-height:42vh;max-width:60vw;border-radius:12px;margin-top:2vh;align-self:flex-start}\n"
        << ".counter{position:fixed;right:2vw;bottom:2vh;color:" << p.mutedText << ";font-size:1.4vw}\n"
        << "@media print{html,body{overflow:visible;height:auto}.slide{display:flex;position:relative;height:100vh;page-break-after:always}.counter{display:none}}\n"
        << "</style></head><body>\n";
    int index = 0;
    for (const Slide& slide : deck) {
        out << "<section class=\"slide" << (index == 0 ? " active" : "") << "\"><h" << (slide.isTitle ? 1 : 2) << ">" << escapeHtml(slide.title) << "</h"
            << (slide.isTitle ? 1 : 2) << ">\n";
        if (!slide.bullets.empty()) {
            out << "<ul>\n";
            for (const std::string& bullet : slide.bullets) out << "<li>" << escapeHtml(bullet) << "</li>\n";
            out << "</ul>\n";
        }
        if (!slide.imagePath.empty()) {
            const std::string uri = imageDataUri(slide.imagePath);
            if (!uri.empty()) out << "<img src=\"" << uri << "\" alt=\"\">\n";
        }
        out << "</section>\n";
        ++index;
    }
    out << "<div class=\"counter\" id=\"counter\"></div>\n<script>\n"
        << "const slides=[...document.querySelectorAll('.slide')];let at=0;\n"
        << "function show(i){at=Math.max(0,Math.min(slides.length-1,i));slides.forEach((s,n)=>s.classList.toggle('active',n===at));"
        << "document.getElementById('counter').textContent=(at+1)+' / '+slides.length;}\n"
        << "document.addEventListener('keydown',e=>{if(['ArrowRight','PageDown',' '].includes(e.key))show(at+1);"
        << "if(['ArrowLeft','PageUp'].includes(e.key))show(at-1);if(e.key==='Home')show(0);if(e.key==='End')show(slides.length-1);});\n"
        << "document.addEventListener('click',e=>show(e.clientX>window.innerWidth/2?at+1:at-1));show(0);\n</script>\n</body></html>\n";
    return out.str();
}

}  // namespace eden
