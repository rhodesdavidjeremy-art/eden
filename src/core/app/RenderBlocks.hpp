#pragma once

#include <string>
#include <vector>

#include "app/ThemeManager.hpp"
#include "common/Json.hpp"

namespace eden {

enum class BlockKind { Heading, Paragraph, Bullet, Numbered, Quote, KeyValue, Code, Badge };

struct RenderBlock {
    BlockKind kind = BlockKind::Paragraph;
    std::string text;
    std::string detail;
    int level = 0;
};

std::vector<RenderBlock> markdownToBlocks(const std::string& markdown);
std::vector<RenderBlock> renderFunctionResult(const std::string& functionId, const std::string& output,
                                              const Json& data, const std::string& text);
std::string inlineToHtml(const std::string& text);
std::string blocksToHtml(const std::vector<RenderBlock>& blocks, const Palette& palette);

}  // namespace eden
