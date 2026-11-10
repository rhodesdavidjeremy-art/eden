#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "TestFramework.hpp"
#include "ai/SidecarClient.hpp"

using namespace eden;

namespace {

SidecarOptions fakeOptions() {
    SidecarOptions options;
    options.command = {"python3", EDEN_FAKE_SIDECAR};
    options.startupTimeoutMs = 10000;
    options.defaultTimeoutMs = 10000;
    return options;
}

}  // namespace

EDEN_TEST(sidecar_starts_pings_and_echoes_json) {
    SidecarClient client(fakeOptions());
    EXPECT_TRUE(client.start().ok());
    EXPECT_TRUE(client.running());
    Json params = Json::object({{"text", Json("caf\xC3\xA9 \"quoted\"")}, {"n", Json(42)}});
    auto reply = client.call("echo", params);
    EXPECT_TRUE(reply.ok());
    EXPECT_TRUE(reply.value() == params);
    client.stop();
    EXPECT_FALSE(client.running());
}

EDEN_TEST(sidecar_starts_lazily_on_first_call) {
    SidecarClient client(fakeOptions());
    auto reply = client.call("echo", Json::object({{"a", Json(1)}}));
    EXPECT_TRUE(reply.ok());
    EXPECT_EQ(reply.value().getInt("a"), static_cast<std::int64_t>(1));
}

EDEN_TEST(sidecar_delivers_streaming_events_before_the_result) {
    SidecarClient client(fakeOptions());
    std::vector<std::string> chunks;
    CallOptions options;
    options.onEvent = [&](const Json& event) { chunks.push_back(event.get("data").getString("text")); };
    auto reply = client.call("stream", Json::object({{"count", Json(4)}}), options);
    EXPECT_TRUE(reply.ok());
    EXPECT_EQ(chunks.size(), static_cast<std::size_t>(4));
    EXPECT_EQ(reply.value().getString("text"), std::string("part0 part1 part2 part3 "));
}

EDEN_TEST(sidecar_maps_error_codes) {
    SidecarClient client(fakeOptions());
    EXPECT_EQ(client.call("fail", Json::object({{"code", Json("auth")}})).error().code, ErrorCode::Auth);
    EXPECT_EQ(client.call("fail", Json::object({{"code", Json("rate_limit")}})).error().code, ErrorCode::RateLimited);
    EXPECT_EQ(client.call("fail", Json::object({{"code", Json("invalid_argument")}})).error().code,
              ErrorCode::InvalidArgument);
    EXPECT_EQ(client.call("fail").error().code, ErrorCode::Provider);
    EXPECT_EQ(client.call("no.such").error().code, ErrorCode::NotFound);
    EXPECT_EQ(client.call("fail").error().message, std::string("boom"));
}

EDEN_TEST(sidecar_times_out_slow_calls_and_stays_usable) {
    SidecarClient client(fakeOptions());
    CallOptions options;
    options.timeoutMs = 300;
    auto slow = client.call("slow", Json::object({{"seconds", Json(5)}}), options);
    EXPECT_FALSE(slow.ok());
    EXPECT_EQ(slow.error().code, ErrorCode::Timeout);
    EXPECT_TRUE(client.call("echo", Json::object()).ok());
}

EDEN_TEST(sidecar_cancel_flag_cancels_running_work) {
    SidecarClient client(fakeOptions());
    std::atomic<bool> flag{false};
    CallOptions options;
    options.cancelFlag = &flag;
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        flag = true;
    });
    const auto started = std::chrono::steady_clock::now();
    auto result = client.call("slow", Json::object({{"seconds", Json(10)}}), options);
    canceller.join();
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
    EXPECT_TRUE(std::chrono::steady_clock::now() - started < std::chrono::seconds(4));
}

EDEN_TEST(sidecar_handles_concurrent_calls) {
    SidecarClient client(fakeOptions());
    EXPECT_TRUE(client.start().ok());
    std::vector<std::thread> threads;
    std::atomic<int> good{0};
    for (int i = 0; i < 16; ++i) {
        threads.emplace_back([&, i] {
            auto reply = client.call("echo", Json::object({{"i", Json(i)}}));
            if (reply.ok() && reply.value().getInt("i") == i) ++good;
        });
    }
    for (std::thread& thread : threads) thread.join();
    EXPECT_EQ(good.load(), 16);
}

EDEN_TEST(sidecar_restarts_after_a_crash) {
    SidecarClient client(fakeOptions());
    EXPECT_TRUE(client.start().ok());
    auto crashed = client.call("crash");
    EXPECT_FALSE(crashed.ok());
    for (int i = 0; i < 100 && client.running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto again = client.call("echo", Json::object({{"x", Json(1)}}));
    EXPECT_TRUE(again.ok());
    EXPECT_EQ(client.restarts(), 1);
}

EDEN_TEST(sidecar_without_autorestart_reports_dead_process) {
    SidecarOptions options = fakeOptions();
    options.autoRestart = false;
    SidecarClient client(options);
    EXPECT_TRUE(client.start().ok());
    client.call("crash");
    for (int i = 0; i < 100 && client.running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(client.call("echo").ok());
}

EDEN_TEST(sidecar_forwards_stderr_and_async_calls_complete) {
    std::mutex mutex;
    std::vector<std::string> lines;
    SidecarOptions options = fakeOptions();
    options.onStderr = [&](const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex);
        lines.push_back(line);
    };
    SidecarClient client(options);
    EXPECT_TRUE(client.call("stderr").ok());
    std::atomic<bool> done{false};
    std::string echoed;
    client.callAsync("echo", Json::object({{"v", Json("async")}}), {}, [&](Result<Json> result) {
        if (result.ok()) echoed = result.value().getString("v");
        done = true;
    });
    for (int i = 0; i < 200 && !done.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_TRUE(done.load());
    EXPECT_EQ(echoed, std::string("async"));
    for (int i = 0; i < 100; ++i) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!lines.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_TRUE(!lines.empty() && lines[0] == "diagnostic line");
}

EDEN_TEST(sidecar_rejects_bad_commands) {
    SidecarOptions options;
    SidecarClient empty(options);
    EXPECT_FALSE(empty.start().ok());
    options.command = {"/nonexistent/program"};
    options.startupTimeoutMs = 500;
    SidecarClient missing(options);
    EXPECT_FALSE(missing.start().ok());
}
