// Smoke test for the in-app Python path used on Android: unpack a bundle, start the sidecar inside this process,
// talk to it over the same pipes SidecarClient uses, restart it, and make sure a missing module fails cleanly.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ai/ChildProcess.hpp"
#include "ai/EmbeddedPython.hpp"
#include "app/BundleExtractor.hpp"
#include "doc/Zip.hpp"

using namespace eden;

static int failures = 0;
static void expect(bool condition, const char* what) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) ++failures;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <bundle.zip> <extract-dir>\n", argv[0]);
        return 2;
    }
    const std::string bundle = argv[1], root = argv[2];
    auto bytes = readFileBytes(bundle);
    expect(bytes.ok(), "bundle readable");
    if (!bytes.ok()) return 1;
    Status extracted = extractZipBundle(bytes.value(), root);
    expect(extracted.ok(), "bundle extracts");
    if (!extracted.ok()) return 1;

    const std::map<std::string, std::string> env = {
        {"PYTHONHOME", root + "/python"}, {"PYTHONPATH", root + "/eden-python"}, {"PYTHONUTF8", "1"}};
    for (int run = 1; run <= 2; ++run) {  // second pass = sidecar restart on the same interpreter
        auto spawned = ChildProcess::spawn({kEmbeddedPythonCommand, "-m", "eden_ai"}, env);
        expect(spawned.ok(), "sidecar starts in-process");
        if (!spawned.ok()) return 1;
        ChildProcess& process = *spawned.value();
        process.writeAll("{\"id\":\"1\",\"method\":\"cache.stats\",\"params\":{}}\n");
        std::string line;
        expect(process.readLine(line, 30000) == ReadStatus::Line && line.find("\"ok\":true") != std::string::npos,
               "sidecar answers a request");
        process.closeStdin();
        expect(process.wait(10000) == 0, "sidecar exits cleanly when stdin closes");
    }
    auto bad = ChildProcess::spawn({kEmbeddedPythonCommand, "-m", "module_that_does_not_exist"}, env);
    expect(bad.ok(), "bad module still spawns");
    if (bad.ok()) expect(bad.value()->wait(10000) == 1, "bad module exits with status 1");
    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
