#pragma once

#include <QString>

namespace eden::ui {

struct PythonRuntime {
    QString home;        // extracted CPython prefix (contains lib/python3.x)
    QString sidecarDir;  // folder holding the eden_ai package
    QString error;       // empty on success
    bool ok() const { return error.isEmpty(); }
};

// Android: unpacks assets:/python-bundle.zip (CPython stdlib + the eden_ai sidecar) into <dataRoot>/runtime on
// the first start of each installed build and returns where everything is.
PythonRuntime preparePythonRuntime(const QString& dataRoot);

}  // namespace eden::ui
