#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <utility>

namespace eden {

enum class LogLevel { Trace, Debug, Info, Warn, Error, Off };

using LogSink = std::function<void(LogLevel, const std::string&, const std::string&)>;

void setLogLevel(LogLevel level);
LogLevel logLevel();
void setLogSink(LogSink sink);
void resetLogSink();
void logMessage(LogLevel level, const std::string& component, const std::string& message);
const char* logLevelName(LogLevel level);

template <typename... Args>
void logf(LogLevel level, const std::string& component, Args&&... args) {
    if (level < logLevel()) return;
    std::ostringstream stream;
    (stream << ... << std::forward<Args>(args));
    logMessage(level, component, stream.str());
}

}  // namespace eden
