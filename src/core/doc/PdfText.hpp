#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "doc/PdfDocument.hpp"
#include "doc/PdfFont.hpp"

namespace eden {

struct PdfHeading {
    std::size_t offset = 0;
    std::string title;
    int level = 1;
};

struct PdfPageText {
    std::string text;
    std::vector<PdfHeading> headings;
};

class PdfFontCache {
public:
    std::shared_ptr<PdfFont> get(const PdfDocument& doc, const PdfObject& fontObject);

private:
    std::map<int, std::shared_ptr<PdfFont>> cache_;
};

PdfPageText extractPdfPageText(const PdfDocument& doc, const PdfPage& page, PdfFontCache& fonts,
                               std::vector<std::string>& warnings);

}  // namespace eden
