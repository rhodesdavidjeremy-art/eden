#pragma once

#include <string>
#include <vector>

#include "ai/SidecarClient.hpp"
#include "common/Json.hpp"
#include "common/Result.hpp"
#include "repo/SettingsRepository.hpp"

namespace eden {

struct FunctionInfo {
    std::string id;
    std::string group;
    std::string title;
    std::string output;
};

class FunctionDirectory {
public:
    static FunctionDirectory fromJson(const Json& json);
    Json toJson() const;

    const std::vector<FunctionInfo>& all() const { return functions_; }
    const FunctionInfo* find(const std::string& id) const;
    std::vector<std::string> groups() const;
    std::vector<const FunctionInfo*> inGroup(const std::string& group) const;
    bool empty() const { return functions_.empty(); }

    static std::string groupTitle(const std::string& group);

private:
    std::vector<FunctionInfo> functions_;
};

Result<FunctionDirectory> loadFunctionDirectory(SidecarClient& sidecar, Database& db);

}  // namespace eden
