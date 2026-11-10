#pragma once

#include <QTimer>
#include <QWidget>

#include <memory>

#include "app/BackgroundSimulation.hpp"

namespace eden {

class BackgroundWidget : public QWidget {
    Q_OBJECT
public:
    explicit BackgroundWidget(QWidget* parent = nullptr);

    void configure(const BackgroundPreset& preset, int particleCount, bool reducedMotion);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void tick();
    void drawLogo(QPainter& painter, const SimPoint& at);

    std::unique_ptr<BackgroundSimulation> simulation_;
    BackgroundPreset preset_;
    bool reduced_ = false;
    QTimer timer_;
};

}  // namespace eden
