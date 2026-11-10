#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QUrl>

// Android's file picker hands back content:// URIs, which the core (plain C++ file I/O) cannot open. These helpers
// copy a picked file into app storage before the core reads it, and copy a finished export out to the chosen
// location. On every other platform they pass paths through untouched.
namespace eden::ui {

inline bool isContentUri(const QString& path) { return path.startsWith(QStringLiteral("content:")); }

inline QString sanitizedFileName(QString name) {
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char(':'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return name;
}

inline QString guessExtension(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    const QByteArray head = file.read(65536);
    if (head.startsWith("%PDF")) return QStringLiteral(".pdf");
    if (head.startsWith("PK")) {
        if (head.contains("application/epub+zip")) return QStringLiteral(".epub");
        if (head.contains("word/")) return QStringLiteral(".docx");
    }
    return QStringLiteral(".txt");
}

inline QString localCopyForRead(const QString& chosen) {
    if (!isContentUri(chosen)) return chosen;
    QString name = sanitizedFileName(QFileInfo(chosen).fileName());
    if (name.isEmpty()) name = QStringLiteral("picked");
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/picked");
    QDir().mkpath(directory);
    QString target = directory + QLatin1Char('/') + name;
    QFile::remove(target);
    if (!QFile::copy(chosen, target)) return chosen;  // the core will report the failure
    if (!QFileInfo(target).suffix().isEmpty()) return target;
    const QString extension = guessExtension(target);
    const QString renamed = target + extension;
    QFile::remove(renamed);
    return QFile::rename(target, renamed) ? renamed : target;
}

struct SaveTarget {
    QString localPath;  // where the core writes
    QString chosen;     // what the user picked
    bool copyBack = false;
};

inline SaveTarget prepareSave(const QString& chosen) {
    SaveTarget target;
    target.chosen = chosen;
    target.localPath = chosen;
    if (isContentUri(chosen)) {
        QString name = sanitizedFileName(QFileInfo(chosen).fileName());
        if (name.isEmpty()) name = QStringLiteral("export");
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/exports");
        QDir().mkpath(directory);
        target.localPath = directory + QLatin1Char('/') + name;
        target.copyBack = true;
    }
    return target;
}

inline bool finishSave(const SaveTarget& target, QString* error = nullptr) {
    if (!target.copyBack) return true;
    QFile in(target.localPath);
    QFile out(target.chosen);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) *error = QStringLiteral("Could not write to the location you picked.");
        return false;
    }
    while (!in.atEnd()) {
        if (out.write(in.read(1 << 20)) < 0) {
            if (error != nullptr) *error = QStringLiteral("Could not write to the location you picked.");
            return false;
        }
    }
    out.close();
    in.close();
    QFile::remove(target.localPath);
    return true;
}

inline QUrl urlForOpening(const QString& path) { return isContentUri(path) ? QUrl(path) : QUrl::fromLocalFile(path); }

}  // namespace eden::ui
