#include "BackgroundWidget.hpp"

#include <QElapsedTimer>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <cmath>

namespace eden {

namespace {

QColor colorOf(const std::string& hex, double alpha = 1.0) {
    QColor color(QString::fromStdString(hex));
    color.setAlphaF(std::max(0.0, std::min(1.0, alpha)));
    return color;
}

}  // namespace

BackgroundWidget::BackgroundWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
    configure(builtinBackgrounds()[1], 80, false);
}

void BackgroundWidget::configure(const BackgroundPreset& preset, int particleCount, bool reducedMotion) {
    preset_ = preset;
    reduced_ = reducedMotion;
    simulation_ = std::make_unique<BackgroundSimulation>(preset, particleCount, 11);
    simulation_->resize(width(), height());
    simulation_->setReducedMotion(reducedMotion);
    if (isVisible() && !reducedMotion) timer_.start();
    else timer_.stop();
    update();
}

void BackgroundWidget::tick() {
    static QElapsedTimer clock;
    const double dt = clock.isValid() ? clock.restart() / 1000.0 : 0.033;
    if (!clock.isValid()) clock.start();
    simulation_->step(dt);
    update();
}

void BackgroundWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (simulation_) simulation_->resize(width(), height());
}

void BackgroundWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!reduced_) timer_.start();
}

void BackgroundWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    timer_.stop();
}

void BackgroundWidget::drawLogo(QPainter& painter, const SimPoint& at) {
    painter.save();
    painter.translate(at.x, at.y);
    QRadialGradient halo(QPointF(0, 0), 46);
    halo.setColorAt(0.0, colorOf("#f2c94c", 0.55));
    halo.setColorAt(1.0, colorOf("#f2c94c", 0.0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(halo);
    painter.drawEllipse(QPointF(0, 0), 46, 46);
    painter.setPen(QPen(colorOf("#f2c94c", 0.9), 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(0, 0), 20, 20);
    painter.drawLine(QPointF(0, 14), QPointF(0, -10));
    painter.drawArc(QRectF(-12, -16, 24, 18), 20 * 16, 140 * 16);
    painter.drawLine(QPointF(0, 2), QPointF(-8, -4));
    painter.drawLine(QPointF(0, 2), QPointF(8, -4));
    painter.restore();
}

void BackgroundWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient base(0, 0, 0, height());
    base.setColorAt(0.0, colorOf(preset_.gradientTop));
    base.setColorAt(1.0, colorOf(preset_.gradientBottom));
    painter.fillRect(rect(), base);
    if (!simulation_ || reduced_) return;
    const SimFrame& frame = simulation_->frame();

    for (const SimStar& star : frame.stars) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colorOf(preset_.particleColor, star.alpha * 0.8));
        painter.drawEllipse(QPointF(star.x, star.y), star.size, star.size);
    }
    for (const SimBand& band : frame.bands) {
        QPainterPath path;
        const int steps = 40;
        for (int i = 0; i <= steps; ++i) {
            const double x = width() * i / steps;
            const double y = band.baseY + band.amplitude * std::sin(x / band.wavelength * 6.2832 + band.phase);
            if (i == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        painter.setPen(QPen(colorOf(band.color, band.alpha), band.thickness, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    for (const SimStreak& streak : frame.streaks) {
        painter.setPen(QPen(colorOf(preset_.particleColor, streak.alpha), 1.2));
        painter.drawLine(QPointF(streak.x1, streak.y1), QPointF(streak.x2, streak.y2));
    }
    for (const SimParticle& particle : frame.particles) {
        QRadialGradient glow(QPointF(particle.x, particle.y), particle.size * 3.2);
        glow.setColorAt(0.0, colorOf(preset_.particleColor, particle.alpha));
        glow.setColorAt(1.0, colorOf(preset_.particleColor, 0.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(QPointF(particle.x, particle.y), particle.size * 3.2, particle.size * 3.2);
    }
    for (const SimBolt& bolt : frame.bolts) {
        QPainterPath path;
        for (std::size_t i = 0; i < bolt.points.size(); ++i) {
            if (i == 0) path.moveTo(bolt.points[i].x, bolt.points[i].y);
            else path.lineTo(bolt.points[i].x, bolt.points[i].y);
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(colorOf("#ffd9d0", bolt.alpha * 0.35), 7.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
        painter.setPen(QPen(colorOf("#ffffff", bolt.alpha), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }
    if (frame.flash > 0.0) painter.fillRect(rect(), colorOf("#ffd9d0", frame.flash));
    if (preset_.riverFlow) drawLogo(painter, frame.logo);
}

}  // namespace eden
