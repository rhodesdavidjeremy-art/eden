#include <chrono>
#include <thread>

#include "TestFramework.hpp"
#include "ai/ChildProcess.hpp"

using namespace eden;

EDEN_TEST(process_round_trips_lines_through_cat) {
    auto spawned = ChildProcess::spawn({"cat"});
    EXPECT_TRUE(spawned.ok());
    ChildProcess& process = *spawned.value();
    EXPECT_TRUE(process.writeAll("alpha\nbeta\n"));
    std::string line;
    EXPECT_TRUE(process.readLine(line, 2000) == ReadStatus::Line);
    EXPECT_EQ(line, std::string("alpha"));
    EXPECT_TRUE(process.readLine(line, 2000) == ReadStatus::Line);
    EXPECT_EQ(line, std::string("beta"));
    EXPECT_TRUE(process.readLine(line, 50) == ReadStatus::Timeout);
    process.closeStdin();
    EXPECT_TRUE(process.readLine(line, 2000) == ReadStatus::Closed);
    EXPECT_EQ(process.wait(2000), 0);
}

EDEN_TEST(process_reports_exit_codes_and_stderr) {
    auto spawned = ChildProcess::spawn({"sh", "-c", "echo oops 1>&2; exit 7"});
    EXPECT_TRUE(spawned.ok());
    std::string line;
    EXPECT_TRUE(spawned.value()->readErrorLine(line, 2000) == ReadStatus::Line);
    EXPECT_EQ(line, std::string("oops"));
    EXPECT_EQ(spawned.value()->wait(2000), 7);
}

EDEN_TEST(process_passes_environment_and_working_directory) {
    auto spawned = ChildProcess::spawn({"sh", "-c", "echo $EDEN_TEST_VALUE; pwd"}, {{"EDEN_TEST_VALUE", "gold"}}, "/tmp");
    EXPECT_TRUE(spawned.ok());
    std::string first;
    std::string second;
    EXPECT_TRUE(spawned.value()->readLine(first, 2000) == ReadStatus::Line);
    EXPECT_TRUE(spawned.value()->readLine(second, 2000) == ReadStatus::Line);
    EXPECT_EQ(first, std::string("gold"));
    EXPECT_EQ(second, std::string("/tmp"));
}

EDEN_TEST(process_terminate_stops_long_running_children) {
    auto spawned = ChildProcess::spawn({"sleep", "30"});
    EXPECT_TRUE(spawned.ok());
    EXPECT_TRUE(spawned.value()->running());
    spawned.value()->terminate(1000);
    EXPECT_FALSE(spawned.value()->running());
}

EDEN_TEST(process_missing_binary_exits_127_and_empty_command_is_rejected) {
    auto spawned = ChildProcess::spawn({"/nonexistent/binary"});
    EXPECT_TRUE(spawned.ok());
    EXPECT_EQ(spawned.value()->wait(2000), 127);
    EXPECT_FALSE(ChildProcess::spawn({}).ok());
}

EDEN_TEST(process_write_after_exit_fails_cleanly) {
    auto spawned = ChildProcess::spawn({"true"});
    EXPECT_TRUE(spawned.ok());
    spawned.value()->wait(2000);
    bool failed = false;
    for (int i = 0; i < 50 && !failed; ++i) failed = !spawned.value()->writeAll(std::string(65536, 'x'));
    EXPECT_TRUE(failed);
}
