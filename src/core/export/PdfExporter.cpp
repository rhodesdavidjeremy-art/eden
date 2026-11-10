#include "export/PdfExporter.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "app/ThemeManager.hpp"
#include "common/Strings.hpp"
#include "export/PdfWriter.hpp"

namespace eden {

namespace {

constexpr double kMargin = 54.0;
constexpr double kBottom = 66.0;

struct Style {
    bool illuminated = false;
    PdfColor page;
    PdfColor text;
    PdfColor muted;
    PdfColor heading;
    PdfColor rule;
    PdfColor passageBackground;
    PdfColor passageBar;
    PdfColor gold;
    PdfColor onColor{1, 1, 1};
    std::vector<PdfColor> jewels;
};

Style makeStyle(bool illuminated) {
    Style s;
    s.illuminated = illuminated;
    if (illuminated) {
        s.page = PdfColor::fromHex("#fbf6ea");
        s.text = PdfColor::fromHex("#2a2016");
        s.muted = PdfColor::fromHex("#6b5c40");
        s.heading = PdfColor::fromHex("#8a6a0a");
        s.rule = PdfColor::fromHex("#d4af37");
        s.passageBackground = PdfColor::fromHex("#f1e6c8");
        s.passageBar = PdfColor::fromHex("#a3173f");
        s.gold = PdfColor::fromHex("#d4af37");
        for (const char* hex : {"#a3173f", "#0f7a50", "#1d4fb3", "#6a38b0", "#b8860b", "#0b7285"}) s.jewels.push_back(PdfColor::fromHex(hex));
    } else {
        s.page = PdfColor{1, 1, 1};
        s.text = PdfColor{0.08, 0.08, 0.08};
        s.muted = PdfColor{0.4, 0.4, 0.4};
        s.heading = PdfColor{0.1, 0.1, 0.1};
        s.rule = PdfColor{0.7, 0.7, 0.7};
        s.passageBackground = PdfColor{0.95, 0.95, 0.95};
        s.passageBar = PdfColor{0.5, 0.5, 0.5};
        s.gold = PdfColor{0.3, 0.3, 0.3};
        s.jewels.push_back(PdfColor{0.25, 0.25, 0.25});
    }
    return s;
}

PdfColor groupColor(const Style& style, const std::string& group) {
    if (!style.illuminated) return style.jewels[0];
    static const std::map<std::string, std::size_t> order = {{"understand", 2}, {"evidence", 1}, {"expand", 3}, {"assist", 0}, {"visual", 4}, {"output", 5}};
    auto it = order.find(group);
    return style.jewels[it == order.end() ? 5 : it->second];
}

std::vector<std::string> wrapText(const std::string& text, PdfFace face, double size, double width) {
    std::vector<std::string> lines;
    for (const std::string& paragraph : split(text, '\n')) {
        std::string line;
        for (const std::string& word : split(paragraph, ' ', false)) {
            std::string remaining = word;
            while (PdfWriter::textWidth(remaining, face, size) > width && remaining.size() > 1) {
                std::size_t cut = remaining.size();
                while (cut > 1 && PdfWriter::textWidth(remaining.substr(0, cut), face, size) > width) {
                    --cut;
                    while (cut > 1 && (static_cast<unsigned char>(remaining[cut]) & 0xC0) == 0x80) --cut;
                }
                if (!line.empty()) {
                    lines.push_back(line);
                    line.clear();
                }
                lines.push_back(remaining.substr(0, cut));
                remaining = remaining.substr(cut);
            }
            const std::string candidate = line.empty() ? remaining : line + " " + remaining;
            if (!line.empty() && PdfWriter::textWidth(candidate, face, size) > width) {
                lines.push_back(line);
                line = remaining;
            } else {
                line = candidate;
            }
        }
        lines.push_back(line);
    }
    if (lines.size() > 1 && lines.back().empty()) lines.pop_back();
    return lines;
}

void drawGem(PdfWriter& pdf, double cx, double cy, double radius, const PdfColor& color, const PdfColor& rim) {
    const std::vector<std::pair<double, double>> diamond = {{cx, cy + radius}, {cx + radius * 0.8, cy}, {cx, cy - radius}, {cx - radius * 0.8, cy}};
    pdf.fillPolygon(diamond, color);
    pdf.fillPolygon({{cx, cy + radius}, {cx + radius * 0.8, cy}, {cx, cy}, {cx - radius * 0.8, cy}}, PdfColor::mix(color, PdfColor{1, 1, 1}, 0.35));
    pdf.strokePolygon(diamond, rim, 0.8);
}

void drawLogo(PdfWriter& pdf, double cx, double cy, double scale, const PdfColor& gold) {
    pdf.strokeCircle(cx, cy, 34 * scale, gold, 1.6);
    pdf.strokeCircle(cx, cy, 30 * scale, gold, 0.6);
    pdf.line(cx, cy - 20 * scale, cx, cy + 14 * scale, gold, 1.8);
    pdf.strokeBezier(cx, cy + 6 * scale, cx - 14 * scale, cy + 10 * scale, cx - 16 * scale, cy + 22 * scale, cx - 6 * scale, cy + 26 * scale, gold, 1.4);
    pdf.strokeBezier(cx, cy + 6 * scale, cx + 14 * scale, cy + 10 * scale, cx + 16 * scale, cy + 22 * scale, cx + 6 * scale, cy + 26 * scale, gold, 1.4);
    pdf.strokeBezier(cx, cy - 20 * scale, cx + 8 * scale, cy - 34 * scale, cx - 10 * scale, cy - 52 * scale, cx + 5 * scale, cy - 66 * scale, gold, 2.2);
}

class Flow {
public:
    Flow(PdfWriter& pdf, const Style& style, std::string label) : pdf_(pdf), style_(style), label_(std::move(label)) {}

    double width() const { return pdf_.pageWidth() - 2 * kMargin; }
    double cursor() const { return y_; }

    void startPage() {
        pdf_.newPage();
        const double w = pdf_.pageWidth();
        const double h = pdf_.pageHeight();
        if (style_.illuminated) {
            pdf_.fillRect(0, 0, w, h, style_.page);
            pdf_.strokeRect(24, 24, w - 48, h - 48, style_.gold, 1.6);
            pdf_.strokeRect(29, 29, w - 58, h - 58, style_.gold, 0.5);
            for (std::size_t i = 0; i < style_.jewels.size(); ++i) {
                drawGem(pdf_, w / 2 - 50 + static_cast<double>(i) * 20, 40, 5, style_.jewels[i], style_.gold);
            }
        } else {
            pdf_.line(kMargin, 52, w - kMargin, 52, style_.rule, 0.5);
        }
        const std::string number = std::to_string(pdf_.pageCount());
        pdf_.text(w - kMargin - PdfWriter::textWidth(number, PdfFace::Regular, 9), 38, number, PdfFace::Regular, 9, style_.muted);
        pdf_.text(kMargin, 38, label_, PdfFace::Regular, 9, style_.muted);
        y_ = h - kMargin;
    }

    void ensure(double height) {
        if (y_ - height < kBottom) startPage();
    }

    void space(double amount) { y_ -= amount; }

    void textLines(const std::vector<std::string>& lines, PdfFace face, double size, const PdfColor& color, double indent, double lineHeight = 1.38) {
        for (const std::string& line : lines) {
            ensure(size * lineHeight);
            y_ -= size * lineHeight;
            pdf_.text(kMargin + indent, y_ + size * 0.28, line, face, size, color);
        }
    }

    void paragraph(const std::string& text, PdfFace face, double size, const PdfColor& color, double indent = 0.0, double after = 4.0) {
        textLines(wrapText(text, face, size, width() - indent), face, size, color, indent);
        space(after);
    }

    void shaded(const std::string& text, PdfFace face, double size, const PdfColor& background, const PdfColor& bar, const PdfColor& color) {
        const double inner = width() - 22;
        for (const std::string& line : wrapText(text, face, size, inner)) {
            const double lh = size * 1.4;
            ensure(lh);
            y_ -= lh;
            pdf_.fillRect(kMargin, y_, width(), lh, background);
            pdf_.fillRect(kMargin, y_, 3.5, lh, bar);
            pdf_.text(kMargin + 14, y_ + size * 0.3, line, face, size, color);
        }
        space(6);
    }

    void headingBar(const std::string& title, const PdfColor& color, double size) {
        const double barHeight = size * 2.1;
        ensure(barHeight + 30);
        if (style_.illuminated) {
            y_ -= barHeight;
            pdf_.fillRect(kMargin, y_, width(), barHeight, color);
            pdf_.fillRect(kMargin, y_ - 2.5, width(), 1.4, style_.gold);
            drawGem(pdf_, kMargin + 14, y_ + barHeight / 2, 6, style_.gold, style_.onColor);
            pdf_.text(kMargin + 28, y_ + barHeight / 2 - size * 0.35, title, PdfFace::Bold, size, style_.onColor);
            space(10);
        } else {
            paragraph(title, PdfFace::Bold, size, style_.heading, 0, 2);
            pdf_.line(kMargin, y_, kMargin + width(), y_, style_.rule, 0.7);
            space(8);
        }
    }

    void ribbon(const std::string& title, const PdfColor& color) {
        ensure(40);
        space(4);
        if (style_.illuminated) {
            const double w = PdfWriter::textWidth(title, PdfFace::Bold, 10.5) + 22;
            y_ -= 17;
            pdf_.fillRect(kMargin, y_, w, 17, color);
            pdf_.fillPolygon({{kMargin + w, y_}, {kMargin + w + 7, y_ + 8.5}, {kMargin + w, y_ + 17}}, color);
            pdf_.text(kMargin + 11, y_ + 5, title, PdfFace::Bold, 10.5, style_.onColor);
            space(7);
        } else {
            paragraph(title, PdfFace::Bold, 11, style_.heading, 0, 3);
        }
    }

    void bullet(const std::string& label, const std::string& text, double indent, const PdfColor& color) {
        const auto lines = wrapText(text, PdfFace::Regular, 10.5, width() - indent - 16);
        bool first = true;
        for (const std::string& line : lines) {
            ensure(10.5 * 1.38);
            y_ -= 10.5 * 1.38;
            if (first) pdf_.text(kMargin + indent, y_ + 3, label, PdfFace::Bold, 10.5, color);
            pdf_.text(kMargin + indent + 16, y_ + 3, line, PdfFace::Regular, 10.5, style_.text);
            first = false;
        }
        space(2);
    }

    void badge(const std::string& text, const std::string& detail, const PdfColor& color) {
        ensure(22);
        const double w = PdfWriter::textWidth(text, PdfFace::Bold, 10) + 16;
        y_ -= 16;
        pdf_.fillRect(kMargin, y_, w, 16, color);
        pdf_.text(kMargin + 8, y_ + 4.5, text, PdfFace::Bold, 10, style_.onColor);
        if (!detail.empty()) pdf_.text(kMargin + w + 8, y_ + 4.5, detail, PdfFace::Regular, 10, style_.muted);
        space(8);
    }

    void image(const ImageData& data, int index) {
        if (index < 0) return;
        const double maxW = std::min(width(), 380.0);
        const double maxH = 250.0;
        double w = data.width;
        double h = data.height;
        const double scale = std::min(maxW / w, maxH / h);
        w *= scale;
        h *= scale;
        ensure(h + 12);
        y_ -= h;
        pdf_.drawImage(index, kMargin + (width() - w) / 2, y_, w, h);
        if (style_.illuminated) pdf_.strokeRect(kMargin + (width() - w) / 2, y_, w, h, style_.gold, 1.0);
        space(10);
    }

    PdfWriter& pdf() { return pdf_; }
    const Style& style() const { return style_; }
    void setCursor(double y) { y_ = y; }

private:
    PdfWriter& pdf_;
    const Style& style_;
    std::string label_;
    double y_ = 0.0;
};

void renderBlock(Flow& flow, const Style& style, const RenderBlock& block, const PdfColor& accent, int& numbered) {
    if (block.kind != BlockKind::Numbered) numbered = 0;
    switch (block.kind) {
        case BlockKind::Heading: flow.paragraph(block.text, PdfFace::Bold, 11.5, style.heading, 0, 2); break;
        case BlockKind::Paragraph: flow.paragraph(block.text, PdfFace::Regular, 10.5, style.text, 0, 5); break;
        case BlockKind::Bullet: flow.bullet("\xE2\x80\xA2", block.text, 8, accent); break;
        case BlockKind::Numbered:
            flow.bullet(std::to_string(++numbered) + ".", block.text + (block.detail.empty() ? "" : "  (hint: " + block.detail + ")"), 8, accent);
            break;
        case BlockKind::Quote: {
            flow.shaded(block.text, PdfFace::Regular, 10.5, style.passageBackground, accent, style.text);
            if (!block.detail.empty()) flow.paragraph(replaceAll(block.detail, "\n", " "), PdfFace::Regular, 9, style.muted, 14, 6);
            break;
        }
        case BlockKind::KeyValue:
            flow.paragraph(block.text, PdfFace::Bold, 10.5, accent, 8, 1);
            if (!block.detail.empty()) flow.paragraph(replaceAll(block.detail, "\n", " "), PdfFace::Regular, 10.5, style.text, 20, 5);
            break;
        case BlockKind::Code:
            for (const std::string& line : wrapText(block.text, PdfFace::Mono, 8.5, flow.width() - 20)) {
                flow.ensure(12);
                flow.space(12);
                flow.pdf().fillRect(kMargin, flow.cursor() - 2, flow.width(), 12, style.passageBackground);
                flow.pdf().text(kMargin + 8, flow.cursor(), line, PdfFace::Mono, 8.5, style.text);
            }
            flow.space(6);
            break;
        case BlockKind::Badge: flow.badge(block.text, block.detail, accent); break;
    }
}

}  // namespace

Result<Bytes> exportPdf(const ExportBundle& bundle, const ExportOptions& options, bool illuminated) {
    if (bundle.cards.empty()) return makeError(ErrorCode::InvalidArgument, "nothing to export");
    const Style style = makeStyle(illuminated);
    PdfWriter pdf;
    Flow flow(pdf, style, "Eden Research Reader  -  " + bundle.title);
    const double w = pdf.pageWidth();
    const double h = pdf.pageHeight();

    flow.startPage();
    if (illuminated) {
        pdf.fillRect(0, 0, w, h, PdfColor::fromHex("#12060a"));
        pdf.strokeRect(24, 24, w - 48, h - 48, style.gold, 2.0);
        pdf.strokeRect(32, 32, w - 64, h - 64, style.gold, 0.6);
        for (std::size_t i = 0; i < style.jewels.size(); ++i) {
            drawGem(pdf, w / 2 - 62.5 + static_cast<double>(i) * 25, h - 120, 9, PdfColor::mix(style.jewels[i], PdfColor{1, 1, 1}, 0.15), style.gold);
        }
        drawLogo(pdf, w / 2, h - 205, 1.3, style.gold);
    }
    const PdfColor titleColor = illuminated ? style.gold : style.heading;
    const PdfColor subColor = illuminated ? PdfColor::fromHex("#e9dcc0") : style.muted;
    double cursor = h - (illuminated ? 340 : 240);
    for (const std::string& line : wrapText(bundle.title, PdfFace::Bold, 26, w - 140)) {
        pdf.text((w - PdfWriter::textWidth(line, PdfFace::Bold, 26)) / 2, cursor, line, PdfFace::Bold, 26, titleColor);
        cursor -= 34;
    }
    for (const std::string& text : {bundle.subtitle, bundle.author.empty() ? std::string() : "Prepared by " + bundle.author, "Generated " + bundle.generatedAt.substr(0, 10)}) {
        if (text.empty()) continue;
        cursor -= 8;
        for (const std::string& line : wrapText(text, PdfFace::Regular, 13, w - 140)) {
            pdf.text((w - PdfWriter::textWidth(line, PdfFace::Regular, 13)) / 2, cursor, line, PdfFace::Regular, 13, subColor);
            cursor -= 18;
        }
    }
    if (illuminated) pdf.text((w - PdfWriter::textWidth("Out of Eden flows a golden river", PdfFace::Regular, 11)) / 2, 80, "Out of Eden flows a golden river", PdfFace::Regular, 11, style.gold);

    const int tocPages = static_cast<int>((bundle.cards.size() + 34) / 35);
    const int tocFirst = pdf.pageCount() + 1;
    for (int i = 0; i < tocPages; ++i) flow.startPage();

    std::vector<int> startPages;
    std::vector<std::string> tocTitles;
    int index = 0;
    for (const ExportCard& card : bundle.cards) {
        flow.startPage();
        startPages.push_back(pdf.pageCount());
        const std::string heading = std::to_string(++index) + ". " + (card.chapter.empty() ? card.documentTitle : card.chapter);
        tocTitles.push_back(heading);
        const PdfColor accent = style.jewels[static_cast<std::size_t>(index - 1) % style.jewels.size()];
        flow.headingBar(heading, accent, 15);
        flow.paragraph(card.documentTitle + "  -  page " + std::to_string(card.page), PdfFace::Regular, 9.5, style.muted, 0, 8);
        flow.shaded(card.passage, PdfFace::Regular, 11, style.passageBackground, style.passageBar, style.text);
        if (!card.summary.empty()) {
            flow.ribbon("Summary", accent);
            flow.paragraph(card.summary, PdfFace::Regular, 10.5, style.text, 0, 6);
        }
        if (options.includeImages) {
            for (const ExportImage& item : card.images) {
                auto loaded = loadImage(item.path);
                if (!loaded.ok()) continue;
                flow.image(loaded.value(), pdf.addImage(loaded.value()));
                if (!item.caption.empty()) flow.paragraph(item.caption, PdfFace::Regular, 9, style.muted, 0, 6);
            }
        }
        for (const ExportSection& section : card.sections) {
            const PdfColor color = groupColor(style, section.group);
            flow.ribbon(section.title, color);
            int numbered = 0;
            for (const RenderBlock& block : section.blocks) renderBlock(flow, style, block, color, numbered);
            flow.space(4);
        }
        if (options.includeSources && !card.sources.empty()) {
            flow.ribbon("Sources", style.jewels[0]);
            for (const Source& source : card.sources) {
                flow.bullet("\xE2\x80\xA2", (source.title.empty() ? source.url : source.title) + "  -  " + source.url, 8, style.jewels[0]);
            }
        }
        if (!card.comments.empty()) {
            flow.ribbon("Discussion", style.jewels[std::min<std::size_t>(3, style.jewels.size() - 1)]);
            for (const ExportComment& comment : card.comments) {
                const std::string who = comment.author + (comment.writerOriginal ? "  [Author]" : "");
                flow.paragraph(who + ": " + (comment.body.empty() ? "[" + comment.kind + " attachment]" : comment.body), PdfFace::Regular, 10, style.text,
                               8.0 + comment.depth * 14.0, 3);
            }
        }
    }

    for (int page = 0; page < tocPages; ++page) {
        pdf.setCurrentPage(tocFirst - 1 + page);
        flow.setCursor(h - kMargin);
        if (page == 0) {
            flow.paragraph("Contents", PdfFace::Bold, 20, style.heading, 0, 10);
        }
        const std::size_t begin = static_cast<std::size_t>(page) * 35;
        const std::size_t end = std::min(tocTitles.size(), begin + 35);
        for (std::size_t i = begin; i < end; ++i) {
            const std::string label = truncateWithEllipsis(tocTitles[i], 70);
            const double y = flow.cursor() - 17;
            pdf.text(kMargin, y, label, PdfFace::Regular, 11, style.text);
            const std::string number = std::to_string(startPages[i]);
            const double numberWidth = PdfWriter::textWidth(number, PdfFace::Regular, 11);
            pdf.text(w - kMargin - numberWidth, y, number, PdfFace::Regular, 11, style.text);
            const double labelEnd = kMargin + PdfWriter::textWidth(label, PdfFace::Regular, 11) + 6;
            for (double x = labelEnd; x < w - kMargin - numberWidth - 6; x += 5) pdf.fillCircle(x, y + 2, 0.5, style.muted);
            flow.setCursor(flow.cursor() - 17);
        }
    }
    return pdf.finish(bundle.title, bundle.author, bundle.subtitle);
}

}  // namespace eden
