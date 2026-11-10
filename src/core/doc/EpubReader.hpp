#pragma once

#include "common/Result.hpp"
#include "common/Types.hpp"
#include "doc/ExtractedDocument.hpp"

namespace eden {

Result<ExtractedDocument> readEpubDocument(const Bytes& data, const std::string& fileName);
std::string resolveArchivePath(const std::string& baseDirectory, const std::string& reference);
std::string percentDecode(const std::string& text);

}  // namespace eden
