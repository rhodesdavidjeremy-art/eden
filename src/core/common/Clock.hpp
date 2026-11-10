#pragma once

#include <optional>
#include <string>

#include "common/Types.hpp"

namespace eden {

Millis nowMillis();
std::string formatIso8601(Millis millis);
std::optional<Millis> parseIso8601(const std::string& text);

class Stopwatch {
public:
    Stopwatch();
    void restart();
    double elapsedMillis() const;

private:
    std::int64_t startTicks_;
};

}  // namespace eden
