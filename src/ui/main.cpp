#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QStandardPaths>

#include "MainWindow.hpp"
#include "QtDispatcher.hpp"
#include "app/AppController.hpp"
#include "ai/EmbeddedPython.hpp"
#ifdef Q_OS_ANDROID
#include "AndroidBootstrap.hpp"
#endif

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setApplicationName("Eden Research Reader");
    application.setOrganizationName("Eden");

    eden::AppOptions options;
    options.paths = eden::defaultPaths();
    options.dispatcher = eden::ui::makeQtDispatcher();

#ifdef Q_OS_ANDROID
    // Android: everything lives in the app's private storage; there is no /tmp, no $HOME and no python executable.
    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dataRoot);
    QDir().mkpath(cacheRoot);
    qputenv("HOME", dataRoot.toUtf8());
    qputenv("TMPDIR", cacheRoot.toUtf8());
    options.paths = eden::pathsUnder(dataRoot.toStdString());
#ifdef EDEN_EMBED_PYTHON
    const eden::ui::PythonRuntime runtime = eden::ui::preparePythonRuntime(dataRoot);
    if (!runtime.ok()) {
        QMessageBox::critical(nullptr, "Eden Research Reader", runtime.error);
        return 1;
    }
    options.sidecarCommand = {eden::kEmbeddedPythonCommand, "-m", "eden_ai"};
    options.sidecarEnvironment["PYTHONHOME"] = runtime.home.toStdString();
    options.sidecarEnvironment["PYTHONPATH"] = runtime.sidecarDir.toStdString();
    options.sidecarEnvironment["PYTHONUTF8"] = "1";
    options.sidecarEnvironment["SSL_CERT_DIR"] = "/system/etc/security/cacerts";  // Android's CA store
#endif
#else
    const QDir applicationDir(QCoreApplication::applicationDirPath());
    QString pythonDir = qEnvironmentVariable("EDEN_PYTHON_DIR");
    // A packaged build keeps the sidecar in <app>/python; a source build keeps it in <app>/../python.
    if (pythonDir.isEmpty() && QDir(applicationDir.filePath("python/eden_ai")).exists()) {
        pythonDir = applicationDir.filePath("python");
    }
    if (pythonDir.isEmpty()) pythonDir = applicationDir.filePath("../python");
    if (!QDir(pythonDir).exists()) pythonDir = QStringLiteral(EDEN_PYTHON_DIR);
    std::string pythonExecutable = "python3";
#ifdef Q_OS_WIN
    pythonExecutable = "python";
    // Packaged Windows builds ship an embedded Python next to the executable.
    const QString bundledPython = applicationDir.filePath("python-runtime/python.exe");
    if (QFile::exists(bundledPython)) pythonExecutable = QDir::toNativeSeparators(bundledPython).toStdString();
    options.sidecarEnvironment["PYTHONUTF8"] = "1";
#endif
    options.sidecarCommand = {pythonExecutable, "-m", "eden_ai"};
    options.sidecarEnvironment["PYTHONPATH"] = QDir::toNativeSeparators(pythonDir).toStdString();
#endif

    auto created = eden::AppController::create(std::move(options));
    if (!created.ok()) {
        QMessageBox::critical(nullptr, "Eden Research Reader",
                              QString::fromStdString("The application could not start: " + created.error().message));
        return 1;
    }
    eden::MainWindow window(*created.value());
#ifdef Q_OS_ANDROID
    window.showMaximized();
#else
    window.resize(1360, 860);
    window.show();
#endif
    const int code = application.exec();
    created.value()->shutdown();
    return code;
}
