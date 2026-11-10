#include "app/BackgroundSimulation.hpp"

#include <algorithm>
#include <cmath>

namespace eden {

BackgroundSimulation::BackgroundSimulation(BackgroundPreset preset, int particleCount, std::uint32_t seed)
    : preset_(std::move(preset)), particleCount_(std::max(0, particleCount)), rng_(seed) {
    populate();
    nextBolt_ = preset_.lightning ? uniform(0.5, 1.5) * preset_.lightningIntervalSeconds : 0.0;
    rebuildFrame();
}

double BackgroundSimulation::uniform(double low, double high) {
    return std::uniform_real_distribution<double>(low, high)(rng_);
}

void BackgroundSimulation::resize(double width, double height) {
    width_ = std::max(1.0, width);
    height_ = std::max(1.0, height);
    populate();
    rebuildFrame();
}

void BackgroundSimulation::setReducedMotion(bool reduced) {
    reduced_ = reduced;
    rebuildFrame();
}

void BackgroundSimulation::populate() {
    river_.clear();
    drifters_.clear();
    streaks_.clear();
    stars_.clear();
    if (preset_.riverFlow) {
        for (int i = 0; i < particleCount_; ++i) {
            river_.push_back({uniform(0.0, 1.0), uniform(0.03, 0.09) * std::max(0.2, preset_.particleSpeed), uniform(-1.0, 1.0),
                              uniform(1.0, 3.2)});
        }
    } else {
        for (int i = 0; i < particleCount_; ++i) {
            drifters_.push_back({uniform(0.0, width_), uniform(0.0, height_), uniform(-1.0, 1.0) * preset_.particleSpeed * 30.0,
                                 -uniform(0.2, 1.0) * preset_.particleSpeed * 40.0, uniform(1.0, 3.0), uniform(0.0, 6.28)});
        }
    }
    if (preset_.rain) {
        for (int i = 0; i < preset_.rainStreaks; ++i) {
            streaks_.push_back({uniform(0.0, width_), uniform(0.0, height_), uniform(380.0, 720.0), uniform(10.0, 26.0)});
        }
    }
    if (preset_.stars) {
        const int count = preset_.name == "deep-space" ? std::max(particleCount_, 40) : std::max(particleCount_, 25);
        for (int i = 0; i < count; ++i) {
            stars_.push_back({uniform(0.0, width_), uniform(0.0, height_ * 0.85), uniform(0.6, 1.8), uniform(0.0, 6.28), uniform(0.4, 2.2)});
        }
    }
}

SimPoint BackgroundSimulation::riverPoint(double t) const {
    const SimPoint p0{width_ * 0.5, height_ * 0.16};
    const SimPoint p1{width_ * 0.78, height_ * 0.32};
    const SimPoint p2{width_ * 0.18, height_ * 0.62};
    const SimPoint p3{width_ * 0.84, height_ * 1.02};
    const double u = 1.0 - t;
    SimPoint out;
    out.x = u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x;
    out.y = u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y;
    return out;
}

void BackgroundSimulation::spawnBolt() {
    SimBolt main;
    main.alpha = 1.0;
    SimPoint at{uniform(width_ * 0.2, width_ * 0.8), 0.0};
    main.points.push_back(at);
    const int segments = 12;
    std::vector<SimPoint> joints;
    for (int i = 0; i < segments; ++i) {
        at.y += height_ * 0.6 / segments;
        at.x += uniform(-0.05, 0.05) * width_;
        main.points.push_back(at);
        if (i > 2 && i < segments - 2) joints.push_back(at);
    }
    bolts_.push_back(main);
    const int branches = joints.empty() ? 0 : static_cast<int>(uniform(0.0, 2.99));
    for (int b = 0; b < branches; ++b) {
        SimBolt branch;
        branch.alpha = 0.7;
        SimPoint start = joints[static_cast<std::size_t>(uniform(0.0, static_cast<double>(joints.size()) - 0.001))];
        branch.points.push_back(start);
        for (int i = 0; i < 5; ++i) {
            start.y += height_ * 0.05;
            start.x += uniform(-0.06, 0.06) * width_;
            branch.points.push_back(start);
        }
        bolts_.push_back(branch);
    }
}

void BackgroundSimulation::step(double dt) {
    if (reduced_) return;
    dt = std::clamp(dt, 0.0, 0.1);
    time_ += dt;
    for (RiverParticle& particle : river_) {
        particle.t += particle.speed * dt;
        if (particle.t > 1.0) {
            particle.t = 0.0;
            particle.lateral = uniform(-1.0, 1.0);
        }
    }
    for (Drifter& drifter : drifters_) {
        drifter.x += (drifter.vx + std::sin(time_ * 0.7 + drifter.phase) * 6.0) * dt;
        drifter.y += drifter.vy * dt;
        if (drifter.y < -10.0) drifter.y = height_ + 10.0;
        if (drifter.x < -10.0) drifter.x = width_ + 10.0;
        if (drifter.x > width_ + 10.0) drifter.x = -10.0;
    }
    for (Streak& streak : streaks_) {
        streak.y += streak.speed * dt;
        streak.x -= streak.speed * 0.18 * dt;
        if (streak.y > height_ + streak.length) {
            streak.y = -streak.length;
            streak.x = uniform(0.0, width_ * 1.2);
        }
    }
    if (preset_.lightning) {
        nextBolt_ -= dt;
        if (nextBolt_ <= 0.0) {
            spawnBolt();
            nextBolt_ = uniform(0.6, 1.4) * preset_.lightningIntervalSeconds;
        }
        for (SimBolt& bolt : bolts_) bolt.alpha -= dt * 2.8;
        bolts_.erase(std::remove_if(bolts_.begin(), bolts_.end(), [](const SimBolt& bolt) { return bolt.alpha <= 0.0; }),
                     bolts_.end());
    }
    rebuildFrame();
}

void BackgroundSimulation::rebuildFrame() {
    frame_ = SimFrame();
    frame_.logo = {width_ * 0.5, height_ * 0.16};
    if (reduced_) return;
    for (const RiverParticle& particle : river_) {
        const SimPoint center = riverPoint(particle.t);
        const double spread = (0.012 + 0.05 * particle.t) * width_;
        const SimPoint ahead = riverPoint(std::min(1.0, particle.t + 0.01));
        const double dx = ahead.x - center.x;
        const double dy = ahead.y - center.y;
        const double length = std::max(1e-6, std::sqrt(dx * dx + dy * dy));
        SimParticle out;
        out.x = center.x + (-dy / length) * particle.lateral * spread;
        out.y = center.y + (dx / length) * particle.lateral * spread;
        out.size = particle.size * (1.2 - 0.6 * particle.t);
        out.alpha = std::min(1.0, particle.t * 8.0) * (1.0 - particle.t * 0.7);
        frame_.particles.push_back(out);
    }
    for (const Drifter& drifter : drifters_) {
        frame_.particles.push_back({drifter.x, drifter.y, drifter.size, 0.35 + 0.35 * std::sin(time_ * 1.3 + drifter.phase)});
    }
    for (const Streak& streak : streaks_) {
        frame_.streaks.push_back({streak.x, streak.y, streak.x + streak.length * 0.18, streak.y + streak.length, 0.35});
    }
    for (const Star& star : stars_) {
        frame_.stars.push_back({star.x, star.y, star.size, 0.55 + 0.45 * std::sin(time_ * star.rate + star.phase)});
    }
    if (preset_.aurora) {
        const char* tints[] = {"#4dffc3", "#7a8cff", "#c77dff"};
        for (int i = 0; i < 3; ++i) {
            frame_.bands.push_back({height_ * (0.22 + 0.1 * i), height_ * 0.06, width_ * (0.6 + 0.2 * i),
                                    time_ * (0.25 + 0.1 * i) + i, height_ * 0.09, 0.22, tints[i]});
        }
    }
    double flash = 0.0;
    for (const SimBolt& bolt : bolts_) {
        frame_.bolts.push_back(bolt);
        flash = std::max(flash, bolt.alpha * 0.25);
    }
    frame_.flash = flash;
}

}  // namespace eden
