#include "app/FunctionDirectory.hpp"

#include <algorithm>

namespace eden {

namespace {

const char* kGroupOrder[] = {"understand", "evidence", "expand", "assist", "visual", "output"};

int groupRank(const std::string& group) {
    for (int i = 0; i < 6; ++i) {
        if (group == kGroupOrder[i]) return i;
    }
    return 6;
}

}  // namespace

FunctionDirectory FunctionDirectory::fromJson(const Json& json) {
    FunctionDirectory directory;
    const Json& list = json.isArray() ? json : json.get("functions");
    for (const Json& item : list.items()) {
        FunctionInfo info;
        info.id = item.getString("id");
        info.group = item.getString("group");
        info.title = item.getString("title");
        info.output = item.getString("output", "markdown");
        if (!info.id.empty()) directory.functions_.push_back(std::move(info));
    }
    std::stable_sort(directory.functions_.begin(), directory.functions_.end(),
                     [](const FunctionInfo& a, const FunctionInfo& b) { return groupRank(a.group) < groupRank(b.group); });
    return directory;
}

Json FunctionDirectory::toJson() const {
    Json list = Json::array();
    for (const FunctionInfo& info : functions_) {
        list.push(Json::object({{"id", Json(info.id)}, {"group", Json(info.group)}, {"title", Json(info.title)},
                                {"output", Json(info.output)}}));
    }
    return list;
}

const FunctionInfo* FunctionDirectory::find(const std::string& id) const {
    for (const FunctionInfo& info : functions_) {
        if (info.id == id) return &info;
    }
    return nullptr;
}

std::vector<std::string> FunctionDirectory::groups() const {
    std::vector<std::string> out;
    for (const FunctionInfo& info : functions_) {
        if (std::find(out.begin(), out.end(), info.group) == out.end()) out.push_back(info.group);
    }
    return out;
}

std::vector<const FunctionInfo*> FunctionDirectory::inGroup(const std::string& group) const {
    std::vector<const FunctionInfo*> out;
    for (const FunctionInfo& info : functions_) {
        if (info.group == group) out.push_back(&info);
    }
    return out;
}

std::string FunctionDirectory::groupTitle(const std::string& group) {
    if (group == "understand") return "Understand";
    if (group == "evidence") return "Evidence";
    if (group == "expand") return "Expand";
    if (group == "assist") return "AI Assistants";
    if (group == "visual") return "Visuals";
    if (group == "output") return "Export";
    return group;
}

Result<FunctionDirectory> loadFunctionDirectory(SidecarClient& sidecar, Database& db) {
    SettingsRepository settings(db);
    auto reply = sidecar.call("functions.list");
    if (reply.ok()) {
        FunctionDirectory directory = FunctionDirectory::fromJson(reply.value());
        if (!directory.empty()) {
            settings.setJson("functions.directory", directory.toJson());
            return directory;
        }
    }
    const Json cached = settings.getJsonOr("functions.directory", Json());
    FunctionDirectory fromCache = FunctionDirectory::fromJson(cached);
    if (!fromCache.empty()) return fromCache;
    if (!reply.ok()) return reply.error();
    return makeError(ErrorCode::NotFound, "no research functions are available");
}

}  // namespace eden
