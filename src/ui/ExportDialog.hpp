#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>

#include "export/ExportModel.hpp"

namespace eden {

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(const QString& heading, QWidget* parent = nullptr);

    ExportFormat format() const;
    ExportOptions options() const;

private:
    QComboBox* format_;
    QCheckBox* comments_;
    QCheckBox* privateComments_;
    QCheckBox* sources_;
    QCheckBox* images_;
    QCheckBox* emptyCards_;
};

}  // namespace eden
