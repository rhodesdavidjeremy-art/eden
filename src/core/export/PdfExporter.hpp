#pragma once

#include "common/Result.hpp"
#include "common/Types.hpp"
#include "export/ExportModel.hpp"

namespace eden {

Result<Bytes> exportPdf(const ExportBundle& bundle, const ExportOptions& options, bool illuminated);

}  // namespace eden
