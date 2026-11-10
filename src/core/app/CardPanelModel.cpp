#include "app/CardPanelModel.hpp"

#include "common/Strings.hpp"
#include "repo/CardRepository.hpp"
#include "repo/PackRepository.hpp"
#include "repo/SourceRepository.hpp"

namespace eden {

namespace {

SectionView buildSection(const FunctionInfo* info, const std::string& baseId, const CardSection& stored) {
    SectionView section;
    section.storedId = stored.functionId;
    section.functionId = baseId;
    section.title = info != nullptr ? info->title : baseId;
    section.group = info != nullptr ? info->group : "other";
    section.provider = stored.provider;
    section.createdAt = stored.createdAt;
    section.hasContent = true;
    auto envelope = Json::parse(stored.content);
    Json data;
    std::string text = stored.content;
    if (envelope.ok() && envelope.value().isObject()) {
        text = envelope.value().getString("text");
        data = envelope.value().get("data");
        section.model = envelope.value().getString("model");
        section.warnings = envelope.value().getStringList("warnings");
    }
    section.blocks = renderFunctionResult(baseId, info != nullptr ? info->output : "markdown", data, text);
    if (stored.functionId != baseId && !stored.provider.empty()) section.title += " (" + stored.provider + ")";
    return section;
}

}  // namespace

Result<CardView> CardPanelModel::load(const Id& packId) {
    PackRepository packs(db_);
    auto pack = packs.get(packId);
    if (!pack.ok()) return pack.error();
    CardView view;
    view.packId = packId;
    view.passage = pack.value().text;

    CardRepository cards(db_);
    auto card = cards.getByPack(packId);
    std::vector<CardSection> stored;
    if (card.ok()) {
        view.cardId = card.value().id;
        view.summary = card.value().summaryText;
        auto sections = cards.listSections(card.value().id);
        if (sections.ok()) stored = std::move(sections.value());
        SourceRepository sources(db_);
        auto listed = sources.listByCard(card.value().id);
        if (listed.ok()) view.sources = std::move(listed.value());
        auto thread = comments_.thread(card.value().id);
        if (thread.ok()) {
            view.comments = std::move(thread.value());
            view.commentCount = static_cast<std::int64_t>(view.comments.size());
        }
    }

    std::vector<bool> used(stored.size(), false);
    for (const FunctionInfo& info : directory_.all()) {
        bool found = false;
        for (std::size_t i = 0; i < stored.size(); ++i) {
            const std::string& id = stored[i].functionId;
            if (id == info.id || startsWith(id, info.id + "@")) {
                view.sections.push_back(buildSection(&info, info.id, stored[i]));
                used[i] = true;
                found = true;
            }
        }
        if (!found) {
            SectionView empty;
            empty.storedId = info.id;
            empty.functionId = info.id;
            empty.title = info.title;
            empty.group = info.group;
            view.sections.push_back(std::move(empty));
        }
    }
    for (std::size_t i = 0; i < stored.size(); ++i) {
        if (used[i]) continue;
        const std::string base = stored[i].functionId.substr(0, stored[i].functionId.find('@'));
        view.sections.push_back(buildSection(directory_.find(base), base, stored[i]));
    }
    for (const SectionView& section : view.sections) view.filledSections += section.hasContent ? 1 : 0;
    return view;
}

std::vector<SectionView> CardPanelModel::sectionsInGroup(const CardView& view, const std::string& group) const {
    std::vector<SectionView> out;
    for (const SectionView& section : view.sections) {
        if (section.group == group) out.push_back(section);
    }
    return out;
}

}  // namespace eden
