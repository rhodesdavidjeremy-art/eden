#include "doc/DocumentLoader.hpp"

#include "common/Strings.hpp"
#include "doc/DocxReader.hpp"
#include "doc/EpubReader.hpp"
#include "doc/PdfReader.hpp"
#include "doc/TextUtil.hpp"
#include "doc/TxtReader.hpp"
#include "doc/Zip.hpp"

namespace eden {

namespace {

bool looksLikePlainText(const Bytes& data) {
    if (data.empty()) return false;
    const std::size_t probe = std::min<std::size_t>(data.size(), 4096);
    std::size_t suspicious = 0;
    for (std::size_t i = 0; i < probe; ++i) {
        const std::uint8_t byte = data[i];
        if (byte == 0 || (byte < 0x09) || (byte > 0x0D && byte < 0x20)) ++suspicious;
    }
    return suspicious * 100 < probe;
}

}  // namespace

FileType sniffFileType(const Bytes& data, const std::string& name) {
    if (data.size() >= 5) {
        const std::size_t window = std::min<std::size_t>(data.size(), 1024);
        const std::string head(reinterpret_cast<const char*>(data.data()), window);
        if (head.find("%PDF-") != std::string::npos) return FileType::Pdf;
    }
    if (data.size() >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 3 && data[3] == 4) {
        auto zip = ZipReader::open(data);
        if (zip.ok()) {
            if (zip.value().has("eden.json")) return FileType::Eden;
            if (zip.value().has("META-INF/container.xml")) return FileType::Epub;
            if (zip.value().has("word/document.xml")) return FileType::Docx;
        }
        return FileType::Unknown;
    }
    const FileType byName = fileTypeFromPath(name);
    if (byName == FileType::Txt) return FileType::Txt;
    const std::string lower = toLowerAscii(name);
    if (endsWith(lower, ".md") || endsWith(lower, ".markdown") || endsWith(lower, ".text") || endsWith(lower, ".rst")) {
        return FileType::Txt;
    }
    if (looksLikePlainText(data)) return FileType::Txt;
    return FileType::Unknown;
}

Result<ExtractedDocument> loadDocumentBytes(const Bytes& data, FileType type, const std::string& name) {
    if (data.empty()) return makeError(ErrorCode::InvalidArgument, "file is empty: " + name);
    if (data.size() > kMaxDocumentBytes) return makeError(ErrorCode::InvalidArgument, "file is too large: " + name);
    if (type == FileType::Unknown) type = sniffFileType(data, name);
    Result<ExtractedDocument> loaded = makeError(ErrorCode::Unsupported, "unsupported file type: " + name);
    switch (type) {
        case FileType::Pdf: loaded = readPdfDocument(data, name); break;
        case FileType::Docx: loaded = readDocxDocument(data, name); break;
        case FileType::Epub: loaded = readEpubDocument(data, name); break;
        case FileType::Txt: loaded = readTxtDocument(data, name); break;
        case FileType::Eden: loaded = makeError(ErrorCode::Unsupported, "enhanced eden documents are not supported yet"); break;
        case FileType::Unknown: break;
    }
    if (!loaded.ok()) return loaded;
    ExtractedDocument doc = std::move(loaded.value());
    doc.type = type;
    doc.title = trim(doc.title);
    if (doc.title.empty()) doc.title = fileStem(name);
    if (doc.pages.empty()) paginateByLength(doc.text, 0, doc.charCount(), 3000, 0, doc.pages);
    return doc;
}

Result<ExtractedDocument> loadDocument(const std::string& path) {
    auto bytes = readFileBytes(path);
    if (!bytes.ok()) return bytes.error();
    const FileType type = sniffFileType(bytes.value(), path);
    return loadDocumentBytes(bytes.value(), type, path);
}

}  // namespace eden
