#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "app/ThemeManager.hpp"

namespace eden {

struct SimPoint {
    double x = 0.0;
    double y = 0.0;
};

struct SimParticle {
    double x = 0.0;
    double y = 0.0;
    double size = 1.0;
    double alpha = 1.0;
};

struct SimBolt {
    std::vector<SimPoint> points;
    double alpha = 0.0;
};

struct SimStreak {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    double alpha = 0.0;
};

struct SimStar {
    double x = 0.0;
    double y = 0.0;
    double size = 1.0;
    double alpha = 1.0;
};

struct SimBand {
    double baseY = 0.0;
    double amplitude = 0.0;
    double wavelength = 1.0;
    double phase = 0.0;
    double thickness = 0.0;
    double alpha = 0.0;
    std::string color;
};

struct SimFrame {
    std::vector<SimParticle> particles;
    std::vector<SimBolt> bolts;
    std::vector<SimStreak> streaks;
    std::vector<SimStar> stars;
    std::vector<SimBand> bands;
    double flash = 0.0;
    SimPoint logo;
};

class BackgroundSimulation {
public:
    BackgroundSimulation(BackgroundPreset preset, int particleCount, std::uint32_t seed = 1);

    void resize(double width, double height);
    void setReducedMotion(bool reduced);
    void step(double dt);

    const SimFrame& frame() const { return frame_; }
    const BackgroundPreset& preset() const { return preset_; }
    double time() const { return time_; }
    SimPoint riverPoint(double t) const;

private:
    struct RiverParticle {
        double t = 0.0;
        double speed = 0.0;
        double lateral = 0.0;
        double size = 1.0;
    };
    struct Drifter {
        double x = 0.0;
        double y = 0.0;
        double vx = 0.0;
        double vy = 0.0;
        double size = 1.0;
        double phase = 0.0;
    };
    struct Streak {
        double x = 0.0;
        double y = 0.0;
        double speed = 0.0;
        double length = 0.0;
    };
    struct Star {
        double x = 0.0;
        double y = 0.0;
        double size = 1.0;
        double phase = 0.0;
        double rate = 1.0;
    };

    double uniform(double low, double high);
    void populate();
    void spawnBolt();
    void rebuildFrame();

    BackgroundPreset preset_;
    int particleCount_;
    std::mt19937 rng_;
    double width_ = 800.0;
    double height_ = 600.0;
    double time_ = 0.0;
    double nextBolt_ = 0.0;
    bool reduced_ = false;
    std::vector<RiverParticle> river_;
    std::vector<Drifter> drifters_;
    std::vector<Streak> streaks_;
    std::vector<Star> stars_;
    std::vector<SimBolt> bolts_;
    SimFrame frame_;
};

}  // namespace eden
