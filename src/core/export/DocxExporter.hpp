#pragma once

#include "common/Result.hpp"
#include "common/Types.hpp"
#include "export/ExportModel.hpp"

namespace eden {

Result<Bytes> exportDocx(const ExportBundle& bundle, const ExportOptions& options);

}  // namespace eden
