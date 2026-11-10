#include "MainWindow.hpp"

#include <QAction>
#include <algorithm>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>

#include "Convert.hpp"
#include "CardPanel.hpp"
#include "ExportDialog.hpp"
#include "LibraryView.hpp"
#include "PlatformFiles.hpp"
#include "ReaderView.hpp"
#include "SettingsDialog.hpp"
#include "common/Json.hpp"
#include "repo/CardRepository.hpp"
#include "repo/CollectionRepository.hpp"
#include "repo/PackRepository.hpp"

namespace eden {

MainWindow::MainWindow(AppController& app, QWidget* parent) : QMainWindow(parent), app_(app) {
    setWindowTitle("Eden Research Reader");
    stack_ = new QStackedWidget(this);
    library_ = new LibraryView(app_, stack_);
    reader_ = new ReaderView(app_, stack_);
    stack_->addWidget(library_);
    stack_->addWidget(reader_);
    setCentralWidget(stack_);

    auto* toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    QAction* libraryAction = toolbar->addAction("Library");
    QAction* importAction = toolbar->addAction("Import...");
    toolbar->addSeparator();
    auto* exportButton = new QToolButton(toolbar);
    exportButton->setText("Export");
    exportButton->setPopupMode(QToolButton::InstantPopup);
    auto* exportMenu = new QMenu(exportButton);
    exportMenu->addAction("Current passage...", this, &MainWindow::exportCurrentCard);
    exportMenu->addAction("Whole document study guide...", this, &MainWindow::exportDocument);
    exportMenu->addAction("Collection...", this, &MainWindow::exportCollection);
    exportMenu->addSeparator();
    exportMenu->addAction("Prepare research for every passage (author)...", this, &MainWindow::prepareResearch);
    exportMenu->addAction("Publish Enhanced Eden document...", this, &MainWindow::publishEnhanced);
    exportButton->setMenu(exportMenu);
    toolbar->addWidget(exportButton);
    toolbar->addSeparator();
    QAction* settingsAction = toolbar->addAction("Settings");
    connect(libraryAction, &QAction::triggered, this, &MainWindow::showLibrary);
    connect(importAction, &QAction::triggered, this, &MainWindow::importDocuments);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);

    cardDock_ = new QDockWidget("Research Card", this);
    card_ = new CardPanel(app_, cardDock_);
    cardDock_->setWidget(card_);
    cardDock_->setMinimumWidth(460);
    addDockWidget(Qt::RightDockWidgetArea, cardDock_);
    cardDock_->hide();

    taskProgress_ = new QProgressBar(this);
    taskProgress_->setRange(0, 100);
    taskProgress_->setFixedWidth(220);
    taskProgress_->hide();
    statusBar()->addPermanentWidget(taskProgress_);

    connect(library_, &LibraryView::openRequested, this, &MainWindow::openDocument);
    connect(reader_, &ReaderView::packActivated, this, &MainWindow::showCard);
    connect(card_, &CardPanel::cardChanged, reader_, &ReaderView::packsChanged);
    applyTheme();
    stack_->setCurrentWidget(library_);
}

void MainWindow::applyTheme() {
    qApp->setStyleSheet(ui::q(app_.theme().styleSheet()));
    library_->applyTheme();
    reader_->applyTheme();
    card_->applyTheme();
}

void MainWindow::showLibrary() {
    reader_->closeDocument();
    cardDock_->hide();
    library_->refresh();
    stack_->setCurrentWidget(library_);
    setWindowTitle("Eden Research Reader");
}

void MainWindow::openDocument(const QString& documentId) {
    if (!reader_->openDocument(documentId)) return;
    stack_->setCurrentWidget(reader_);
    setWindowTitle(reader_->documentTitle() + " - Eden Research Reader");
    auto unlocked = app_.unlockProviders();
    if (!unlocked.ok()) statusBar()->showMessage("AI helper unavailable: " + ui::q(unlocked.error().message), 8000);
}

void MainWindow::importDocuments() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Import documents", QString(), "Documents (*.pdf *.docx *.txt *.md *.epub *.eden);;All files (*)");
    for (const QString& file : files) {
        taskProgress_->setValue(0);
        taskProgress_->show();
        app_.importAsync(
            ui::s(ui::localCopyForRead(file)), {},
            [this](const TaskInfo& info) {
                taskProgress_->setValue(static_cast<int>(info.progress * 100.0));
                statusBar()->showMessage(ui::q(info.message));
            },
            [this, file](Result<ImportResult> result) {
                taskProgress_->hide();
                if (!result.ok()) {
                    QMessageBox::warning(this, "Import failed", file + "\n\n" + ui::q(result.error().message));
                    return;
                }
                const ImportResult& imported = result.value();
                statusBar()->showMessage(imported.alreadyExisted
                                             ? "Already in your library: " + ui::q(imported.document.title)
                                             : QString("Imported %1 with %2 research packs")
                                                   .arg(ui::q(imported.document.title))
                                                   .arg(imported.packCount),
                                         8000);
                library_->refresh();
            });
    }
}

void MainWindow::openSettings() {
    SettingsDialog dialog(app_, this);
    connect(&dialog, &SettingsDialog::themeChanged, this, &MainWindow::applyTheme);
    dialog.exec();
}

void MainWindow::runExport(const std::vector<Id>& packIds, const Id& docId, const Id& collectionId, const QString& heading) {
    ExportDialog dialog(heading, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const ExportOptions options = dialog.options();
    auto bundle = app_.buildExport(packIds, docId, collectionId, ui::s(heading), options);
    if (!bundle.ok()) {
        QMessageBox::information(this, "Nothing to export", ui::q(bundle.error().message));
        return;
    }
    const QString suggested = ui::q(suggestedFileName(bundle.value(), dialog.format()));
    const QString path = QFileDialog::getSaveFileName(this, "Save export", suggested);
    if (path.isEmpty()) return;
    const ui::SaveTarget saveTarget = ui::prepareSave(path);
    Status status = app_.exportTo(dialog.format(), bundle.value(), options, ui::s(saveTarget.localPath));
    if (!status.ok()) {
        QMessageBox::warning(this, "Export failed", ui::q(status.error().message));
        return;
    }
    QString copyError;
    if (!ui::finishSave(saveTarget, &copyError)) {
        QMessageBox::warning(this, "Export failed", copyError);
        return;
    }
    statusBar()->showMessage("Saved " + path, 8000);
    QDesktopServices::openUrl(ui::urlForOpening(path));
}

void MainWindow::exportCurrentCard() {
    if (!reader_->hasDocument() || card_->currentPackId().isEmpty()) {
        QMessageBox::information(this, "Export", "Open a research card first.");
        return;
    }
    runExport({ui::s(card_->currentPackId())}, "", "", "Research card");
}

void MainWindow::exportDocument() {
    if (!reader_->hasDocument()) {
        QMessageBox::information(this, "Export", "Open a document first.");
        return;
    }
    runExport({}, reader_->model()->document().id, "", reader_->documentTitle() + " - Study Guide");
}

void MainWindow::exportCollection() {
    CollectionRepository collections(app_.database());
    auto listed = collections.listByOwner(app_.user().id);
    if (!listed.ok() || listed.value().empty()) {
        QMessageBox::information(this, "Export", "You have no collections yet. Use \"Add to collection\" on a research card.");
        return;
    }
    QStringList names;
    for (const Collection& collection : listed.value()) names << ui::q(collection.name);
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(this, "Export collection", "Collection:", names, 0, false, &accepted);
    if (!accepted) return;
    runExport({}, "", listed.value()[static_cast<std::size_t>(names.indexOf(chosen))].id, chosen);
}

void MainWindow::prepareResearch() {
    if (!reader_->hasDocument()) {
        QMessageBox::information(this, "Author mode", "Open the document you want to prepare first.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Prepare research cards");
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    const std::vector<std::string> defaults = {"summary.plain", "keypoints", "glossary", "study_questions"};
    for (const FunctionInfo& info : app_.functions().all()) {
        auto* item = new QListWidgetItem(ui::q(info.title), list);
        item->setData(Qt::UserRole, ui::q(info.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(std::find(defaults.begin(), defaults.end(), info.id) != defaults.end() ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    std::vector<std::string> chosen;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->checkState() == Qt::Checked) chosen.push_back(ui::s(list->item(i)->data(Qt::UserRole).toString()));
    }
    if (chosen.empty()) return;
    taskProgress_->setValue(0);
    taskProgress_->show();
    app_.authorBatchAsync(
        reader_->model()->document().id, chosen,
        [this](const TaskInfo& info) {
            taskProgress_->setValue(static_cast<int>(info.progress * 100.0));
            statusBar()->showMessage(ui::q(info.message));
        },
        [this](Result<AuthorBatchReport> result) {
            taskProgress_->hide();
            if (!result.ok()) {
                QMessageBox::warning(this, "Preparation failed", ui::q(result.error().message));
                return;
            }
            const AuthorBatchReport& report = result.value();
            QString message = QString("%1 of %2 research results saved (%3 reused from cache).").arg(report.succeeded).arg(report.total).arg(report.cached);
            if (report.failed > 0) message += QString("\n%1 failed. First problem: %2").arg(report.failed).arg(ui::q(report.errors.empty() ? "" : report.errors.front()));
            if (report.stoppedEarly) message += "\nStopped early: add an API key under Settings.";
            QMessageBox::information(this, "Author mode", message);
            reader_->packsChanged();
        });
}

void MainWindow::publishEnhanced() {
    if (!reader_->hasDocument()) {
        QMessageBox::information(this, "Publish", "Open the document you want to publish first.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Publish Enhanced Eden document", reader_->documentTitle() + ".eden", "Enhanced Eden (*.eden)");
    if (path.isEmpty()) return;
    const ui::SaveTarget saveTarget = ui::prepareSave(path);
    Status status = app_.publishEnhanced(reader_->model()->document().id, ui::s(saveTarget.localPath));
    if (!status.ok()) {
        QMessageBox::warning(this, "Publish failed", ui::q(status.error().message));
        return;
    }
    QString copyError;
    if (!ui::finishSave(saveTarget, &copyError)) {
        QMessageBox::warning(this, "Publish failed", copyError);
        return;
    }
    statusBar()->showMessage("Published " + path, 8000);
}

void MainWindow::showCard(const QString& packId) {
    cardDock_->show();
    card_->showPack(packId);
}

}  // namespace eden
