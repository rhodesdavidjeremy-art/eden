#include "SettingsDialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "Convert.hpp"

namespace eden {

SettingsDialog::SettingsDialog(AppController& app, QWidget* parent) : QDialog(parent), app_(app) {
    setWindowTitle("Settings");
    resize(620, 520);
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);

    auto* providers = new QWidget(tabs);
    auto* providerLayout = new QVBoxLayout(providers);
    auto* note = new QLabel("API keys are kept in your system keyring when one is available, otherwise in a "
                            "private file in the application data folder. Keys are never written to the library database.",
                            providers);
    note->setWordWrap(true);
    note->setObjectName("muted");
    providerLayout->addWidget(note);
    for (const QString& name : {QStringLiteral("claude"), QStringLiteral("grok"), QStringLiteral("local"), QStringLiteral("images")}) {
        auto* group = new QGroupBox(name == "images" ? QStringLiteral("Image generation") : name.left(1).toUpper() + name.mid(1), providers);
        auto* row = new QHBoxLayout(group);
        auto* field = new QLineEdit(group);
        field->setEchoMode(QLineEdit::Password);
        auto stored = app_.keys().get(ui::s(name));
        field->setPlaceholderText(stored.ok() && stored.value().has_value() ? "Key saved - enter a new key to replace it" : "Paste API key");
        auto* save = new QPushButton("Save", group);
        auto* remove = new QPushButton("Remove", group);
        row->addWidget(field, 1);
        row->addWidget(save);
        row->addWidget(remove);
        providerLayout->addWidget(group);
        keyFields_[name] = field;
        connect(save, &QPushButton::clicked, this, [this, name] { saveKey(name); });
        connect(remove, &QPushButton::clicked, this, [this, name] { removeKey(name); });
    }
    providerLayout->addStretch(1);
    tabs->addTab(providers, "AI providers");

    auto* appearance = new QWidget(tabs);
    auto* form = new QFormLayout(appearance);
    palette_ = new QComboBox(appearance);
    for (const Palette& palette : builtinPalettes()) palette_->addItem(ui::q(palette.title), ui::q(palette.name));
    palette_->setCurrentIndex(std::max(0, palette_->findData(ui::q(app_.theme().settings().palette))));
    background_ = new QComboBox(appearance);
    for (const BackgroundPreset& preset : builtinBackgrounds()) background_->addItem(ui::q(preset.title), ui::q(preset.name));
    background_->setCurrentIndex(std::max(0, background_->findData(ui::q(app_.theme().settings().background))));
    fontScale_ = new QDoubleSpinBox(appearance);
    fontScale_->setRange(0.7, 2.5);
    fontScale_->setSingleStep(0.1);
    fontScale_->setValue(app_.theme().settings().fontScale);
    density_ = new QSlider(Qt::Horizontal, appearance);
    density_->setRange(0, 100);
    density_->setValue(static_cast<int>(app_.theme().settings().particleDensity * 100.0));
    reducedMotion_ = new QCheckBox("Reduce motion (no animated backgrounds)", appearance);
    reducedMotion_->setChecked(app_.theme().settings().reducedMotion);
    dyslexia_ = new QCheckBox("Dyslexia-friendly font", appearance);
    dyslexia_->setChecked(app_.theme().settings().dyslexiaFont);
    highContrast_ = new QCheckBox("High-contrast theme", appearance);
    highContrast_->setChecked(app_.theme().settings().highContrast);
    form->addRow("Jewel palette", palette_);
    form->addRow("Living background", background_);
    form->addRow("Text size", fontScale_);
    form->addRow("Particle density", density_);
    form->addRow(reducedMotion_);
    form->addRow(dyslexia_);
    form->addRow(highContrast_);
    tabs->addTab(appearance, "Appearance");

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::applyTheme);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
}

void SettingsDialog::saveKey(const QString& provider) {
    QLineEdit* field = keyFields_[provider];
    const QString key = field->text().trimmed();
    if (key.isEmpty()) {
        QMessageBox::information(this, "API key", "Enter a key first.");
        return;
    }
    Status status = provider == "images" ? app_.keys().set("images", ui::s(key)) : app_.saveProviderKey(ui::s(provider), ui::s(key));
    if (!status.ok()) {
        QMessageBox::warning(this, "Could not save key", ui::q(status.error().message));
        return;
    }
    if (provider == "images") app_.sidecar().call("providers.set_images_key", Json::object({{"key", Json(ui::s(key))}}));
    field->clear();
    field->setPlaceholderText("Key saved - enter a new key to replace it");
}

void SettingsDialog::removeKey(const QString& provider) {
    Status status = provider == "images" ? app_.keys().remove("images") : app_.removeProviderKey(ui::s(provider));
    if (!status.ok()) {
        QMessageBox::warning(this, "Could not remove key", ui::q(status.error().message));
        return;
    }
    keyFields_[provider]->setPlaceholderText("Paste API key");
}

void SettingsDialog::applyTheme() {
    ThemeManager& theme = app_.theme();
    theme.settings().palette = ui::s(palette_->currentData().toString());
    theme.settings().background = ui::s(background_->currentData().toString());
    theme.settings().fontScale = fontScale_->value();
    theme.settings().particleDensity = density_->value() / 100.0;
    theme.settings().reducedMotion = reducedMotion_->isChecked();
    theme.settings().dyslexiaFont = dyslexia_->isChecked();
    theme.settings().highContrast = highContrast_->isChecked();
    Status saved = theme.save();
    if (!saved.ok()) QMessageBox::warning(this, "Could not save settings", ui::q(saved.error().message));
    emit themeChanged();
}

}  // namespace eden
