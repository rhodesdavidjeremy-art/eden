#include "app/AuthorTools.hpp"

#include <algorithm>

#include "repo/PackRepository.hpp"

namespace eden {

Result<AuthorBatchReport> runAuthorBatch(CardService& cards, Database& db, const Id& userId, const AuthorBatchOptions& options) {
    if (options.functionIds.empty()) return makeError(ErrorCode::InvalidArgument, "choose at least one research function");
    PackRepository packRepository(db);
    auto listed = packRepository.listByDocument(options.docId);
    if (!listed.ok()) return listed.error();
    std::vector<ResearchPack> packs;
    for (const ResearchPack& pack : listed.value()) {
        if (!options.packIds.empty() && std::find(options.packIds.begin(), options.packIds.end(), pack.id) == options.packIds.end()) continue;
        if (pack.text.size() < options.minimumPackCharacters) continue;
        packs.push_back(pack);
    }
    if (packs.empty()) return makeError(ErrorCode::NotFound, "no research packs match the batch selection");

    AuthorBatchReport report;
    report.total = static_cast<int>(packs.size() * options.functionIds.size());
    int done = 0;
    for (const ResearchPack& pack : packs) {
        for (const std::string& functionId : options.functionIds) {
            if (options.cancelFlag != nullptr && options.cancelFlag->load()) {
                report.cancelled = true;
                return report;
            }
            if (options.progress) {
                options.progress(static_cast<double>(done) / static_cast<double>(report.total),
                                 "Pack " + std::to_string(pack.orderIndex + 1) + ": " + functionId);
            }
            RunOptions run = options.run;
            run.cancelFlag = options.cancelFlag;
            auto result = cards.run(userId, pack.id, functionId, run);
            ++done;
            if (result.ok()) {
                ++report.succeeded;
                if (result.value().cached) ++report.cached;
                continue;
            }
            ++report.failed;
            report.errors.push_back(functionId + " on pack " + std::to_string(pack.orderIndex + 1) + ": " + result.error().message);
            if (result.error().code == ErrorCode::Cancelled) {
                report.cancelled = true;
                return report;
            }
            if (result.error().code == ErrorCode::Auth) {
                report.stoppedEarly = true;
                return report;
            }
        }
    }
    if (options.progress) options.progress(1.0, "Done");
    return report;
}

}  // namespace eden
