#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSlider>

#include <map>

#include "app/AppController.hpp"

namespace eden {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(AppController& app, QWidget* parent = nullptr);

signals:
    void themeChanged();

private slots:
    void saveKey(const QString& provider);
    void removeKey(const QString& provider);
    void applyTheme();

private:
    AppController& app_;
    std::map<QString, QLineEdit*> keyFields_;
    QComboBox* palette_;
    QComboBox* background_;
    QDoubleSpinBox* fontScale_;
    QSlider* density_;
    QCheckBox* reducedMotion_;
    QCheckBox* dyslexia_;
    QCheckBox* highContrast_;
};

}  // namespace eden
