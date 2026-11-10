#pragma once

#include <QCoreApplication>
#include <QMetaObject>

#include <functional>

#include "app/TaskRunner.hpp"

namespace eden::ui {

inline Dispatcher makeQtDispatcher() {
    return [](std::function<void()> work) {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(), [work = std::move(work)]() { work(); }, Qt::QueuedConnection);
    };
}

}  // namespace eden::ui
