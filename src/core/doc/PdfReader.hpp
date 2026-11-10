#pragma once

#include "common/Result.hpp"
#include "common/Types.hpp"
#include "doc/ExtractedDocument.hpp"

namespace eden {

Result<ExtractedDocument> readPdfDocument(const Bytes& data, const std::string& fileName);

}  // namespace eden
