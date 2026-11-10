#include "app/AppController.hpp"

#include "db/Migrations.hpp"
#include "doc/Zip.hpp"
#include "repo/UserRepository.hpp"

namespace eden {

Result<std::unique_ptr<AppController>> AppController::create(AppOptions options) {
    std::unique_ptr<AppController> app(new AppController());
    app->paths_ = options.paths;
    app->dispatcher_ = options.dispatcher;
    Status directories = ensureDirectories(app->paths_);
    if (!directories.ok()) return directories.error();
    try {
        app->db_ = std::make_unique<Database>(Database::open(app->paths_.databasePath));
        migrate(*app->db_);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
    UserRepository users(*app->db_);
    auto user = users.ensureDefault(options.defaultUserName);
    if (!user.ok()) return user.error();
    app->user_ = user.value();
    app->keys_ = options.keys ? std::move(options.keys) : makeDefaultKeyStore(app->paths_.keyFilePath);

    SidecarOptions sidecar;
    sidecar.command = options.sidecarCommand;
    sidecar.environment = options.sidecarEnvironment;
    app->sidecar_ = std::make_unique<SidecarClient>(std::move(sidecar));
    app->cards_ = std::make_unique<CardService>(*app->db_, *app->sidecar_);
    app->library_ = std::make_unique<LibraryModel>(*app->db_, app->user_.id);
    app->theme_ = std::make_unique<ThemeManager>(*app->db_);
    Status themeLoaded = app->theme_->load();
    if (!themeLoaded.ok()) return themeLoaded.error();
    app->tasks_ = std::make_unique<TaskRunner>(options.workers, options.dispatcher);
    return app;
}

AppController::~AppController() { shutdown(); }

void AppController::shutdown() {
    if (shutDown_) return;
    shutDown_ = true;
    if (tasks_) {
        tasks_->cancelAll();
        tasks_->shutdown();
    }
    if (sidecar_) sidecar_->stop();
}

void AppController::post(std::function<void()> work) {
    if (dispatcher_) {
        dispatcher_(std::move(work));
    } else {
        work();
    }
}

std::unique_ptr<ReaderModel> AppController::openReader(const Id& docId, Status* status) {
    auto reader = std::make_unique<ReaderModel>(*db_);
    Status opened = reader->open(docId);
    if (status != nullptr) *status = opened;
    if (!opened.ok()) return nullptr;
    return reader;
}

TaskId AppController::importAsync(const std::string& path, ImportOptions options,
                                  std::function<void(const TaskInfo&)> onProgress,
                                  std::function<void(Result<ImportResult>)> onDone) {
    if (onProgress) tasks_->setProgressHandler(std::move(onProgress));
    auto outcome = std::make_shared<std::optional<ImportResult>>();
    auto failure = std::make_shared<Status>();
    options.ownerId = user_.id;
    if (options.mediaDirectory.empty()) options.mediaDirectory = mediaDirectory();
    if (options.imageDirectory.empty()) options.imageDirectory = paths_.imageDirectory;
    return tasks_->submit(
        "Importing " + path,
        [this, path, options, outcome, failure](TaskContext& context) mutable {
            options.progress = [&context](double fraction, const std::string& message) { context.progress(fraction, message); };
            options.cancelFlag = context.cancelFlag();
            DocumentImporter importer(*db_);
            auto imported = importer.importFile(path, options);
            if (!imported.ok()) {
                *failure = imported.error();
                return Status(imported.error());
            }
            *outcome = std::move(imported.value());
            return Status::success();
        },
        [outcome, failure, onDone](const Status& status) {
            if (!onDone) return;
            if (!status.ok()) {
                onDone(status.error());
            } else {
                onDone(std::move(**outcome));
            }
        });
}

TaskId AppController::runCardAsync(const Id& packId, const std::string& functionId, RunOptions options,
                                   ChunkHandler onChunk, std::function<void(Result<CardResult>)> onDone) {
    auto outcome = std::make_shared<std::optional<CardResult>>();
    return tasks_->submit(
        "Researching " + functionId,
        [this, packId, functionId, options, onChunk, outcome](TaskContext& context) mutable {
            options.cancelFlag = context.cancelFlag();
            ChunkHandler forward;
            if (onChunk) forward = [this, onChunk](const std::string& text) { post([onChunk, text] { onChunk(text); }); };
            auto result = cards_->run(user_.id, packId, functionId, options, forward);
            if (!result.ok()) return Status(result.error());
            *outcome = std::move(result.value());
            return Status::success();
        },
        [outcome, onDone](const Status& status) {
            if (!onDone) return;
            if (!status.ok()) {
                onDone(status.error());
            } else {
                onDone(std::move(**outcome));
            }
        });
}

const FunctionDirectory& AppController::functions() {
    if (!directoryLoaded_ || directory_.empty()) {
        auto loaded = loadFunctionDirectory(*sidecar_, *db_);
        if (loaded.ok()) {
            directory_ = loaded.value();
            directoryLoaded_ = true;
        }
    }
    return directory_;
}

Result<ExportBundle> AppController::buildExport(const std::vector<Id>& packIds, const Id& docId, const Id& collectionId,
                                                const std::string& title, const ExportOptions& options) {
    CommentService comments(*db_, mediaDirectory(), user_.id);
    ExportBuilder builder(*db_, functions(), comments);
    if (!collectionId.empty()) return builder.forCollection(collectionId, options);
    if (!docId.empty()) return builder.forDocument(docId, options);
    return builder.forPacks(packIds, title, options);
}

Status AppController::exportTo(ExportFormat format, const ExportBundle& bundle, const ExportOptions& options, const std::string& path) {
    return writeExport(format, bundle, options, path);
}

Status AppController::publishEnhanced(const Id& docId, const std::string& path, const EdenPublishOptions& options) {
    auto built = buildEnhancedDocument(*db_, docId, user_.id, mediaDirectory(), options);
    if (!built.ok()) return built.error();
    return writeFileBytes(path, built.value());
}

TaskId AppController::authorBatchAsync(const Id& docId, std::vector<std::string> functionIds,
                                       std::function<void(const TaskInfo&)> onProgress,
                                       std::function<void(Result<AuthorBatchReport>)> onDone) {
    if (onProgress) tasks_->setProgressHandler(std::move(onProgress));
    auto report = std::make_shared<std::optional<AuthorBatchReport>>();
    return tasks_->submit(
        "Preparing research cards",
        [this, docId, functionIds = std::move(functionIds), report](TaskContext& context) {
            AuthorBatchOptions options;
            options.docId = docId;
            options.functionIds = functionIds;
            options.cancelFlag = context.cancelFlag();
            options.progress = [&context](double fraction, const std::string& message) { context.progress(fraction, message); };
            auto outcome = runAuthorBatch(*cards_, *db_, user_.id, options);
            if (!outcome.ok()) return Status(outcome.error());
            *report = std::move(outcome.value());
            return report->has_value() && (*report)->cancelled ? Status(makeError(ErrorCode::Cancelled, "batch cancelled")) : Status::success();
        },
        [report, onDone](const Status& status) {
            if (!onDone) return;
            if (!status.ok() && !(report->has_value())) {
                onDone(status.error());
            } else {
                onDone(std::move(**report));
            }
        });
}

Result<int> AppController::unlockProviders() {
    auto listed = cards_->providers();
    if (!listed.ok()) return listed.error();
    int unlocked = 0;
    for (const Json& provider : listed.value()) {
        const std::string name = provider.getString("name");
        auto key = keys_->get(name);
        if (!key.ok()) return key.error();
        if (!key.value().has_value()) continue;
        Status pushed = cards_->setProviderKey(name, *key.value());
        if (!pushed.ok()) return pushed.error();
        ++unlocked;
    }
    return unlocked;
}

Status AppController::saveProviderKey(const std::string& provider, const std::string& key) {
    Status stored = keys_->set(provider, key);
    if (!stored.ok()) return stored;
    return cards_->setProviderKey(provider, key);
}

Status AppController::removeProviderKey(const std::string& provider) {
    Status removed = keys_->remove(provider);
    if (!removed.ok()) return removed;
    return cards_->setProviderKey(provider, "");
}

}  // namespace eden
