#pragma once

#include <string>
#include <vector>

#include "app/CommentService.hpp"
#include "app/FunctionDirectory.hpp"
#include "app/RenderBlocks.hpp"
#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

struct SectionView {
    std::string storedId;
    std::string functionId;
    std::string title;
    std::string group;
    std::string provider;
    std::string model;
    bool hasContent = false;
    std::vector<std::string> warnings;
    std::vector<RenderBlock> blocks;
    Millis createdAt = 0;
};

struct CardView {
    Id packId;
    Id cardId;
    std::string passage;
    std::string summary;
    std::vector<SectionView> sections;
    std::vector<Source> sources;
    std::vector<CommentView> comments;
    std::int64_t commentCount = 0;
    std::int64_t filledSections = 0;
};

class CardPanelModel {
public:
    CardPanelModel(Database& db, const FunctionDirectory& directory, CommentService& comments)
        : db_(db), directory_(directory), comments_(comments) {}

    Result<CardView> load(const Id& packId);
    std::vector<SectionView> sectionsInGroup(const CardView& view, const std::string& group) const;

private:
    Database& db_;
    const FunctionDirectory& directory_;
    CommentService& comments_;
};

}  // namespace eden
