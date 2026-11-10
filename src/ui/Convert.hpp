#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <string>

namespace eden::ui {

inline QString q(const std::string& text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }

inline std::string s(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

inline int utf8ToUtf16Offset(const std::string& text, std::int64_t byteOffset) {
    if (byteOffset <= 0) return 0;
    const std::int64_t clamped = byteOffset > static_cast<std::int64_t>(text.size()) ? static_cast<std::int64_t>(text.size()) : byteOffset;
    return static_cast<int>(QString::fromUtf8(text.data(), static_cast<qsizetype>(clamped)).size());
}

inline std::int64_t utf16ToUtf8Offset(const QString& text, int position) {
    if (position <= 0) return 0;
    return static_cast<std::int64_t>(text.left(position).toUtf8().size());
}

}  // namespace eden::ui
