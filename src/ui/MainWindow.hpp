#pragma once

#include <QDockWidget>
#include <QMainWindow>
#include <QProgressBar>
#include <QStackedWidget>

#include <vector>
#include "app/AppController.hpp"

namespace eden {

class LibraryView;
class ReaderView;
class CardPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppController& app, QWidget* parent = nullptr);

private slots:
    void importDocuments();
    void showLibrary();
    void openDocument(const QString& documentId);
    void openSettings();
    void applyTheme();
    void showCard(const QString& packId);
    void exportCurrentCard();
    void exportDocument();
    void exportCollection();
    void prepareResearch();
    void publishEnhanced();

private:
    void runExport(const std::vector<Id>& packIds, const Id& docId, const Id& collectionId, const QString& heading);

    AppController& app_;
    QStackedWidget* stack_;
    LibraryView* library_;
    ReaderView* reader_;
    QDockWidget* cardDock_;
    CardPanel* card_;
    QProgressBar* taskProgress_;
};

}  // namespace eden
