#include <sys/stat.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

#include "TestFramework.hpp"
#include "app/AppController.hpp"
#include "app/AppPaths.hpp"
#include "app/KeyStore.hpp"
#include "app/TaskRunner.hpp"
#include "app/ThemeManager.hpp"
#include "db/Migrations.hpp"

using namespace eden;

namespace {

struct TempDir {
    std::string path;
    TempDir() { path = makeTemporaryDirectory("eden-test-").value(); }
    ~TempDir() { removeDirectoryTree(path); }
};

void writeFile(const std::string& path, const std::string& content, mode_t mode = 0644) {
    std::ofstream output(path, std::ios::binary);
    output << content;
    output.close();
    chmod(path.c_str(), mode);
}

std::string longText() {
    std::string text = "THE GARDEN\n\n";
    for (int i = 0; i < 40; ++i) {
        text += "A river went out of Eden to water the garden number " + std::to_string(i) + ". ";
        text += "From there it divided and became four rivers that ran through the land.\n\n";
    }
    return text;
}

}  // namespace

EDEN_TEST(task_runner_executes_and_reports_progress) {
    std::mutex mutex;
    std::vector<double> fractions;
    TaskRunner runner(2);
    runner.setProgressHandler([&](const TaskInfo& info) {
        std::lock_guard<std::mutex> lock(mutex);
        fractions.push_back(info.progress);
    });
    std::atomic<int> finished{0};
    Status last;
    runner.submit("work", [](TaskContext& context) {
        context.progress(0.25, "a");
        context.progress(0.75, "b");
        return Status::success();
    }, [&](const Status& status) {
        last = status;
        ++finished;
    });
    runner.waitIdle();
    EXPECT_EQ(finished.load(), 1);
    EXPECT_TRUE(last.ok());
    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_EQ(fractions.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(fractions.back(), 0.75);
}

EDEN_TEST(task_runner_captures_failures_and_exceptions) {
    TaskRunner runner(1);
    std::vector<ErrorCode> codes;
    std::mutex mutex;
    auto record = [&](const Status& status) {
        std::lock_guard<std::mutex> lock(mutex);
        codes.push_back(status.ok() ? ErrorCode::None : status.error().code);
    };
    runner.submit("fails", [](TaskContext&) { return Status(makeError(ErrorCode::NotFound, "nope")); }, record);
    runner.submit("throws", [](TaskContext&) -> Status { throw std::runtime_error("bad"); }, record);
    runner.waitIdle();
    EXPECT_EQ(codes.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(codes[0] == ErrorCode::NotFound);
    EXPECT_TRUE(codes[1] == ErrorCode::Internal);
}

EDEN_TEST(task_runner_cancels_running_and_queued_tasks) {
    TaskRunner runner(1);
    std::atomic<bool> started{false};
    std::vector<ErrorCode> codes;
    std::mutex mutex;
    const TaskId first = runner.submit("long", [&](TaskContext& context) {
        started = true;
        while (!context.cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return Status(makeError(ErrorCode::Cancelled, "stopped"));
    }, [&](const Status& status) {
        std::lock_guard<std::mutex> lock(mutex);
        codes.push_back(status.error().code);
    });
    const TaskId second = runner.submit("queued", [](TaskContext&) { return Status::success(); }, [&](const Status& status) {
        std::lock_guard<std::mutex> lock(mutex);
        codes.push_back(status.ok() ? ErrorCode::None : status.error().code);
    });
    while (!started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_TRUE(runner.cancel(second));
    EXPECT_TRUE(runner.cancel(first));
    EXPECT_FALSE(runner.cancel(9999));
    runner.waitIdle();
    EXPECT_EQ(codes.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(codes[0] == ErrorCode::Cancelled && codes[1] == ErrorCode::Cancelled);
}

EDEN_TEST(task_runner_marshals_callbacks_through_the_dispatcher) {
    std::atomic<int> dispatched{0};
    TaskRunner runner(1, [&](std::function<void()> work) {
        ++dispatched;
        work();
    });
    runner.submit("x", [](TaskContext& context) {
        context.progress(0.5, "half");
        return Status::success();
    }, [](const Status&) {});
    runner.setProgressHandler([](const TaskInfo&) {});
    runner.waitIdle();
    EXPECT_TRUE(dispatched.load() >= 1);
}

EDEN_TEST(file_key_store_persists_with_private_permissions) {
    TempDir dir;
    FileKeyStore store(dir.path + "/keys.json");
    EXPECT_FALSE(store.get("claude").value().has_value());
    EXPECT_TRUE(store.set("claude", "sk-one").ok());
    EXPECT_TRUE(store.set("grok", "xk-two").ok());
    EXPECT_EQ(*store.get("claude").value(), std::string("sk-one"));
    struct stat info{};
    stat((dir.path + "/keys.json").c_str(), &info);
    EXPECT_EQ(static_cast<int>(info.st_mode & 0777), 0600);
    EXPECT_TRUE(store.remove("claude").ok());
    EXPECT_FALSE(store.get("claude").value().has_value());
    EXPECT_EQ(*store.get("grok").value(), std::string("xk-two"));
    EXPECT_FALSE(store.set("", "x").ok());
}

EDEN_TEST(secret_tool_store_talks_to_the_command_line_tool) {
    TempDir dir;
    const std::string script = dir.path + "/secret-tool";
    writeFile(script,
              "#!/bin/sh\n"
              "dir=\"$(dirname \"$0\")/vault\"\nmkdir -p \"$dir\"\ncmd=\"$1\"; shift\nacct=\"\"\n"
              "while [ $# -gt 0 ]; do if [ \"$1\" = account ]; then acct=\"$2\"; fi; shift; done\n"
              "case \"$cmd\" in\n"
              "store) cat > \"$dir/$acct\" ;;\n"
              "lookup) if [ -f \"$dir/$acct\" ]; then cat \"$dir/$acct\"; else exit 1; fi ;;\n"
              "clear) rm -f \"$dir/$acct\" ;;\n"
              "esac\n", 0755);
    EXPECT_TRUE(SecretToolKeyStore::available(script));
    EXPECT_FALSE(SecretToolKeyStore::available(dir.path + "/missing"));
    SecretToolKeyStore store(script);
    EXPECT_FALSE(store.get("claude").value().has_value());
    EXPECT_TRUE(store.set("claude", "sk-secret").ok());
    EXPECT_EQ(*store.get("claude").value(), std::string("sk-secret"));
    EXPECT_TRUE(store.remove("claude").ok());
    EXPECT_FALSE(store.get("claude").value().has_value());
}

EDEN_TEST(palettes_meet_contrast_requirements) {
    EXPECT_TRUE(builtinPalettes().size() >= 6);
    for (const Palette& palette : builtinPalettes()) {
        EXPECT_TRUE(contrastRatio(palette.text, palette.background) >= 7.0);
        EXPECT_TRUE(contrastRatio(palette.text, palette.surface) >= 7.0);
        EXPECT_TRUE(contrastRatio(palette.mutedText, palette.background) >= 4.5);
        EXPECT_TRUE(contrastRatio(palette.accent, palette.background) >= 4.5);
        EXPECT_TRUE(contrastRatio(palette.text, palette.selection) >= 4.5);
        Rgb parsed;
        for (const std::string& color : {palette.gold, palette.ruby, palette.emerald, palette.sapphire, palette.amethyst}) {
            EXPECT_TRUE(parseHexColor(color, parsed));
        }
    }
    const Palette* high = findPalette("high-contrast");
    EXPECT_TRUE(high != nullptr);
    EXPECT_TRUE(contrastRatio(high->text, high->background) >= 20.0);
}

EDEN_TEST(color_helpers) {
    EXPECT_EQ(contrastRatio("#000000", "#ffffff"), 21.0);
    EXPECT_EQ(mixColors("#000000", "#ffffff", 0.5), std::string("#808080"));
    EXPECT_EQ(withAlpha("#ff8000", 0.5), std::string("rgba(255, 128, 0, 0.50)"));
    Rgb color;
    EXPECT_FALSE(parseHexColor("nope", color));
    EXPECT_EQ(toHexColor({255, 0, 16}), std::string("#ff0010"));
}

EDEN_TEST(theme_manager_round_trips_settings_and_builds_stylesheet) {
    Database db = Database::openMemory();
    migrate(db);
    ThemeManager theme(db);
    EXPECT_TRUE(theme.load().ok());
    EXPECT_EQ(theme.settings().palette, std::string("ruby-night"));
    EXPECT_TRUE(theme.setPalette("emerald-garden").ok());
    EXPECT_TRUE(theme.setBackground("aurora").ok());
    EXPECT_TRUE(theme.setFontScale(1.5).ok());
    theme.settings().dyslexiaFont = true;
    theme.settings().reducedMotion = true;
    EXPECT_TRUE(theme.save().ok());
    ThemeManager reloaded(db);
    EXPECT_TRUE(reloaded.load().ok());
    EXPECT_EQ(reloaded.settings().palette, std::string("emerald-garden"));
    EXPECT_EQ(reloaded.settings().background, std::string("aurora"));
    EXPECT_EQ(reloaded.settings().fontScale, 1.5);
    EXPECT_TRUE(reloaded.settings().dyslexiaFont);
    EXPECT_EQ(reloaded.particleCount(), 0);
    const std::string css = reloaded.styleSheet();
    EXPECT_TRUE(css.find("#04120c") != std::string::npos);
    EXPECT_TRUE(css.find("OpenDyslexic") != std::string::npos);
    EXPECT_TRUE(css.find("16.5pt") != std::string::npos);
    EXPECT_FALSE(theme.setPalette("nope").ok());
    EXPECT_FALSE(theme.setFontScale(5.0).ok());
    reloaded.settings().highContrast = true;
    EXPECT_EQ(reloaded.activePalette().name, std::string("high-contrast"));
}

EDEN_TEST(theme_manager_recovers_from_invalid_stored_values) {
    Database db = Database::openMemory();
    migrate(db);
    SettingsRepository settings(db);
    settings.set("theme.palette", "deleted-palette");
    settings.set("theme.fontScale", "banana");
    settings.set("theme.particleDensity", "9");
    ThemeManager theme(db);
    EXPECT_TRUE(theme.load().ok());
    EXPECT_EQ(theme.settings().palette, std::string("ruby-night"));
    EXPECT_EQ(theme.settings().fontScale, 1.0);
    EXPECT_EQ(theme.settings().particleDensity, 1.0);
}

EDEN_TEST(app_controller_imports_reads_and_researches) {
    TempDir dir;
    AppOptions options;
    options.paths = pathsUnder(dir.path + "/data");
    options.sidecarCommand = {"python3", EDEN_FAKE_SIDECAR};
    options.keys = std::make_unique<FileKeyStore>(options.paths.keyFilePath);
    auto created = AppController::create(std::move(options));
    EXPECT_TRUE(created.ok());
    AppController& app = *created.value();
    EXPECT_FALSE(app.user().id.empty());

    writeFile(dir.path + "/garden.txt", longText());
    std::mutex mutex;
    std::vector<double> progress;
    std::optional<ImportResult> imported;
    Status importStatus = makeError(ErrorCode::Internal, "not finished");
    app.importAsync(dir.path + "/garden.txt", {}, [&](const TaskInfo& info) {
        std::lock_guard<std::mutex> lock(mutex);
        progress.push_back(info.progress);
    }, [&](Result<ImportResult> result) {
        std::lock_guard<std::mutex> lock(mutex);
        if (result.ok()) {
            imported = result.value();
            importStatus = Status::success();
        } else {
            importStatus = result.error();
        }
    });
    app.tasks().waitIdle();
    EXPECT_TRUE(importStatus.ok());
    EXPECT_TRUE(imported.has_value());
    EXPECT_TRUE(imported->packCount > 10);
    EXPECT_TRUE(progress.size() >= 3);

    auto entries = app.library().entries();
    EXPECT_EQ(entries.value().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(entries.value()[0].document.title, std::string("THE GARDEN"));
    EXPECT_TRUE(entries.value()[0].subtitle.find("TXT") == 0);

    Status status;
    auto reader = app.openReader(imported->document.id, &status);
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(reader != nullptr);
    const ResearchPack pack = reader->packs()[3];

    std::mutex resultMutex;
    std::optional<CardResult> card;
    std::vector<std::string> chunks;
    app.runCardAsync(pack.id, "keypoints", {}, [&](const std::string& text) {
        std::lock_guard<std::mutex> lock(resultMutex);
        chunks.push_back(text);
    }, [&](Result<CardResult> result) {
        std::lock_guard<std::mutex> lock(resultMutex);
        if (result.ok()) card = result.value();
    });
    app.tasks().waitIdle();
    EXPECT_TRUE(card.has_value());
    EXPECT_EQ(card->data.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(chunks.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(reader->reloadPacks().ok());
    auto page = reader->page(reader->content().pageIndexAt(pack.startOffset));
    bool flagged = false;
    for (const PackMark& mark : page.value().marks) flagged = flagged || (mark.packId == pack.id && mark.hasCard);
    EXPECT_TRUE(flagged);

    EXPECT_TRUE(app.saveProviderKey("claude", "sk-live").ok());
    EXPECT_EQ(*app.keys().get("claude").value(), std::string("sk-live"));
    auto unlocked = app.unlockProviders();
    EXPECT_TRUE(unlocked.ok());
    EXPECT_EQ(unlocked.value(), 1);
    EXPECT_TRUE(app.removeProviderKey("claude").ok());
    EXPECT_FALSE(app.keys().get("claude").value().has_value());
    reader.reset();
    app.shutdown();
}

EDEN_TEST(app_controller_reports_import_failures_and_cancellation) {
    TempDir dir;
    AppOptions options;
    options.paths = pathsUnder(dir.path + "/data");
    options.sidecarCommand = {"python3", EDEN_FAKE_SIDECAR};
    auto created = AppController::create(std::move(options));
    EXPECT_TRUE(created.ok());
    AppController& app = *created.value();
    writeFile(dir.path + "/bad.pdf", "%PDF-1.4 not really a pdf");
    std::mutex mutex;
    std::optional<ErrorCode> code;
    app.importAsync(dir.path + "/bad.pdf", {}, nullptr, [&](Result<ImportResult> result) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!result.ok()) code = result.error().code;
    });
    app.importAsync(dir.path + "/missing.txt", {}, nullptr, [&](Result<ImportResult> result) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!result.ok() && !code.has_value()) code = result.error().code;
    });
    app.tasks().waitIdle();
    EXPECT_TRUE(code.has_value());
    EXPECT_EQ(app.library().entries().value().size(), static_cast<std::size_t>(0));
}
