#include <string>

#include "TestFramework.hpp"
#include "common/Log.hpp"

int main(int argc, char** argv) {
    eden::setLogLevel(eden::LogLevel::Warn);
    const std::string filter = argc > 1 ? argv[1] : "";
    return eden::testing::runAll(filter);
}
