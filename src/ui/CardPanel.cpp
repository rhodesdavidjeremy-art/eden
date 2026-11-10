#include "CardPanel.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include "Convert.hpp"
#include "PlatformFiles.hpp"
#include <QInputDialog>

#include "repo/CardRepository.hpp"
#include "repo/CollectionRepository.hpp"
#include "repo/PackRepository.hpp"

namespace eden {

CardPanel::CardPanel(AppController& app, QWidget* parent) : QWidget(parent), app_(app) {
    const QString mediaDir = ui::q(app_.paths().dataDirectory + "/media");
    comments_ = std::make_unique<CommentService>(app_.database(), ui::s(mediaDir), app_.user().id);
    capture_ = new MediaCapture(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_);
    connect(capture_, &MediaCapture::finished, this, [this](const QString& path) {
        auto added = comments_->addMedia(ui::s(view_.cardId), captureKind_, ui::s(path), ui::s(composer_->toPlainText()),
                                         ui::s(replyTo_), -1, 0, privateBox_->isChecked());
        voiceButton_->setText("Record voice");
        cameraButton_->setText("Record camera");
        if (!added.ok()) QMessageBox::warning(this, "Recording not saved", ui::q(added.error().message));
        composer_->clear();
        replyTo_.clear();
        rebuild();
    });
    connect(capture_, &MediaCapture::failed, this, [this](const QString& reason) {
        voiceButton_->setText("Record voice");
        cameraButton_->setText("Record camera");
        QMessageBox::warning(this, "Recording failed", reason);
    });
    buildTabs();
}

void CardPanel::buildTabs() {
    auto loaded = loadFunctionDirectory(app_.sidecar(), app_.database());
    if (loaded.ok()) directory_ = loaded.value();
    model_ = std::make_unique<CardPanelModel>(app_.database(), directory_, *comments_);

    overview_ = new QTextBrowser(this);
    auto* overviewPage = new QWidget(this);
    auto* overviewLayout = new QVBoxLayout(overviewPage);
    overviewLayout->addWidget(overview_, 1);
    auto* collectionRow = new QHBoxLayout();
    collectionBox_ = new QComboBox(overviewPage);
    auto* addToCollection = new QPushButton("Add to collection", overviewPage);
    auto* newCollection = new QPushButton("New collection...", overviewPage);
    collectionRow->addWidget(collectionBox_, 1);
    collectionRow->addWidget(addToCollection);
    collectionRow->addWidget(newCollection);
    overviewLayout->addLayout(collectionRow);
    tabs_->addTab(overviewPage, "Overview");
    connect(addToCollection, &QPushButton::clicked, this, [this] {
        if (collectionBox_->currentIndex() < 0 || packId_.isEmpty()) return;
        CollectionRepository collections(app_.database());
        Status status = collections.addPack(ui::s(collectionBox_->currentData().toString()), ui::s(packId_));
        if (!status.ok()) QMessageBox::warning(this, "Could not add", ui::q(status.error().message));
    });
    connect(newCollection, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, "New collection", "Name:", QLineEdit::Normal, QString(), &accepted);
        if (!accepted || name.trimmed().isEmpty()) return;
        Collection collection;
        collection.ownerId = app_.user().id;
        collection.name = ui::s(name.trimmed());
        auto created = CollectionRepository(app_.database()).create(collection);
        if (!created.ok()) QMessageBox::warning(this, "Could not create", ui::q(created.error().message));
        refreshCollections();
    });
    refreshCollections();
    for (const std::string& group : directory_.groups()) {
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        auto* container = new QWidget(scroll);
        auto* column = new QVBoxLayout(container);
        for (const FunctionInfo* info : directory_.inGroup(group)) {
            auto* box = new QGroupBox(ui::q(info->title), container);
            auto* inner = new QVBoxLayout(box);
            auto* button = new QPushButton("Run", box);
            auto* view = new QTextBrowser(box);
            view->setOpenExternalLinks(true);
            view->setMinimumHeight(60);
            view->hide();
            inner->addWidget(button, 0, Qt::AlignLeft);
            inner->addWidget(view);
            column->addWidget(box);
            sectionViews_[info->id] = view;
            runButtons_[info->id] = button;
            connect(button, &QPushButton::clicked, this, [this, id = info->id] {
                runFunction(id, runButtons_[id]->text() == "Refresh");
            });
        }
        column->addStretch(1);
        scroll->setWidget(container);
        tabs_->addTab(scroll, ui::q(FunctionDirectory::groupTitle(group)));
    }

    sources_ = new QTextBrowser(this);
    sources_->setOpenExternalLinks(true);
    auto* sourcesPage = new QWidget(this);
    auto* sourcesLayout = new QVBoxLayout(sourcesPage);
    searchButton_ = new QPushButton("Search the web for this passage", sourcesPage);
    sourcesLayout->addWidget(searchButton_);
    sourcesLayout->addWidget(sources_);
    tabs_->addTab(sourcesPage, "Sources");
    connect(searchButton_, &QPushButton::clicked, this, [this] {
        auto found = app_.cards().search(view_.passage.substr(0, 200), "web", 8);
        if (!found.ok()) {
            QMessageBox::warning(this, "Search failed", ui::q(found.error().message));
            return;
        }
        ensureCard();
        std::vector<std::size_t> picks;
        for (std::size_t i = 0; i < found.value().get("results").size(); ++i) picks.push_back(i);
        app_.cards().saveSearchResults(ui::s(QString(view_.cardId.c_str())), found.value(), picks);
        rebuild();
    });

    auto* discussionPage = new QWidget(this);
    auto* discussionLayout = new QVBoxLayout(discussionPage);
    discussion_ = new QTextBrowser(discussionPage);
    discussion_->setOpenLinks(false);
    connect(discussion_, &QTextBrowser::anchorClicked, this, &CardPanel::handleLink);
    composer_ = new QPlainTextEdit(discussionPage);
    composer_->setPlaceholderText("Add to the discussion");
    composer_->setMaximumHeight(90);
    privateBox_ = new QCheckBox("Private", discussionPage);
    privateBox_->setChecked(true);
    writerBox_ = new QCheckBox("Post as Writer-Original", discussionPage);
    writerBox_->hide();
    auto* actions = new QHBoxLayout();
    auto* post = new QPushButton("Post", discussionPage);
    auto* image = new QPushButton("Image", discussionPage);
    auto* video = new QPushButton("Video", discussionPage);
    auto* audio = new QPushButton("Audio", discussionPage);
    voiceButton_ = new QPushButton("Record voice", discussionPage);
    cameraButton_ = new QPushButton("Record camera", discussionPage);
    for (QWidget* widget : std::initializer_list<QWidget*>{post, image, video, audio, voiceButton_, cameraButton_, privateBox_, writerBox_}) {
        actions->addWidget(widget);
    }
    discussionLayout->addWidget(discussion_, 1);
    discussionLayout->addWidget(composer_);
    discussionLayout->addLayout(actions);
    tabs_->addTab(discussionPage, "Discussion");
    connect(post, &QPushButton::clicked, this, &CardPanel::postComment);
    connect(image, &QPushButton::clicked, this, [this] { attachMedia(CommentKind::Image); });
    connect(video, &QPushButton::clicked, this, [this] { attachMedia(CommentKind::Video); });
    connect(audio, &QPushButton::clicked, this, [this] { attachMedia(CommentKind::Voice); });
    connect(voiceButton_, &QPushButton::clicked, this, [this] { toggleRecording(false); });
    connect(cameraButton_, &QPushButton::clicked, this, [this] { toggleRecording(true); });
}

void CardPanel::refreshCollections() {
    collectionBox_->clear();
    auto listed = CollectionRepository(app_.database()).listByOwner(app_.user().id);
    if (!listed.ok()) return;
    for (const Collection& collection : listed.value()) collectionBox_->addItem(ui::q(collection.name), ui::q(collection.id));
}

void CardPanel::applyTheme() {
    if (!view_.packId.empty()) rebuild();
}

void CardPanel::ensureCard() {
    if (!view_.cardId.empty()) return;
    CardRepository cards(app_.database());
    auto card = cards.getOrCreateByPack(ui::s(packId_));
    if (card.ok()) view_.cardId = card.value().id;
}

void CardPanel::showPack(const QString& packId) {
    packId_ = packId;
    replyTo_.clear();
    rebuild();
    tabs_->setCurrentIndex(0);
    const bool hasSummary = !view_.summary.empty();
    if (!hasSummary) runFunction("summary.plain", false);
}

void CardPanel::rebuild() {
    auto loaded = model_->load(ui::s(packId_));
    if (!loaded.ok()) return;
    view_ = loaded.value();
    const Palette& pal = app_.theme().activePalette();
    QString html = QString("<h3 style='color:%1'>Passage</h3><p>%2</p>").arg(ui::q(pal.gold), ui::q(escapeHtml(view_.passage)));
    if (!view_.summary.empty()) {
        html += QString("<h3 style='color:%1'>Summary</h3><p>%2</p>").arg(ui::q(pal.gold), ui::q(escapeHtml(view_.summary)));
    }
    html += QString("<p style='color:%1'>%2 of %3 research functions completed - %4 comments</p>")
                .arg(ui::q(pal.mutedText)).arg(view_.filledSections).arg(directory_.all().size()).arg(view_.commentCount);
    overview_->setHtml(html);
    for (auto& [id, browser] : sectionViews_) {
        browser->hide();
        runButtons_[id]->setText("Run");
    }
    for (const SectionView& section : view_.sections) {
        auto found = sectionViews_.find(section.functionId);
        if (found == sectionViews_.end() || !section.hasContent) continue;
        QString body = ui::q(blocksToHtml(section.blocks, pal));
        if (!section.provider.empty()) body += QString("<p style='color:%1'>%2 \xC2\xB7 %3</p>").arg(ui::q(pal.mutedText), ui::q(section.provider), ui::q(section.model));
        found->second->setHtml(body);
        found->second->show();
        runButtons_[section.functionId]->setText("Refresh");
    }
    QString sourceHtml;
    for (const Source& source : view_.sources) {
        sourceHtml += QString("<p><a href='%1'>%2</a><br><span style='color:%3'>%4 - %5</span></p>")
                          .arg(ui::q(source.url), ui::q(escapeHtml(source.title.empty() ? source.url : source.title)),
                               ui::q(pal.mutedText), ui::q(source.publisher), ui::q(escapeHtml(source.snippet)));
    }
    sources_->setHtml(sourceHtml.isEmpty() ? "<p>No saved sources yet.</p>" : sourceHtml);
    auto owner = view_.cardId.empty() ? Result<Id>(makeError(ErrorCode::NotFound, "")) : comments_->documentOwnerForCard(view_.cardId);
    isAuthor_ = owner.ok() && owner.value() == app_.user().id;
    writerBox_->setVisible(isAuthor_);
    renderDiscussion();
}

QString CardPanel::commentHtml(const CommentView& view) const {
    const Palette& pal = app_.theme().activePalette();
    const Comment& c = view.comment;
    const QString id = ui::q(c.id);
    QString html = QString("<div style='margin-left:%1px;border-left:2px solid %2;padding-left:8px;margin-bottom:10px'>")
                       .arg(view.depth * 18).arg(ui::q(c.isWriterOriginal ? pal.gold : pal.border));
    html += QString("<b>%1</b>").arg(ui::q(escapeHtml(view.authorName)));
    if (c.isWriterOriginal) html += QString(" <span style='color:%1'>\xE2\x9C\xA6 Author</span>").arg(ui::q(pal.gold));
    if (c.isPrivate) html += " <i>(private)</i>";
    if (c.hidden) html += " <i>(hidden)</i>";
    html += "<br>" + ui::q(replaceAll(escapeHtml(c.body), "\n", "<br>"));
    if (!c.mediaUri.empty()) {
        if (c.kind == CommentKind::Image) html += QString("<br><img src='file://%1' width='320'>").arg(ui::q(view.mediaPath));
        else html += QString("<br><a href='eden:play/%1'>\xE2\x96\xB6 Play %2</a>").arg(id, ui::q(toString(c.kind)));
    }
    html += "<br>";
    for (const std::string& reaction : CommentService::jewelReactions()) {
        int count = 0;
        for (const ReactionCount& item : view.reactions) count += item.reaction == reaction ? static_cast<int>(item.count) : 0;
        const bool mine = std::find(view.viewerReactions.begin(), view.viewerReactions.end(), reaction) != view.viewerReactions.end();
        html += QString("<a href='eden:%1/%2/%3'>%4%5</a> ").arg(mine ? "unreact" : "react", id, ui::q(reaction),
                                                                  ui::q(reaction.substr(0, 1) == "r" ? "\xE2\x99\xA6" : "\xE2\x97\x86"), count > 0 ? QString::number(count) : QString());
    }
    html += QString(" <a href='eden:reply/%1'>Reply</a>").arg(id);
    if (view.canDelete) html += QString(" <a href='eden:delete/%1'>Delete</a>").arg(id);
    if (view.canModerate) html += QString(" <a href='eden:%1/%2'>%3</a>").arg(c.hidden ? "unhide" : "hide", id, c.hidden ? "Unhide" : "Hide");
    return html + "</div>";
}

void CardPanel::renderDiscussion() {
    QString html;
    for (const CommentView& view : view_.comments) html += commentHtml(view);
    if (!replyTo_.isEmpty()) html += "<p><i>Replying to a comment - type below and press Post.</i></p>";
    discussion_->setHtml(html.isEmpty() ? "<p>No comments yet.</p>" : html);
}

void CardPanel::handleLink(const QUrl& url) {
    const QStringList parts = url.toString().mid(5).split('/');
    if (parts.size() < 2) return;
    const std::string action = ui::s(parts[0]);
    const std::string id = ui::s(parts[1]);
    Status status = Status::success();
    if (action == "react") status = comments_->react(id, ui::s(parts.value(2)));
    else if (action == "unreact") status = comments_->unreact(id, ui::s(parts.value(2)));
    else if (action == "delete") status = comments_->remove(id);
    else if (action == "hide") status = comments_->moderate(id, true);
    else if (action == "unhide") status = comments_->moderate(id, false);
    else if (action == "reply") {
        replyTo_ = parts[1];
        composer_->setFocus();
    } else if (action == "play") {
        for (const CommentView& view : view_.comments) {
            if (view.comment.id == id) QDesktopServices::openUrl(QUrl::fromLocalFile(ui::q(view.mediaPath)));
        }
        return;
    }
    if (!status.ok()) QMessageBox::warning(this, "Not allowed", ui::q(status.error().message));
    rebuild();
}

void CardPanel::postComment() {
    ensureCard();
    const std::string body = ui::s(composer_->toPlainText());
    auto added = writerBox_->isVisible() && writerBox_->isChecked()
                     ? comments_->addWriterOriginal(view_.cardId, body)
                     : comments_->addText(view_.cardId, body, ui::s(replyTo_), -1, privateBox_->isChecked());
    if (!added.ok()) {
        QMessageBox::warning(this, "Comment not posted", ui::q(added.error().message));
        return;
    }
    composer_->clear();
    replyTo_.clear();
    rebuild();
    emit cardChanged();
}

void CardPanel::attachMedia(CommentKind kind) {
    ensureCard();
    const QString filter = kind == CommentKind::Image ? "Images (*.png *.jpg *.jpeg *.gif *.webp)"
                           : kind == CommentKind::Video ? "Video (*.mp4 *.mov *.webm)"
                                                        : "Audio (*.wav *.mp3 *.m4a *.ogg *.flac *.webm)";
    const QString file = QFileDialog::getOpenFileName(this, "Attach media", QString(), filter);
    if (file.isEmpty()) return;
    auto added = comments_->addMedia(view_.cardId, kind, ui::s(ui::localCopyForRead(file)), ui::s(composer_->toPlainText()), ui::s(replyTo_), -1, 0,
                                     privateBox_->isChecked());
    if (!added.ok()) {
        QMessageBox::warning(this, "Could not attach", ui::q(added.error().message));
        return;
    }
    composer_->clear();
    replyTo_.clear();
    rebuild();
}

void CardPanel::toggleRecording(bool video) {
    if (capture_->recording()) {
        capture_->stop();
        return;
    }
    ensureCard();
    captureKind_ = video ? CommentKind::Camera : CommentKind::Voice;
    QDir().mkpath(ui::q(app_.paths().dataDirectory + "/recordings"));
    capturePath_ = ui::q(app_.paths().dataDirectory + "/recordings/") + QString::number(QDateTime::currentMSecsSinceEpoch()) +
                   (video ? ".mp4" : ".m4a");
    if (capture_->start(video, capturePath_)) (video ? cameraButton_ : voiceButton_)->setText("Stop recording");
}

void CardPanel::runFunction(const std::string& functionId, bool refresh) {
    RunOptions options;
    options.refresh = refresh;
    streamed_.clear();
    auto* target = sectionViews_.count(functionId) ? sectionViews_[functionId] : overview_;
    if (target != overview_) target->show();
    const QString pack = packId_;
    app_.runCardAsync(
        ui::s(pack), functionId, options,
        [this, target, pack](const std::string& chunk) {
            if (pack != packId_ || target == overview_) return;
            streamed_ += ui::q(chunk);
            target->setPlainText(streamed_);
        },
        [this, pack](Result<CardResult> result) {
            if (pack != packId_) return;
            if (!result.ok()) {
                QMessageBox::information(this, "Research unavailable",
                                         ui::q(result.error().message) + "\n\nAdd an API key under Settings to enable AI research.");
            }
            rebuild();
            emit cardChanged();
        });
}

}  // namespace eden
