#include "ExportDialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace eden {

ExportDialog::ExportDialog(const QString& heading, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Export");
    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(heading, this);
    title->setObjectName("title");
    layout->addWidget(title);
    auto* form = new QFormLayout();
    format_ = new QComboBox(this);
    for (ExportFormat format : {ExportFormat::Docx, ExportFormat::Pdf, ExportFormat::EdenPdf, ExportFormat::Pptx, ExportFormat::Markdown,
                                ExportFormat::Html, ExportFormat::HtmlSlides, ExportFormat::AnkiTsv, ExportFormat::Text}) {
        format_->addItem(QString::fromUtf8(formatTitle(format)), static_cast<int>(format));
    }
    comments_ = new QCheckBox("Include discussion", this);
    comments_->setChecked(true);
    privateComments_ = new QCheckBox("Include my private comments", this);
    sources_ = new QCheckBox("Include saved sources", this);
    sources_->setChecked(true);
    images_ = new QCheckBox("Include generated images", this);
    images_->setChecked(true);
    emptyCards_ = new QCheckBox("Include passages without research yet", this);
    form->addRow("Format", format_);
    form->addRow(comments_);
    form->addRow(privateComments_);
    form->addRow(sources_);
    form->addRow(images_);
    form->addRow(emptyCards_);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ExportFormat ExportDialog::format() const { return static_cast<ExportFormat>(format_->currentData().toInt()); }

ExportOptions ExportDialog::options() const {
    ExportOptions options;
    options.includeComments = comments_->isChecked();
    options.includePrivateComments = privateComments_->isChecked();
    options.includeSources = sources_->isChecked();
    options.includeImages = images_->isChecked();
    options.onlyCardsWithContent = !emptyCards_->isChecked();
    return options;
}

}  // namespace eden
