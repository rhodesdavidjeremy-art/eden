#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QWidget>

#include <memory>

#include "app/AppController.hpp"

namespace eden {

class ReaderView;

class GemGutter : public QWidget {
    Q_OBJECT
public:
    explicit GemGutter(ReaderView* reader);
    QSize sizeHint() const override { return QSize(30, 0); }

signals:
    void gemClicked(const QString& packId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    ReaderView* reader_;
};

class ReaderView : public QWidget {
    Q_OBJECT
public:
    explicit ReaderView(AppController& app, QWidget* parent = nullptr);
    ~ReaderView() override;

    bool openDocument(const QString& documentId);
    void closeDocument();
    bool hasDocument() const { return model_ != nullptr; }
    void applyTheme();
    QString documentTitle() const;

    const PageView& currentView() const { return view_; }
    QPlainTextEdit* textEdit() const { return text_; }
    const Palette& palette() const { return app_.theme().activePalette(); }
    QRect gemRect(const PackMark& mark) const;
    ReaderModel* model() const { return model_.get(); }

signals:
    void packActivated(const QString& packId);
    void packsChanged();
    void documentClosed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void goNext();
    void goPrevious();
    void goToSpinPage(int page);
    void goToChapterItem(QTreeWidgetItem* item);
    void findNextMatch();
    void findPreviousMatch();
    void showTextMenu(const QPoint& position);

private:
    void render();
    void applyMarks();
    void rebuildToc();
    void selectGlobalRange(std::int64_t start, std::int64_t end);
    void createPackFromSelection();
    std::int64_t globalOffsetAt(int utf16Position) const;
    void updateControls();

    AppController& app_;
    std::unique_ptr<ReaderModel> model_;
    PageView view_;
    QPlainTextEdit* text_;
    GemGutter* gutter_;
    QTreeWidget* toc_;
    QPushButton* previousButton_;
    QPushButton* nextButton_;
    QSpinBox* pageSpin_;
    QLabel* pageLabel_;
    QLabel* chapterLabel_;
    QProgressBar* progress_;
    QLineEdit* find_;
    bool rendering_ = false;
};

}  // namespace eden
