#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/Result.hpp"

namespace eden {

// Android cannot launch a separate Python process, so the sidecar runs inside the app on a thread of an
// embedded CPython. ChildProcess::spawn recognises this pseudo command and wires the sidecar's
// stdin/stdout/stderr to pipes, so SidecarClient and the sidecar protocol are unchanged.
inline constexpr const char* kEmbeddedPythonCommand = "@eden-embedded-python";

struct EmbeddedRun {
    std::atomic<bool> finished{false};
    std::atomic<int> exitCode{-1};
};

#ifdef EDEN_EMBED_PYTHON
// argv: {kEmbeddedPythonCommand, "-m", "<module>", args...}. env must hold PYTHONHOME (the extracted
// CPython prefix); PYTHONPATH (":"-separated) is added to sys.path. The three descriptors are the
// interpreter's ends of the pipes; the interpreter owns and closes them.
Result<std::shared_ptr<EmbeddedRun>> startEmbeddedPython(const std::vector<std::string>& argv,
                                                         const std::map<std::string, std::string>& env,
                                                         int stdinFd, int stdoutFd, int stderrFd);
#endif

}  // namespace eden
