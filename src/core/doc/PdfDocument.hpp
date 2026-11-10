#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "common/Result.hpp"
#include "common/Types.hpp"
#include "doc/PdfObjects.hpp"

namespace eden {

struct PdfPage {
    int objectNumber = 0;
    PdfObject dict;
    PdfObject resources;
};

class PdfDocument {
public:
    static Result<PdfDocument> parse(Bytes data);

    PdfObject resolve(const PdfObject& object) const;
    PdfObject object(int number) const;
    bool hasObject(int number) const;
    Result<Bytes> streamData(int number) const;
    Result<Bytes> streamData(const PdfObject& referenceOrObject) const;
    Result<Bytes> decodeStreamBytes(const PdfObject& dict, std::string_view raw) const;

    const std::vector<PdfPage>& pages() const { return pages_; }
    std::string infoString(const std::string& key) const;
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    struct Indirect {
        PdfObject value;
        bool hasStream = false;
        std::size_t streamStart = 0;
        std::size_t streamEnd = 0;
    };

    void scanObjects();
    void expandObjectStreams();
    void locateTrailer();
    void collectPages();
    void walkPageTree(const PdfObject& node, const PdfObject& inheritedResources, int depth,
                      std::set<int>& visited);

    Bytes data_;
    std::map<int, Indirect> objects_;
    PdfObject root_;
    PdfObject info_;
    bool encrypted_ = false;
    std::vector<PdfPage> pages_;
    std::vector<std::string> warnings_;
};

std::string pdfTextToUtf8(const std::string& raw);
Result<Bytes> lzwDecode(const std::uint8_t* data, std::size_t size, int earlyChange);
Bytes applyPredictor(const Bytes& data, int predictor, int columns, int colors, int bitsPerComponent);

}  // namespace eden
