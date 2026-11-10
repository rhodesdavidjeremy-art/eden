#include "export/PdfWriter.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

#include "common/Clock.hpp"
#include "common/Strings.hpp"
#include "doc/Inflate.hpp"

namespace eden {

namespace {

const int kHelvetica[95] = {278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556,
                            556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778,
                            722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278,
                            278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
                            556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584};
const int kHelveticaBold[95] = {278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556,
                                556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722, 667, 611, 778,
                                722, 278, 556, 722, 611, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333,
                                278, 333, 584, 556, 333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
                                611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584};

struct WinAnsiMap {
    char32_t code;
    unsigned char byte;
};

const WinAnsiMap kSpecial[] = {{0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87},
                               {0x02C6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8A}, {0x2039, 0x8B}, {0x0152, 0x8C}, {0x017D, 0x8E}, {0x2018, 0x91},
                               {0x2019, 0x92}, {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97}, {0x02DC, 0x98},
                               {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B}, {0x0153, 0x9C}, {0x017E, 0x9E}, {0x0178, 0x9F}};

double glyphWidth(unsigned char byte, PdfFace face) {
    if (face == PdfFace::Mono) return 600.0;
    const int* table = face == PdfFace::Bold ? kHelveticaBold : kHelvetica;
    if (byte >= 32 && byte <= 126) return table[byte - 32];
    switch (byte) {
        case 0x95: return 350.0;
        case 0x97: return 1000.0;
        case 0x96: return 556.0;
        case 0x91:
        case 0x92: return 222.0;
        case 0x93:
        case 0x94: return 333.0;
        case 0x85: return 1000.0;
        case 0xA0: return 278.0;
        default: return face == PdfFace::Bold ? 611.0 : 556.0;
    }
}

std::string fixed(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

std::string colorOps(const PdfColor& c, bool fill) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f %.3f %.3f %s\n", c.r, c.g, c.b, fill ? "rg" : "RG");
    return buffer;
}

std::string pdfString(const std::string& winAnsi) {
    std::string out = "(";
    for (unsigned char c : winAnsi) {
        if (c == '(' || c == ')' || c == '\\') {
            out.push_back('\\');
            out.push_back(static_cast<char>(c));
        } else if (c < 0x20) {
            continue;
        } else if (c >= 0x80) {
            char octal[8];
            std::snprintf(octal, sizeof(octal), "\\%03o", c);
            out += octal;
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out + ")";
}

std::string utf16Hex(const std::string& utf8) {
    std::string out = "<FEFF";
    std::size_t position = 0;
    char32_t cp = 0;
    while (position < utf8.size() && decodeUtf8(utf8, position, cp)) {
        char buffer[16];
        if (cp >= 0x10000) {
            const char32_t v = cp - 0x10000;
            std::snprintf(buffer, sizeof(buffer), "%04X%04X", static_cast<unsigned>(0xD800 + (v >> 10)), static_cast<unsigned>(0xDC00 + (v & 0x3FF)));
        } else {
            std::snprintf(buffer, sizeof(buffer), "%04X", static_cast<unsigned>(cp));
        }
        out += buffer;
    }
    return out + ">";
}

}  // namespace

PdfColor PdfColor::fromHex(const std::string& hex) {
    unsigned value = 0;
    if (hex.size() == 7 && hex[0] == '#' && std::sscanf(hex.c_str() + 1, "%6x", &value) == 1) {
        return {((value >> 16) & 0xFF) / 255.0, ((value >> 8) & 0xFF) / 255.0, (value & 0xFF) / 255.0};
    }
    return {0, 0, 0};
}

PdfColor PdfColor::mix(const PdfColor& a, const PdfColor& b, double amount) {
    return {a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount, a.b + (b.b - a.b) * amount};
}

std::string toWinAnsi(const std::string& utf8) {
    std::string out;
    std::size_t position = 0;
    char32_t cp = 0;
    const std::string safe = sanitizeUtf8(utf8);
    while (position < safe.size() && decodeUtf8(safe, position, cp)) {
        if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) {
            out.push_back(static_cast<char>(cp));
            continue;
        }
        bool mapped = false;
        for (const WinAnsiMap& entry : kSpecial) {
            if (entry.code == cp) {
                out.push_back(static_cast<char>(entry.byte));
                mapped = true;
                break;
            }
        }
        if (mapped) continue;
        if (cp == 0x2726 || cp == 0x25C6 || cp == 0x25CF) out.push_back('*');
        else if (cp == 0x2192) out += "->";
        else if (cp == 0x2212) out.push_back('-');
        else if (cp == 0x00A0 || cp == 0x2009 || cp == 0x2002 || cp == 0x2003) out.push_back(' ');
        else out.push_back('?');
    }
    return out;
}

std::string& PdfWriter::content() {
    if (current_ < 0) newPage();
    return pages_[static_cast<std::size_t>(current_)];
}

void PdfWriter::newPage() {
    pages_.emplace_back();
    current_ = static_cast<int>(pages_.size()) - 1;
}

void PdfWriter::setCurrentPage(int index) {
    if (index >= 0 && index < pageCount()) current_ = index;
}

int PdfWriter::addImage(const ImageData& image) {
    StoredImage stored;
    if (image.extension == "jpeg") {
        if (image.components != 1 && image.components != 3) return -1;
        stored.jpeg = image;
        stored.isJpeg = true;
    } else {
        auto raw = decodePngRaw(image.bytes);
        if (!raw.ok()) return -1;
        stored.raw = std::move(raw.value());
    }
    images_.push_back(std::move(stored));
    return static_cast<int>(images_.size()) - 1;
}

void PdfWriter::fillRect(double x, double y, double w, double h, const PdfColor& color) {
    content() += colorOps(color, true) + fixed(x) + " " + fixed(y) + " " + fixed(w) + " " + fixed(h) + " re f\n";
}

void PdfWriter::strokeRect(double x, double y, double w, double h, const PdfColor& color, double lineWidth) {
    content() += colorOps(color, false) + fixed(lineWidth) + " w\n" + fixed(x) + " " + fixed(y) + " " + fixed(w) + " " + fixed(h) + " re S\n";
}

void PdfWriter::line(double x1, double y1, double x2, double y2, const PdfColor& color, double lineWidth) {
    content() += colorOps(color, false) + fixed(lineWidth) + " w\n" + fixed(x1) + " " + fixed(y1) + " m " + fixed(x2) + " " + fixed(y2) + " l S\n";
}

void PdfWriter::fillPolygon(const std::vector<std::pair<double, double>>& points, const PdfColor& color) {
    if (points.size() < 3) return;
    std::string ops = colorOps(color, true);
    for (std::size_t i = 0; i < points.size(); ++i) ops += fixed(points[i].first) + " " + fixed(points[i].second) + (i == 0 ? " m\n" : " l\n");
    content() += ops + "h f\n";
}

void PdfWriter::strokePolygon(const std::vector<std::pair<double, double>>& points, const PdfColor& color, double lineWidth) {
    if (points.size() < 3) return;
    std::string ops = colorOps(color, false) + fixed(lineWidth) + " w\n";
    for (std::size_t i = 0; i < points.size(); ++i) ops += fixed(points[i].first) + " " + fixed(points[i].second) + (i == 0 ? " m\n" : " l\n");
    content() += ops + "h S\n";
}

namespace {

std::string circlePath(double cx, double cy, double r) {
    const double k = 0.5523 * r;
    return fixed(cx + r) + " " + fixed(cy) + " m\n" + fixed(cx + r) + " " + fixed(cy + k) + " " + fixed(cx + k) + " " + fixed(cy + r) + " " + fixed(cx) + " " + fixed(cy + r) + " c\n" +
           fixed(cx - k) + " " + fixed(cy + r) + " " + fixed(cx - r) + " " + fixed(cy + k) + " " + fixed(cx - r) + " " + fixed(cy) + " c\n" + fixed(cx - r) + " " + fixed(cy - k) + " " +
           fixed(cx - k) + " " + fixed(cy - r) + " " + fixed(cx) + " " + fixed(cy - r) + " c\n" + fixed(cx + k) + " " + fixed(cy - r) + " " + fixed(cx + r) + " " + fixed(cy - k) + " " +
           fixed(cx + r) + " " + fixed(cy) + " c\n";
}

}  // namespace

void PdfWriter::fillCircle(double cx, double cy, double radius, const PdfColor& color) {
    content() += colorOps(color, true) + circlePath(cx, cy, radius) + "f\n";
}

void PdfWriter::strokeCircle(double cx, double cy, double radius, const PdfColor& color, double lineWidth) {
    content() += colorOps(color, false) + fixed(lineWidth) + " w\n" + circlePath(cx, cy, radius) + "S\n";
}

void PdfWriter::strokeBezier(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, const PdfColor& color,
                             double lineWidth) {
    content() += colorOps(color, false) + fixed(lineWidth) + " 1 J\n" + fixed(x0) + " " + fixed(y0) + " m\n" + fixed(x1) + " " + fixed(y1) + " " + fixed(x2) + " " + fixed(y2) + " " +
                 fixed(x3) + " " + fixed(y3) + " c S\n";
}

void PdfWriter::text(double x, double y, const std::string& utf8, PdfFace face, double size, const PdfColor& color) {
    const char* font = face == PdfFace::Bold ? "F2" : face == PdfFace::Mono ? "F3" : "F1";
    content() += "BT\n" + colorOps(color, true) + "/" + font + " " + fixed(size) + " Tf\n" + fixed(x) + " " + fixed(y) + " Td\n" + pdfString(toWinAnsi(utf8)) + " Tj\nET\n";
}

void PdfWriter::drawImage(int index, double x, double y, double w, double h) {
    if (index < 0 || index >= static_cast<int>(images_.size())) return;
    content() += "q\n" + fixed(w) + " 0 0 " + fixed(h) + " " + fixed(x) + " " + fixed(y) + " cm\n/Im" + std::to_string(index + 1) + " Do\nQ\n";
}

double PdfWriter::textWidth(const std::string& utf8, PdfFace face, double size) {
    double total = 0.0;
    for (unsigned char c : toWinAnsi(utf8)) total += glyphWidth(c, face);
    return total / 1000.0 * size;
}

Bytes PdfWriter::finish(const std::string& title, const std::string& author, const std::string& subject) const {
    std::vector<std::string> objects;
    auto add = [&](std::string body) {
        objects.push_back(std::move(body));
        return static_cast<int>(objects.size());
    };
    struct Placeholder {
        std::size_t index;
        Bytes stream;
    };
    std::vector<Placeholder> streams;
    const int pagesObject = 2;
    add("<< /Type /Catalog /Pages 2 0 R >>");
    add("");
    add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    add("<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>");
    add("<< /Title " + utf16Hex(title) + " /Author " + utf16Hex(author) + " /Subject " + utf16Hex(subject) + " /Creator (Eden Research Reader) /Producer (Eden Research Reader) /CreationDate (D:" +
        [] {
            std::string stamp = formatIso8601(nowMillis());
            return stamp.substr(0, 4) + stamp.substr(5, 2) + stamp.substr(8, 2) + stamp.substr(11, 2) + stamp.substr(14, 2) + stamp.substr(17, 2) + "Z";
        }() + ") >>");

    std::string imageResources;
    std::vector<int> imageObjects;
    for (std::size_t i = 0; i < images_.size(); ++i) {
        const StoredImage& image = images_[i];
        const int number = add("");
        imageObjects.push_back(number);
        if (image.isJpeg) {
            const std::string dict = "<< /Type /XObject /Subtype /Image /Width " + std::to_string(image.jpeg.width) + " /Height " + std::to_string(image.jpeg.height) +
                                     " /ColorSpace /" + (image.jpeg.components == 1 ? "DeviceGray" : "DeviceRGB") + " /BitsPerComponent 8 /Filter /DCTDecode /Length " +
                                     std::to_string(image.jpeg.bytes.size()) + " >>";
            objects.back() = dict;
            streams.push_back({objects.size() - 1, image.jpeg.bytes});
        } else {
            const Bytes packed = zlibWrapCompressed(image.raw.pixels.data(), image.raw.pixels.size());
            objects.back() = "<< /Type /XObject /Subtype /Image /Width " + std::to_string(image.raw.width) + " /Height " + std::to_string(image.raw.height) + " /ColorSpace /" +
                             (image.raw.components == 1 ? "DeviceGray" : "DeviceRGB") + " /BitsPerComponent 8 /Filter /FlateDecode /Length " + std::to_string(packed.size()) + " >>";
            streams.push_back({objects.size() - 1, packed});
        }
        imageResources += "/Im" + std::to_string(i + 1) + " " + std::to_string(number) + " 0 R ";
    }

    std::vector<int> pageObjects;
    for (const std::string& page : pages_) {
        const Bytes packed = zlibWrapCompressed(reinterpret_cast<const std::uint8_t*>(page.data()), page.size());
        const int contentObject = add("<< /Length " + std::to_string(packed.size()) + " /Filter /FlateDecode >>");
        streams.push_back({objects.size() - 1, packed});
        const int pageObject = add("<< /Type /Page /Parent " + std::to_string(pagesObject) + " 0 R /MediaBox [0 0 " + fixed(width_) + " " + fixed(height_) +
                                   "] /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R >> /XObject << " + imageResources + ">> /ProcSet [/PDF /Text /ImageB /ImageC] >> /Contents " +
                                   std::to_string(contentObject) + " 0 R >>");
        pageObjects.push_back(pageObject);
    }
    std::string kids;
    for (int number : pageObjects) kids += std::to_string(number) + " 0 R ";
    objects[1] = "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pageObjects.size()) + " >>";

    Bytes out;
    auto append = [&](const std::string& text) { out.insert(out.end(), text.begin(), text.end()); };
    append("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
    std::vector<std::size_t> offsets(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets[i] = out.size();
        append(std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\n");
        for (const Placeholder& stream : streams) {
            if (stream.index == i) {
                append("stream\n");
                out.insert(out.end(), stream.stream.begin(), stream.stream.end());
                append("\nendstream\n");
            }
        }
        append("endobj\n");
    }
    const std::size_t xref = out.size();
    append("xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n");
    for (std::size_t offset : offsets) {
        char entry[24];
        std::snprintf(entry, sizeof(entry), "%010zu 00000 n \n", offset);
        append(entry);
    }
    append("trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R /Info 6 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n");
    return out;
}

}  // namespace eden
