#include "common/Log.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>

#include "common/Clock.hpp"

namespace eden {

namespace {

std::atomic<int> gLevel{static_cast<int>(LogLevel::Info)};
std::mutex gSinkMutex;
LogSink gSink;

void defaultSink(LogLevel level, const std::string& component, const std::string& message) {
    const std::string stamp = formatIso8601(nowMillis());
    std::fprintf(stderr, "%s %-5s [%s] %s\n", stamp.c_str(), logLevelName(level), component.c_str(),
                 message.c_str());
}

}  // namespace

const char* logLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Off: return "OFF";
    }
    return "?";
}

void setLogLevel(LogLevel level) { gLevel.store(static_cast<int>(level)); }

LogLevel logLevel() { return static_cast<LogLevel>(gLevel.load()); }

void setLogSink(LogSink sink) {
    std::lock_guard<std::mutex> lock(gSinkMutex);
    gSink = std::move(sink);
}

void resetLogSink() {
    std::lock_guard<std::mutex> lock(gSinkMutex);
    gSink = nullptr;
}

void logMessage(LogLevel level, const std::string& component, const std::string& message) {
    if (level < logLevel()) return;
    LogSink sink;
    {
        std::lock_guard<std::mutex> lock(gSinkMutex);
        sink = gSink;
    }
    if (sink) {
        sink(level, component, message);
    } else {
        defaultSink(level, component, message);
    }
}

}  // namespace eden
