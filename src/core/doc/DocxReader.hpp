#pragma once

#include "common/Result.hpp"
#include "common/Types.hpp"
#include "doc/ExtractedDocument.hpp"

namespace eden {

Result<ExtractedDocument> readDocxDocument(const Bytes& data, const std::string& fileName);

}  // namespace eden
