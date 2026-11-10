#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QListView>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QWidget>

#include "app/AppController.hpp"
#include "BackgroundWidget.hpp"

namespace eden {

class LibraryDelegate : public QStyledItemDelegate {
public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, SubtitleRole, ProgressRole, CardsRole };

    explicit LibraryDelegate(const Palette* palette, QObject* parent = nullptr) : QStyledItemDelegate(parent), palette_(palette) {}
    void setPalette(const Palette* palette) { palette_ = palette; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    const Palette* palette_;
};

class LibraryView : public QWidget {
    Q_OBJECT
public:
    explicit LibraryView(AppController& app, QWidget* parent = nullptr);

    void refresh();
    void applyTheme();

signals:
    void openRequested(const QString& documentId);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void showContextMenu(const QPoint& position);

private:
    AppController& app_;
    BackgroundWidget* background_;
    QLineEdit* search_;
    QComboBox* sort_;
    QListView* list_;
    QStandardItemModel* model_;
    LibraryDelegate* delegate_;
};

}  // namespace eden
