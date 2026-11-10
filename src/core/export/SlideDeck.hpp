#pragma once

#include <string>
#include <vector>

#include "export/ExportModel.hpp"

namespace eden {

struct Slide {
    std::string title;
    std::vector<std::string> bullets;
    std::string notes;
    std::string imagePath;
    bool isTitle = false;
};

std::vector<Slide> buildSlideDeck(const ExportBundle& bundle, const ExportOptions& options);
std::string exportHtmlSlides(const ExportBundle& bundle, const ExportOptions& options);

}  // namespace eden
