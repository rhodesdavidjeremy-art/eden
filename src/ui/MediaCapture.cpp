#include "MediaCapture.hpp"

#include <QCoreApplication>
#include <QUrl>
#if defined(Q_OS_ANDROID) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermission>
#define EDEN_ANDROID_PERMISSIONS 1
#endif

namespace eden {

MediaCapture::MediaCapture(QObject* parent) : QObject(parent) {}

#ifdef EDEN_HAS_MULTIMEDIA

bool MediaCapture::start(bool withVideo, const QString& outputPath) {
    if (recording_) return false;
#ifdef EDEN_ANDROID_PERMISSIONS
    // Android asks at runtime: request the permission now and let the person tap record again once it is granted.
    QMicrophonePermission microphone;
    if (qApp->checkPermission(microphone) != Qt::PermissionStatus::Granted) {
        qApp->requestPermission(microphone, this, [](const QPermission&) {});
        emit failed("Allow microphone access, then tap record again.");
        return false;
    }
    if (withVideo) {
        QCameraPermission camera;
        if (qApp->checkPermission(camera) != Qt::PermissionStatus::Granted) {
            qApp->requestPermission(camera, this, [](const QPermission&) {});
            emit failed("Allow camera access, then tap record again.");
            return false;
        }
    }
#endif
    path_ = outputPath;
    delete recorder_;
    delete camera_;
    delete audio_;
    camera_ = nullptr;
    audio_ = new QAudioInput(this);
    session_.setAudioInput(audio_);
    if (withVideo) {
        camera_ = new QCamera(this);
        session_.setCamera(camera_);
    } else {
        session_.setCamera(nullptr);
    }
    recorder_ = new QMediaRecorder(this);
    session_.setRecorder(recorder_);
    connect(recorder_, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString& message) {
        recording_ = false;
        emit failed(message);
    });
    connect(recorder_, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState && recording_) {
            recording_ = false;
            emit finished(path_);
        }
    });
    recorder_->setOutputLocation(QUrl::fromLocalFile(outputPath));
    if (camera_) camera_->start();
    recorder_->record();
    recording_ = true;
    return true;
}

void MediaCapture::stop() {
    if (!recording_ || recorder_ == nullptr) return;
    recorder_->stop();
    if (camera_) camera_->stop();
}

#else

bool MediaCapture::start(bool, const QString&) {
#ifdef EDEN_WEB_BUILD
    emit failed("The server has no microphone or camera. Use the Record link above the desktop to record in your "
                "browser, then attach the saved file here.");
#else
    emit failed("This build does not include Qt Multimedia, so recording is unavailable. Attach a file instead.");
#endif
    return false;
}

void MediaCapture::stop() {}

#endif

}  // namespace eden
