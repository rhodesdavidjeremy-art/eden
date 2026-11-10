#include "LibraryView.hpp"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include "Convert.hpp"

namespace eden {

namespace {

constexpr int kCardWidth = 300;
constexpr int kCardHeight = 128;

}  // namespace

void LibraryDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF bounds = QRectF(option.rect).adjusted(8, 8, -8, -8);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    QLinearGradient fill(bounds.topLeft(), bounds.bottomRight());
    fill.setColorAt(0.0, QColor(QString::fromStdString(mixColors(palette_->surfaceAlt, palette_->gold, hovered ? 0.16 : 0.06))));
    fill.setColorAt(1.0, QColor(QString::fromStdString(palette_->surface)));
    QPainterPath path;
    path.addRoundedRect(bounds, 14, 14);
    painter->fillPath(path, fill);

    QLinearGradient rim(bounds.topLeft(), bounds.bottomRight());
    rim.setColorAt(0.0, QColor(QString::fromStdString(palette_->ruby)));
    rim.setColorAt(0.35, QColor(QString::fromStdString(palette_->gold)));
    rim.setColorAt(0.7, QColor(QString::fromStdString(palette_->emerald)));
    rim.setColorAt(1.0, QColor(QString::fromStdString(palette_->sapphire)));
    painter->setPen(QPen(QBrush(rim), selected ? 3.0 : 1.6));
    painter->drawPath(path);

    const QRectF textArea = bounds.adjusted(16, 12, -16, -12);
    QFont titleFont = option.font;
    titleFont.setBold(true);
    titleFont.setPointSizeF(option.font.pointSizeF() * 1.25);
    painter->setFont(titleFont);
    painter->setPen(QColor(QString::fromStdString(palette_->gold)));
    const QString title = index.data(TitleRole).toString();
    painter->drawText(QRectF(textArea.left(), textArea.top(), textArea.width(), 44), Qt::AlignLeft | Qt::TextWordWrap,
                      painter->fontMetrics().elidedText(title, Qt::ElideRight, static_cast<int>(textArea.width()) * 2));

    painter->setFont(option.font);
    painter->setPen(QColor(QString::fromStdString(palette_->mutedText)));
    painter->drawText(QRectF(textArea.left(), textArea.top() + 48, textArea.width(), 20), Qt::AlignLeft,
                      index.data(SubtitleRole).toString());
    const int cards = index.data(CardsRole).toInt();
    painter->drawText(QRectF(textArea.left(), textArea.top() + 68, textArea.width(), 20), Qt::AlignLeft,
                      cards == 1 ? QStringLiteral("1 research card") : QStringLiteral("%1 research cards").arg(cards));

    const QRectF track(textArea.left(), bounds.bottom() - 22, textArea.width(), 6);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(QString::fromStdString(palette_->border)));
    painter->drawRoundedRect(track, 3, 3);
    const double progress = index.data(ProgressRole).toDouble();
    if (progress > 0.0) {
        QLinearGradient bar(track.topLeft(), track.topRight());
        bar.setColorAt(0.0, QColor(QString::fromStdString(palette_->ruby)));
        bar.setColorAt(0.5, QColor(QString::fromStdString(palette_->gold)));
        bar.setColorAt(1.0, QColor(QString::fromStdString(palette_->emerald)));
        painter->setBrush(bar);
        painter->drawRoundedRect(QRectF(track.left(), track.top(), track.width() * progress, track.height()), 3, 3);
    }
    painter->restore();
}

QSize LibraryDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const { return QSize(kCardWidth, kCardHeight); }

LibraryView::LibraryView(AppController& app, QWidget* parent) : QWidget(parent), app_(app) {
    setObjectName("libraryPage");
    background_ = new BackgroundWidget(this);
    background_->lower();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    auto* title = new QLabel("Library", this);
    title->setObjectName("title");
    layout->addWidget(title);

    auto* controls = new QHBoxLayout();
    search_ = new QLineEdit(this);
    search_->setPlaceholderText("Search your documents");
    search_->setClearButtonEnabled(true);
    sort_ = new QComboBox(this);
    sort_->addItems({"Recently opened", "Title", "Recently added", "Progress"});
    controls->addWidget(search_, 1);
    controls->addWidget(sort_);
    layout->addLayout(controls);

    model_ = new QStandardItemModel(this);
    list_ = new QListView(this);
    list_->setObjectName("library");
    list_->setModel(model_);
    list_->setViewMode(QListView::IconMode);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setSpacing(6);
    list_->setUniformItemSizes(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setMouseTracking(true);
    delegate_ = new LibraryDelegate(&app_.theme().activePalette(), list_);
    list_->setItemDelegate(delegate_);
    layout->addWidget(list_, 1);

    connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(sort_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(list_, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        emit openRequested(index.data(LibraryDelegate::IdRole).toString());
    });
    connect(list_, &QListView::customContextMenuRequested, this, &LibraryView::showContextMenu);
    applyTheme();
    refresh();
}

void LibraryView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    background_->setGeometry(rect());
}

void LibraryView::applyTheme() {
    const ThemeManager& theme = app_.theme();
    background_->configure(theme.activeBackground(), theme.particleCount(), theme.settings().reducedMotion);
    delegate_->setPalette(&app_.theme().activePalette());
    list_->viewport()->update();
}

void LibraryView::refresh() {
    LibraryQuery query;
    query.text = ui::s(search_->text());
    switch (sort_->currentIndex()) {
        case 1: query.sort = LibrarySort::Title; break;
        case 2: query.sort = LibrarySort::Added; break;
        case 3: query.sort = LibrarySort::Progress; break;
        default: query.sort = LibrarySort::Recent; break;
    }
    auto entries = app_.library().entries(query);
    model_->clear();
    if (!entries.ok()) return;
    for (const LibraryEntry& entry : entries.value()) {
        auto* item = new QStandardItem();
        item->setData(ui::q(entry.document.id), LibraryDelegate::IdRole);
        item->setData(ui::q(entry.document.title), LibraryDelegate::TitleRole);
        item->setData(ui::q(entry.subtitle), LibraryDelegate::SubtitleRole);
        item->setData(entry.progress, LibraryDelegate::ProgressRole);
        item->setData(static_cast<int>(entry.cardCount), LibraryDelegate::CardsRole);
        item->setEditable(false);
        model_->appendRow(item);
    }
}

void LibraryView::showContextMenu(const QPoint& position) {
    const QModelIndex index = list_->indexAt(position);
    if (!index.isValid()) return;
    const QString id = index.data(LibraryDelegate::IdRole).toString();
    QMenu menu(this);
    QAction* open = menu.addAction("Open");
    QAction* rename = menu.addAction("Rename...");
    QAction* remove = menu.addAction("Remove from library");
    QAction* chosen = menu.exec(list_->viewport()->mapToGlobal(position));
    if (chosen == open) {
        emit openRequested(id);
    } else if (chosen == rename) {
        bool accepted = false;
        const QString title = QInputDialog::getText(this, "Rename document", "Title:", QLineEdit::Normal,
                                                    index.data(LibraryDelegate::TitleRole).toString(), &accepted);
        if (accepted) {
            Status status = app_.library().rename(ui::s(id), ui::s(title));
            if (!status.ok()) QMessageBox::warning(this, "Rename failed", ui::q(status.error().message));
            refresh();
        }
    } else if (chosen == remove) {
        const auto answer = QMessageBox::question(this, "Remove document",
                                                  "Remove this document and all of its research cards and comments?");
        if (answer == QMessageBox::Yes) {
            Status status = app_.library().remove(ui::s(id));
            if (!status.ok()) QMessageBox::warning(this, "Remove failed", ui::q(status.error().message));
            refresh();
        }
    }
}

}  // namespace eden
