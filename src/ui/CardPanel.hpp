#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QWidget>

#include <map>
#include <memory>

#include "MediaCapture.hpp"
#include "app/AppController.hpp"
#include "app/CardPanelModel.hpp"

namespace eden {

class CardPanel : public QWidget {
    Q_OBJECT
public:
    explicit CardPanel(AppController& app, QWidget* parent = nullptr);

    void showPack(const QString& packId);
    void applyTheme();
    QString currentPackId() const { return packId_; }

signals:
    void cardChanged();

private slots:
    void handleLink(const QUrl& url);
    void postComment();
    void attachMedia(CommentKind kind);
    void toggleRecording(bool video);

private:
    void ensureCard();
    void rebuild();
    void runFunction(const std::string& functionId, bool refresh);
    void renderDiscussion();
    QString commentHtml(const CommentView& view) const;
    void buildTabs();

    AppController& app_;
    FunctionDirectory directory_;
    std::unique_ptr<CommentService> comments_;
    std::unique_ptr<CardPanelModel> model_;
    CardView view_;
    QString packId_;
    QString replyTo_;
    bool isAuthor_ = false;
    QTabWidget* tabs_;
    QTextBrowser* overview_;
    QTextBrowser* sources_;
    QTextBrowser* discussion_;
    QPlainTextEdit* composer_;
    QCheckBox* privateBox_;
    QCheckBox* writerBox_;
    QPushButton* voiceButton_;
    QPushButton* cameraButton_;
    QPushButton* searchButton_;
    QComboBox* collectionBox_;
    void refreshCollections();
    MediaCapture* capture_;
    QString capturePath_;
    CommentKind captureKind_ = CommentKind::Voice;
    std::map<std::string, QTextBrowser*> sectionViews_;
    std::map<std::string, QPushButton*> runButtons_;
    QString streamed_;
};

}  // namespace eden
