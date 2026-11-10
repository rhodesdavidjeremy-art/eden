#include "ai/EmbeddedPython.hpp"

#ifdef EDEN_EMBED_PYTHON

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <unistd.h>

#include <cstdio>
#include <mutex>
#include <system_error>
#include <thread>

namespace eden {

namespace {

std::mutex initMutex;
bool initialized = false;
std::string initFailure;

// Runs `<module>.__main__.main(args)` with the interpreter's stdio bound to the pipe descriptors.
const char* const runnerScript = R"PY(
def _eden_run(_a):
    import io, os, sys
    stdin = stdout = stderr = None
    saved = (sys.stdin, sys.stdout, sys.stderr)
    code = 1
    try:
        stdin = io.open(_a["in"], "r", encoding="utf-8", newline="\n", closefd=True)
        stdout = io.open(_a["out"], "w", encoding="utf-8", newline="\n", closefd=True)
        stderr = io.open(_a["err"], "w", encoding="utf-8", newline="\n", buffering=1, closefd=True)
        sys.stdin, sys.stdout, sys.stderr = stdin, stdout, stderr
        os.environ.update(_a["env"])
        for path in reversed(_a["paths"]):
            if path and path not in sys.path:
                sys.path.insert(0, path)
        import importlib
        module = importlib.import_module(_a["module"] + ".__main__")
        code = module.main(_a["args"]) or 0
    except SystemExit as exc:
        code = exc.code if isinstance(exc.code, int) else 1
    except BaseException:
        try:
            import traceback
            traceback.print_exc(file=sys.stderr)
        except BaseException:
            pass
        code = 1
    finally:
        sys.stdin, sys.stdout, sys.stderr = saved
        for stream in (stdin, stdout, stderr):
            if stream is not None:
                try:
                    stream.close()
                except BaseException:
                    pass
    return int(code)

_eden_exit = _eden_run(__import__("json").loads(_eden_args))
)PY";

std::string jsonString(const std::string& text) {
    std::string out = "\"";
    for (const unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
    return out;
}

Status ensureInterpreter(const std::string& home) {
    std::lock_guard<std::mutex> lock(initMutex);
    if (initialized) return Status::success();
    if (!initFailure.empty()) return makeError(ErrorCode::Internal, initFailure);
    PyPreConfig preconfig;
    PyPreConfig_InitIsolatedConfig(&preconfig);
    preconfig.utf8_mode = 1;
    PyStatus status = Py_PreInitialize(&preconfig);
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.install_signal_handlers = 0;
    config.pathconfig_warnings = 0;
    if (!PyStatus_Exception(status)) status = PyConfig_SetBytesString(&config, &config.home, home.c_str());
    if (!PyStatus_Exception(status)) status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
        initFailure = std::string("Python could not start: ") + (status.err_msg != nullptr ? status.err_msg : "unknown error");
        return makeError(ErrorCode::Internal, initFailure);
    }
    PyEval_SaveThread();  // release the GIL held after initialisation; run threads use PyGILState_Ensure
    initialized = true;
    return Status::success();
}

void runEmbedded(std::shared_ptr<EmbeddedRun> run, std::string home, std::string argumentsJson, int inFd, int outFd,
                 int errFd) {
    Status ready = ensureInterpreter(home);
    if (!ready.ok()) {
        const std::string message = ready.error().message + "\n";
        const ssize_t ignored = ::write(errFd, message.data(), message.size());
        (void)ignored;
        ::close(inFd);
        ::close(outFd);
        ::close(errFd);
        run->exitCode = 1;
        run->finished = true;
        return;
    }
    int code = 1;
    PyGILState_STATE gil = PyGILState_Ensure();
    {
        PyObject* mainModule = PyImport_AddModule("__main__");  // borrowed
        PyObject* globals = PyModule_GetDict(mainModule);       // borrowed
        PyObject* arguments = PyUnicode_FromString(argumentsJson.c_str());
        if (arguments != nullptr) {
            PyDict_SetItemString(globals, "_eden_args", arguments);
            Py_DECREF(arguments);
            const int status = PyRun_SimpleString(runnerScript);
            PyObject* exitValue = PyDict_GetItemString(globals, "_eden_exit");  // borrowed
            if (status == 0 && exitValue != nullptr && PyLong_Check(exitValue)) {
                code = static_cast<int>(PyLong_AsLong(exitValue));
            }
            PyErr_Clear();
        } else {
            PyErr_Clear();
        }
    }
    PyGILState_Release(gil);
    run->exitCode = code;
    run->finished = true;
}

}  // namespace

Result<std::shared_ptr<EmbeddedRun>> startEmbeddedPython(const std::vector<std::string>& argv,
                                                         const std::map<std::string, std::string>& env,
                                                         int stdinFd, int stdoutFd, int stderrFd) {
    if (argv.size() < 3 || argv[1] != "-m") {
        return makeError(ErrorCode::InvalidArgument, "the embedded interpreter runs: -m <module> [arguments]");
    }
    const auto home = env.find("PYTHONHOME");
    if (home == env.end() || home->second.empty()) {
        return makeError(ErrorCode::InvalidArgument, "PYTHONHOME must point to the extracted Python runtime");
    }
    std::string paths = "[";
    const auto pythonPath = env.find("PYTHONPATH");
    if (pythonPath != env.end()) {
        std::size_t start = 0;
        bool first = true;
        while (start <= pythonPath->second.size()) {
            std::size_t end = pythonPath->second.find(':', start);
            if (end == std::string::npos) end = pythonPath->second.size();
            if (end > start) {
                if (!first) paths += ",";
                paths += jsonString(pythonPath->second.substr(start, end - start));
                first = false;
            }
            start = end + 1;
        }
    }
    paths += "]";
    std::string environment = "{";
    bool firstEnvironment = true;
    for (const auto& [key, value] : env) {
        if (!firstEnvironment) environment += ",";
        environment += jsonString(key) + ":" + jsonString(value);
        firstEnvironment = false;
    }
    environment += "}";
    std::string arguments = "[";
    for (std::size_t i = 3; i < argv.size(); ++i) {
        if (i > 3) arguments += ",";
        arguments += jsonString(argv[i]);
    }
    arguments += "]";
    const std::string json = "{\"in\":" + std::to_string(stdinFd) + ",\"out\":" + std::to_string(stdoutFd) +
                             ",\"err\":" + std::to_string(stderrFd) + ",\"env\":" + environment + ",\"paths\":" + paths +
                             ",\"module\":" + jsonString(argv[2]) + ",\"args\":" + arguments + "}";
    auto run = std::make_shared<EmbeddedRun>();
    try {
        std::thread(runEmbedded, run, home->second, json, stdinFd, stdoutFd, stderrFd).detach();
    } catch (const std::system_error& error) {
        return makeError(ErrorCode::Io, std::string("cannot start the Python thread: ") + error.what());
    }
    return run;
}

}  // namespace eden

#endif  // EDEN_EMBED_PYTHON
