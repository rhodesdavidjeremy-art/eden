#pragma once

#include <string>

#include "common/Result.hpp"
#include "common/Types.hpp"
#include "doc/ExtractedDocument.hpp"

namespace eden {

Result<ExtractedDocument> readTxtDocument(const Bytes& data, const std::string& fileName);

}  // namespace eden
