#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace eden::testing {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

struct Failure {
    std::string message;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

template <typename T, typename = void>
struct Streamable : std::false_type {};
template <typename T>
struct Streamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <typename T>
std::string show(const T& value) {
    if constexpr (Streamable<T>::value) {
        std::ostringstream out;
        out << value;
        return out.str();
    } else {
        return "<unprintable>";
    }
}

template <typename A, typename B>
void expectEqual(const A& actual, const B& expected, const char* actualText, const char* expectedText,
                 const char* file, int line) {
    if (!(actual == expected)) {
        std::ostringstream message;
        message << file << ":" << line << ": expected " << actualText << " == " << expectedText
                << " (actual: " << show(actual) << ", expected: " << show(expected) << ")";
        throw Failure{message.str()};
    }
}

inline void expectTrue(bool condition, const char* text, const char* file, int line) {
    if (!condition) {
        std::ostringstream message;
        message << file << ":" << line << ": expected " << text;
        throw Failure{message.str()};
    }
}

inline int runAll(const std::string& filter) {
    int passed = 0;
    int failed = 0;
    for (const TestCase& test : registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
        try {
            test.body();
            ++passed;
            std::cout << "[ ok ] " << test.name << "\n";
        } catch (const Failure& failure) {
            ++failed;
            std::cout << "[FAIL] " << test.name << "\n       " << failure.message << "\n";
        } catch (const std::exception& error) {
            ++failed;
            std::cout << "[FAIL] " << test.name << "\n       exception: " << error.what() << "\n";
        }
    }
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace eden::testing

#define EDEN_TEST(name)                                                              \
    static void name();                                                              \
    static ::eden::testing::Registrar registrar_##name(#name, name);                 \
    static void name()

#define EXPECT_EQ(actual, expected) \
    ::eden::testing::expectEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_TRUE(condition) ::eden::testing::expectTrue((condition), #condition, __FILE__, __LINE__)
#define EXPECT_FALSE(condition) ::eden::testing::expectTrue(!(condition), "!(" #condition ")", __FILE__, __LINE__)
