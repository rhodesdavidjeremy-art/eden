#include "ReaderView.hpp"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>

#include "Convert.hpp"

namespace eden {

namespace {

constexpr int kGemSize = 16;

QColor gemColor(const Palette& palette, const PackMark& mark) {
    if (mark.hasCard) return QColor(QString::fromStdString(palette.emerald));
    switch (mark.origin) {
        case PackOrigin::Manual: return QColor(QString::fromStdString(palette.ruby));
        case PackOrigin::Author: return QColor(QString::fromStdString(palette.amethyst));
        default: return QColor(QString::fromStdString(palette.sapphire));
    }
}

}  // namespace

GemGutter::GemGutter(ReaderView* reader) : QWidget(reader), reader_(reader) {
    setFixedWidth(30);
    setMouseTracking(true);
}

void GemGutter::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(QString::fromStdString(reader_->palette().surface)));
    painter.setPen(QColor(QString::fromStdString(reader_->palette().border)));
    painter.drawLine(width() - 1, 0, width() - 1, height());
    if (!reader_->hasDocument()) return;
    for (const PackMark& mark : reader_->currentView().marks) {
        if (mark.continuesFromPrevious) continue;
        const QRect box = reader_->gemRect(mark);
        if (box.isNull() || box.bottom() < 0 || box.top() > height()) continue;
        const QColor base = gemColor(reader_->palette(), mark);
        QPainterPath diamond;
        const QPointF center = box.center();
        const double radius = kGemSize / 2.0;
        diamond.moveTo(center.x(), center.y() - radius);
        diamond.lineTo(center.x() + radius * 0.8, center.y());
        diamond.lineTo(center.x(), center.y() + radius);
        diamond.lineTo(center.x() - radius * 0.8, center.y());
        diamond.closeSubpath();
        QLinearGradient facet(box.topLeft(), box.bottomRight());
        facet.setColorAt(0.0, base.lighter(165));
        facet.setColorAt(0.5, base);
        facet.setColorAt(1.0, base.darker(160));
        painter.setPen(QPen(QColor(QString::fromStdString(reader_->palette().gold)), 1.2));
        painter.setBrush(facet);
        painter.drawPath(diamond);
    }
}

void GemGutter::mousePressEvent(QMouseEvent* event) {
    if (!reader_->hasDocument()) return;
    for (const PackMark& mark : reader_->currentView().marks) {
        if (mark.continuesFromPrevious) continue;
        if (reader_->gemRect(mark).adjusted(-4, -4, 4, 4).contains(event->position().toPoint())) {
            emit gemClicked(ui::q(mark.packId));
            return;
        }
    }
}

ReaderView::ReaderView(AppController& app, QWidget* parent) : QWidget(parent), app_(app) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    toc_ = new QTreeWidget(splitter);
    toc_->setHeaderLabel("Contents");
    toc_->setMinimumWidth(200);
    toc_->setMaximumWidth(340);

    auto* reading = new QWidget(splitter);
    auto* readingLayout = new QHBoxLayout(reading);
    readingLayout->setContentsMargins(0, 0, 0, 0);
    readingLayout->setSpacing(0);
    gutter_ = new GemGutter(this);
    text_ = new QPlainTextEdit(reading);
    text_->setReadOnly(true);
    text_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    text_->setContextMenuPolicy(Qt::CustomContextMenu);
    text_->setCursorWidth(0);
    text_->setFrameShape(QFrame::NoFrame);
    text_->setViewportMargins(18, 14, 18, 14);
    text_->viewport()->installEventFilter(this);
    readingLayout->addWidget(gutter_);
    readingLayout->addWidget(text_, 1);
    splitter->addWidget(toc_);
    splitter->addWidget(reading);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 900});
    outer->addWidget(splitter, 1);

    auto* bar = new QWidget(this);
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(12, 6, 12, 6);
    previousButton_ = new QPushButton("\u2039 Previous", bar);
    nextButton_ = new QPushButton("Next \u203A", bar);
    pageSpin_ = new QSpinBox(bar);
    pageSpin_->setMinimum(1);
    pageLabel_ = new QLabel(bar);
    chapterLabel_ = new QLabel(bar);
    chapterLabel_->setObjectName("muted");
    progress_ = new QProgressBar(bar);
    progress_->setRange(0, 1000);
    progress_->setTextVisible(false);
    progress_->setFixedWidth(160);
    find_ = new QLineEdit(bar);
    find_->setPlaceholderText("Find in document");
    find_->setClearButtonEnabled(true);
    find_->setMaximumWidth(260);
    barLayout->addWidget(previousButton_);
    barLayout->addWidget(pageSpin_);
    barLayout->addWidget(pageLabel_);
    barLayout->addWidget(nextButton_);
    barLayout->addSpacing(12);
    barLayout->addWidget(chapterLabel_, 1);
    barLayout->addWidget(progress_);
    barLayout->addWidget(find_);
    outer->addWidget(bar);

    connect(previousButton_, &QPushButton::clicked, this, &ReaderView::goPrevious);
    connect(nextButton_, &QPushButton::clicked, this, &ReaderView::goNext);
    connect(pageSpin_, &QSpinBox::valueChanged, this, &ReaderView::goToSpinPage);
    connect(toc_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) { goToChapterItem(item); });
    connect(find_, &QLineEdit::returnPressed, this, &ReaderView::findNextMatch);
    connect(text_, &QPlainTextEdit::customContextMenuRequested, this, &ReaderView::showTextMenu);
    connect(text_->verticalScrollBar(), &QScrollBar::valueChanged, gutter_, qOverload<>(&QWidget::update));
    connect(gutter_, &GemGutter::gemClicked, this, &ReaderView::packActivated);
    applyTheme();
}

ReaderView::~ReaderView() { closeDocument(); }

void ReaderView::applyTheme() {
    QFont font(QString::fromStdString("Georgia"));
    font.setPointSizeF(app_.theme().pointSize(13.0));
    text_->setFont(font);
    if (model_) applyMarks();
    gutter_->update();
}

QString ReaderView::documentTitle() const { return model_ ? ui::q(model_->document().title) : QString(); }

bool ReaderView::openDocument(const QString& documentId) {
    closeDocument();
    Status status;
    model_ = app_.openReader(ui::s(documentId), &status);
    if (!model_) {
        QMessageBox::warning(this, "Open failed", ui::q(status.error().message));
        return false;
    }
    rebuildToc();
    render();
    return true;
}

void ReaderView::closeDocument() {
    if (!model_) return;
    model_.reset();
    view_ = PageView();
    text_->clear();
    toc_->clear();
    emit documentClosed();
}

void ReaderView::rebuildToc() {
    toc_->clear();
    const auto& chapters = model_->chapters();
    QTreeWidgetItem* parents[7] = {nullptr};
    for (std::size_t i = 0; i < chapters.size(); ++i) {
        const int level = std::min(std::max(chapters[i].level, 1), 6);
        QTreeWidgetItem* item = nullptr;
        if (level > 1 && parents[level - 1] != nullptr) {
            item = new QTreeWidgetItem(parents[level - 1]);
        } else {
            item = new QTreeWidgetItem(toc_);
        }
        item->setText(0, ui::q(chapters[i].title));
        item->setData(0, Qt::UserRole, static_cast<qulonglong>(i));
        parents[level] = item;
    }
    toc_->expandAll();
}

void ReaderView::render() {
    if (!model_) return;
    auto view = model_->view();
    if (!view.ok()) return;
    rendering_ = true;
    view_ = view.value();
    text_->setPlainText(ui::q(view_.text));
    applyMarks();
    const int anchorOffset = ui::utf8ToUtf16Offset(view_.text, std::max<std::int64_t>(0, model_->currentOffset() - view_.start));
    QTextCursor cursor(text_->document());
    cursor.setPosition(std::min(anchorOffset, text_->document()->characterCount() - 1));
    text_->setTextCursor(cursor);
    text_->centerCursor();
    updateControls();
    rendering_ = false;
    gutter_->update();
}

void ReaderView::updateControls() {
    pageSpin_->blockSignals(true);
    pageSpin_->setMaximum(static_cast<int>(std::max<std::int64_t>(1, model_->pageCount())));
    pageSpin_->setValue(static_cast<int>(view_.index + 1));
    pageSpin_->blockSignals(false);
    pageLabel_->setText(QString("of %1").arg(model_->pageCount()));
    chapterLabel_->setText(ui::q(view_.chapter));
    progress_->setValue(static_cast<int>(model_->progress() * 1000.0));
    previousButton_->setEnabled(view_.index > 0);
    nextButton_->setEnabled(view_.index + 1 < model_->pageCount());
}

void ReaderView::applyMarks() {
    QList<QTextEdit::ExtraSelection> selections;
    const Palette& pal = palette();
    for (const PackMark& mark : view_.marks) {
        const int from = ui::utf8ToUtf16Offset(view_.text, mark.start);
        const int to = ui::utf8ToUtf16Offset(view_.text, mark.end);
        QTextEdit::ExtraSelection selection;
        QTextCursor cursor(text_->document());
        cursor.setPosition(from);
        cursor.setPosition(to, QTextCursor::KeepAnchor);
        selection.cursor = cursor;
        QTextCharFormat format;
        format.setUnderlineStyle(mark.hasCard ? QTextCharFormat::SingleUnderline : QTextCharFormat::DotLine);
        format.setUnderlineColor(QColor(QString::fromStdString(mark.hasCard ? pal.emerald : pal.gold)));
        if (mark.hasCard) format.setBackground(QColor(QString::fromStdString(withAlpha(pal.emerald, 0.12))));
        selection.format = format;
        selections.append(selection);
    }
    text_->setExtraSelections(selections);
}

QRect ReaderView::gemRect(const PackMark& mark) const {
    const int from = ui::utf8ToUtf16Offset(view_.text, mark.start);
    QTextCursor cursor(text_->document());
    cursor.setPosition(from);
    const QRect local = text_->cursorRect(cursor);
    const QPoint inGutter = gutter_->mapFromGlobal(text_->viewport()->mapToGlobal(local.topLeft()));
    return QRect(gutter_->width() / 2 - kGemSize / 2, inGutter.y() + (local.height() - kGemSize) / 2, kGemSize, kGemSize);
}

std::int64_t ReaderView::globalOffsetAt(int utf16Position) const {
    return view_.start + ui::utf16ToUtf8Offset(text_->toPlainText(), utf16Position);
}

void ReaderView::goNext() {
    if (model_ && model_->next().ok()) render();
}

void ReaderView::goPrevious() {
    if (model_ && model_->previous().ok()) render();
}

void ReaderView::goToSpinPage(int page) {
    if (rendering_ || !model_) return;
    if (model_->goToPage(page - 1).ok()) render();
}

void ReaderView::goToChapterItem(QTreeWidgetItem* item) {
    if (!model_ || item == nullptr) return;
    if (model_->goToChapter(item->data(0, Qt::UserRole).toULongLong()).ok()) render();
}

void ReaderView::selectGlobalRange(std::int64_t start, std::int64_t end) {
    if (!model_) return;
    if (model_->goToOffset(start).ok()) render();
    const int from = ui::utf8ToUtf16Offset(view_.text, start - view_.start);
    const int to = ui::utf8ToUtf16Offset(view_.text, end - view_.start);
    QTextCursor cursor(text_->document());
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    text_->setTextCursor(cursor);
    text_->centerCursor();
}

void ReaderView::findNextMatch() {
    if (!model_ || find_->text().isEmpty()) return;
    const std::string needle = ui::s(find_->text());
    const std::int64_t from = globalOffsetAt(text_->textCursor().selectionEnd());
    auto found = model_->findNext(needle, from);
    if (!found.has_value()) found = model_->findNext(needle, 0);
    if (found.has_value()) selectGlobalRange(*found, *found + static_cast<std::int64_t>(needle.size()));
}

void ReaderView::findPreviousMatch() {
    if (!model_ || find_->text().isEmpty()) return;
    const std::string needle = ui::s(find_->text());
    auto found = model_->findPrevious(needle, globalOffsetAt(text_->textCursor().selectionStart()));
    if (found.has_value()) selectGlobalRange(*found, *found + static_cast<std::int64_t>(needle.size()));
}

void ReaderView::createPackFromSelection() {
    if (!model_) return;
    const QTextCursor cursor = text_->textCursor();
    if (!cursor.hasSelection()) return;
    const std::int64_t start = globalOffsetAt(cursor.selectionStart());
    const std::int64_t end = globalOffsetAt(cursor.selectionEnd());
    auto created = model_->createPack(start, end, true);
    if (!created.ok()) {
        QMessageBox::warning(this, "Could not create pack", ui::q(created.error().message));
        return;
    }
    render();
    emit packsChanged();
    emit packActivated(ui::q(created.value().id));
}

void ReaderView::showTextMenu(const QPoint& position) {
    if (!model_) return;
    QMenu menu(this);
    const bool hasSelection = text_->textCursor().hasSelection();
    QAction* create = menu.addAction("Create Research Pack from selection");
    create->setEnabled(hasSelection);
    QAction* copy = menu.addAction("Copy");
    copy->setEnabled(hasSelection);
    const QTextCursor under = text_->cursorForPosition(position);
    auto pack = model_->packAt(globalOffsetAt(under.position()));
    QAction* open = nullptr;
    QAction* remove = nullptr;
    QAction* bookmark = nullptr;
    menu.addSeparator();
    if (pack.ok()) {
        open = menu.addAction("Open Research Card");
        remove = menu.addAction("Remove this pack");
    }
    bookmark = menu.addAction("Add bookmark here");
    QAction* chosen = menu.exec(text_->viewport()->mapToGlobal(position));
    if (chosen == nullptr) return;
    if (chosen == create) {
        createPackFromSelection();
    } else if (chosen == copy) {
        text_->copy();
    } else if (chosen == open && pack.ok()) {
        emit packActivated(ui::q(pack.value().id));
    } else if (chosen == remove && pack.ok()) {
        model_->removePack(pack.value().id);
        render();
        emit packsChanged();
    } else if (chosen == bookmark) {
        model_->addBookmark("");
    }
}

bool ReaderView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == text_->viewport() && event->type() == QEvent::MouseButtonRelease && model_) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && !text_->textCursor().hasSelection()) {
            const QTextCursor under = text_->cursorForPosition(mouse->position().toPoint());
            auto pack = model_->packAt(globalOffsetAt(under.position()));
            if (pack.ok()) emit packActivated(ui::q(pack.value().id));
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace eden
