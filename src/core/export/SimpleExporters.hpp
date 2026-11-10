#pragma once

#include <string>

#include "export/ExportModel.hpp"

namespace eden {

std::string exportMarkdown(const ExportBundle& bundle, const ExportOptions& options);
std::string exportPlainText(const ExportBundle& bundle, const ExportOptions& options);
std::string exportHtml(const ExportBundle& bundle, const ExportOptions& options);
std::string exportAnkiTsv(const ExportBundle& bundle);
std::string imageDataUri(const std::string& path);

}  // namespace eden
