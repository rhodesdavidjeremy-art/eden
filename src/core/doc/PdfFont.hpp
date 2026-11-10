#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "doc/PdfDocument.hpp"

namespace eden {

struct PdfGlyph {
    std::string text;
    double width = 500.0;
    bool isSingleByteSpace = false;
};

class PdfFont {
public:
    static std::shared_ptr<PdfFont> load(const PdfDocument& doc, const PdfObject& fontObject);

    void decode(const std::string& bytes, std::vector<PdfGlyph>& out) const;
    bool isTwoByte() const { return twoByte_; }

private:
    void loadToUnicode(const PdfDocument& doc, const PdfObject& stream);
    void loadSimpleEncoding(const PdfDocument& doc, const PdfObject& encoding);
    void loadSimpleWidths(const PdfDocument& doc, const PdfObject& font);
    void loadCidWidths(const PdfDocument& doc, const PdfObject& descendant);

    bool twoByte_ = false;
    bool hasToUnicode_ = false;
    std::map<std::uint32_t, std::string> toUnicode_;
    std::array<std::string, 256> simple_;
    std::map<std::uint32_t, double> widths_;
    double defaultWidth_ = 500.0;
};

std::string glyphNameToUtf8(const std::string& name);

}  // namespace eden
