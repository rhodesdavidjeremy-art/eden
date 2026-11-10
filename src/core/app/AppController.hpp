#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ai/CardService.hpp"
#include "ai/SidecarClient.hpp"
#include "app/AppPaths.hpp"
#include "app/AuthorTools.hpp"
#include "app/FunctionDirectory.hpp"
#include "doc/EdenFormat.hpp"
#include "export/ExportService.hpp"
#include "app/KeyStore.hpp"
#include "app/LibraryModel.hpp"
#include "app/ReaderModel.hpp"
#include "app/TaskRunner.hpp"
#include "app/ThemeManager.hpp"
#include "db/Database.hpp"

namespace eden {

struct AppOptions {
    AppPaths paths;
    std::vector<std::string> sidecarCommand;
    std::map<std::string, std::string> sidecarEnvironment;
    Dispatcher dispatcher;
    std::unique_ptr<KeyStore> keys;
    std::string defaultUserName = "Reader";
    std::size_t workers = 3;
};

class AppController {
public:
    static Result<std::unique_ptr<AppController>> create(AppOptions options);
    ~AppController();

    Database& database() { return *db_; }
    const User& user() const { return user_; }
    SidecarClient& sidecar() { return *sidecar_; }
    CardService& cards() { return *cards_; }
    LibraryModel& library() { return *library_; }
    ThemeManager& theme() { return *theme_; }
    TaskRunner& tasks() { return *tasks_; }
    KeyStore& keys() { return *keys_; }
    const AppPaths& paths() const { return paths_; }

    std::unique_ptr<ReaderModel> openReader(const Id& docId, Status* status = nullptr);

    TaskId importAsync(const std::string& path, ImportOptions options,
                       std::function<void(const TaskInfo&)> onProgress,
                       std::function<void(Result<ImportResult>)> onDone);
    TaskId runCardAsync(const Id& packId, const std::string& functionId, RunOptions options,
                        ChunkHandler onChunk, std::function<void(Result<CardResult>)> onDone);

    const FunctionDirectory& functions();
    std::string mediaDirectory() const { return paths_.dataDirectory + "/media"; }
    Result<ExportBundle> buildExport(const std::vector<Id>& packIds, const Id& docId, const Id& collectionId,
                                     const std::string& title, const ExportOptions& options);
    Status exportTo(ExportFormat format, const ExportBundle& bundle, const ExportOptions& options, const std::string& path);
    Status publishEnhanced(const Id& docId, const std::string& path, const EdenPublishOptions& options = {});
    TaskId authorBatchAsync(const Id& docId, std::vector<std::string> functionIds, std::function<void(const TaskInfo&)> onProgress,
                            std::function<void(Result<AuthorBatchReport>)> onDone);

    Result<int> unlockProviders();
    Status saveProviderKey(const std::string& provider, const std::string& key);
    Status removeProviderKey(const std::string& provider);

    void shutdown();

private:
    AppController() = default;
    void post(std::function<void()> work);

    AppPaths paths_;
    Dispatcher dispatcher_;
    std::unique_ptr<Database> db_;
    User user_;
    std::unique_ptr<KeyStore> keys_;
    std::unique_ptr<SidecarClient> sidecar_;
    std::unique_ptr<CardService> cards_;
    std::unique_ptr<LibraryModel> library_;
    std::unique_ptr<ThemeManager> theme_;
    std::unique_ptr<TaskRunner> tasks_;
    FunctionDirectory directory_;
    bool directoryLoaded_ = false;
    bool shutDown_ = false;
};

}  // namespace eden
