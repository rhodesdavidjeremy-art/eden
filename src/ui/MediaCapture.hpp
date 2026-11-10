#pragma once

#include <QObject>
#include <QString>

#ifdef EDEN_HAS_MULTIMEDIA
#include <QAudioInput>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#endif

namespace eden {

class MediaCapture : public QObject {
    Q_OBJECT
public:
    explicit MediaCapture(QObject* parent = nullptr);
    bool start(bool withVideo, const QString& outputPath);
    void stop();
    bool recording() const { return recording_; }

signals:
    void finished(const QString& path);
    void failed(const QString& reason);

private:
    bool recording_ = false;
    QString path_;
#ifdef EDEN_HAS_MULTIMEDIA
    QMediaCaptureSession session_;
    QAudioInput* audio_ = nullptr;
    QCamera* camera_ = nullptr;
    QMediaRecorder* recorder_ = nullptr;
#endif
};

}  // namespace eden
