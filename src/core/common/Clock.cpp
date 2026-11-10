#include "common/Clock.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace eden {

namespace {

// Portable wrappers: gmtime_r / timegm are POSIX-only.
void utcParts(std::time_t seconds, std::tm& parts) {
#ifdef _WIN32
    const std::tm* converted = std::gmtime(&seconds);  // thread-local buffer in the Windows C runtime
    if (converted != nullptr) parts = *converted;
#else
    utcParts(seconds, parts);
#endif
}

std::time_t utcSeconds(std::tm& parts) {
#ifdef _WIN32
    return _mkgmtime(&parts);
#else
    return timegm(&parts);
#endif
}

}  // namespace

Millis nowMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string formatIso8601(Millis millis) {
    std::time_t seconds = static_cast<std::time_t>(millis / 1000);
    int remainder = static_cast<int>(millis % 1000);
    if (remainder < 0) {
        remainder += 1000;
        seconds -= 1;
    }
    std::tm parts{};
    gmtime_r(&seconds, &parts);
    char base[32];
    std::strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &parts);
    char out[48];
    std::snprintf(out, sizeof(out), "%s.%03dZ", base, remainder);
    return out;
}

std::optional<Millis> parseIso8601(const std::string& text) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int consumed = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d%n", &year, &month, &day, &hour, &minute,
                    &second, &consumed) != 6) {
        return std::nullopt;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59 || second < 0 || second > 60) {
        return std::nullopt;
    }
    int millisPart = 0;
    std::size_t pos = static_cast<std::size_t>(consumed);
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        int digits = 0;
        int value = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            if (digits < 3) {
                value = value * 10 + (text[pos] - '0');
                ++digits;
            }
            ++pos;
        }
        while (digits < 3) {
            value *= 10;
            ++digits;
        }
        millisPart = value;
    }
    std::tm parts{};
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = second;
    std::time_t seconds = utcSeconds(parts);
    if (seconds == static_cast<std::time_t>(-1)) return std::nullopt;
    return static_cast<Millis>(seconds) * 1000 + millisPart;
}

Stopwatch::Stopwatch() { restart(); }

void Stopwatch::restart() {
    using namespace std::chrono;
    startTicks_ = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

double Stopwatch::elapsedMillis() const {
    using namespace std::chrono;
    const std::int64_t now =
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
    return static_cast<double>(now - startTicks_) / 1000.0;
}

}  // namespace eden
