#pragma once

#include <string>

#include "common/Result.hpp"
#include "common/Types.hpp"
#include "export/ExportModel.hpp"

namespace eden {

Result<Bytes> renderExport(ExportFormat format, const ExportBundle& bundle, const ExportOptions& options);
Status writeExport(ExportFormat format, const ExportBundle& bundle, const ExportOptions& options, const std::string& path);
std::string suggestedFileName(const ExportBundle& bundle, ExportFormat format);

}  // namespace eden
