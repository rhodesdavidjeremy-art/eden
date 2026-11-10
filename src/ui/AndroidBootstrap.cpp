#include "AndroidBootstrap.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProgressDialog>

#include "app/BundleExtractor.hpp"

namespace eden::ui {

PythonRuntime preparePythonRuntime(const QString& dataRoot) {
    PythonRuntime runtime;
    const QString target = dataRoot + "/runtime";
    runtime.home = target + "/python";
    runtime.sidecarDir = target + "/eden-python";

    QFile asset(QStringLiteral("assets:/python-bundle.zip"));
    if (!asset.open(QIODevice::ReadOnly)) {
        runtime.error = QStringLiteral("The Python bundle (assets/python-bundle.zip) is missing from the app package.");
        return runtime;
    }
    // A new build has a new stamp, so an update re-extracts; the same build starts instantly.
    const QString stamp = QString::number(asset.size()) + QStringLiteral("|") + QStringLiteral(__DATE__ " " __TIME__);
    QFile marker(target + "/.stamp");
    if (marker.open(QIODevice::ReadOnly)) {
        const QString existing = QString::fromUtf8(marker.readAll());
        marker.close();
        if (existing == stamp && QDir(runtime.sidecarDir + "/eden_ai").exists() && QDir(runtime.home + "/lib").exists()) {
            return runtime;
        }
    }

    QProgressDialog progress(QStringLiteral("Preparing the AI engine (first start only)..."), QString(), 0, 100);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();
    QCoreApplication::processEvents();

    QDir(target).removeRecursively();
    QDir().mkpath(target);
    const QByteArray data = asset.readAll();
    asset.close();
    eden::Bytes bytes(reinterpret_cast<const std::uint8_t*>(data.constData()),
                      reinterpret_cast<const std::uint8_t*>(data.constData()) + data.size());
    eden::Status extracted = eden::extractZipBundle(bytes, target.toStdString(), [&progress](std::size_t done, std::size_t total) {
        progress.setMaximum(static_cast<int>(total));
        progress.setValue(static_cast<int>(done));
        if (done % 20 == 0) QCoreApplication::processEvents();
    });
    progress.close();
    if (!extracted.ok()) {
        runtime.error = QStringLiteral("Could not unpack the Python runtime: ") + QString::fromStdString(extracted.error().message);
        return runtime;
    }
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write(stamp.toUtf8());
        marker.close();
    }
    return runtime;
}

}  // namespace eden::ui
