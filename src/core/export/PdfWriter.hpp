#pragma once

#include <string>
#include <utility>
#include <vector>

#include "common/Result.hpp"
#include "common/Types.hpp"
#include "export/ImageInfo.hpp"

namespace eden {

struct PdfColor {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;

    static PdfColor fromHex(const std::string& hex);
    static PdfColor mix(const PdfColor& a, const PdfColor& b, double amount);
};

enum class PdfFace { Regular, Bold, Mono };

std::string toWinAnsi(const std::string& utf8);

class PdfWriter {
public:
    explicit PdfWriter(double width = 612.0, double height = 792.0) : width_(width), height_(height) {}

    double pageWidth() const { return width_; }
    double pageHeight() const { return height_; }
    int pageCount() const { return static_cast<int>(pages_.size()); }
    int currentPage() const { return current_; }
    void newPage();
    void setCurrentPage(int index);

    int addImage(const ImageData& image);

    void fillRect(double x, double y, double w, double h, const PdfColor& color);
    void strokeRect(double x, double y, double w, double h, const PdfColor& color, double lineWidth);
    void line(double x1, double y1, double x2, double y2, const PdfColor& color, double lineWidth);
    void fillPolygon(const std::vector<std::pair<double, double>>& points, const PdfColor& color);
    void strokePolygon(const std::vector<std::pair<double, double>>& points, const PdfColor& color, double lineWidth);
    void fillCircle(double cx, double cy, double radius, const PdfColor& color);
    void strokeCircle(double cx, double cy, double radius, const PdfColor& color, double lineWidth);
    void strokeBezier(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                      const PdfColor& color, double lineWidth);
    void text(double x, double y, const std::string& utf8, PdfFace face, double size, const PdfColor& color);
    void drawImage(int index, double x, double y, double w, double h);

    static double textWidth(const std::string& utf8, PdfFace face, double size);
    Bytes finish(const std::string& title, const std::string& author, const std::string& subject) const;

private:
    struct StoredImage {
        ImageData jpeg;
        RawImage raw;
        bool isJpeg = false;
    };

    std::string& content();

    double width_;
    double height_;
    int current_ = -1;
    std::vector<std::string> pages_;
    std::vector<StoredImage> images_;
};

}  // namespace eden
