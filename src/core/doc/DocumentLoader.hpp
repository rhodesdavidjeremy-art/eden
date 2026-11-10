#pragma once

#include <string>

#include "common/Result.hpp"
#include "common/Types.hpp"
#include "doc/ExtractedDocument.hpp"

namespace eden {

constexpr std::size_t kMaxDocumentBytes = 512u * 1024u * 1024u;

FileType sniffFileType(const Bytes& data, const std::string& fileName);
Result<ExtractedDocument> loadDocumentBytes(const Bytes& data, FileType type, const std::string& fileName);
Result<ExtractedDocument> loadDocument(const std::string& path);

}  // namespace eden
