#pragma once

#include <string>
#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "repo/SettingsRepository.hpp"

namespace eden {

struct Palette {
    std::string name;
    std::string title;
    bool dark = true;
    std::string background;
    std::string surface;
    std::string surfaceAlt;
    std::string border;
    std::string text;
    std::string mutedText;
    std::string gold;
    std::string ruby;
    std::string emerald;
    std::string sapphire;
    std::string amethyst;
    std::string pearl;
    std::string accent;
    std::string danger;
    std::string selection;
};

struct BackgroundPreset {
    std::string name;
    std::string title;
    std::string gradientTop;
    std::string gradientBottom;
    std::string particleColor;
    int particleCount = 0;
    double particleSpeed = 0.0;
    bool lightning = false;
    double lightningIntervalSeconds = 0.0;
    bool rain = false;
    int rainStreaks = 0;
    bool aurora = false;
    bool stars = false;
    bool riverFlow = false;
};

struct ThemeSettings {
    std::string palette = "ruby-night";
    std::string background = "golden-river";
    double particleDensity = 0.6;
    bool reducedMotion = false;
    double fontScale = 1.0;
    bool dyslexiaFont = false;
    bool highContrast = false;
};

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

bool parseHexColor(const std::string& hex, Rgb& out);
std::string toHexColor(const Rgb& color);
std::string mixColors(const std::string& a, const std::string& b, double amount);
std::string withAlpha(const std::string& hex, double alpha);
double relativeLuminance(const Rgb& color);
double contrastRatio(const std::string& foreground, const std::string& background);

const std::vector<Palette>& builtinPalettes();
const std::vector<BackgroundPreset>& builtinBackgrounds();
const Palette* findPalette(const std::string& name);
const BackgroundPreset* findBackground(const std::string& name);

class ThemeManager {
public:
    explicit ThemeManager(Database& db) : db_(db) {}

    Status load();
    Status save() const;

    ThemeSettings& settings() { return settings_; }
    const ThemeSettings& settings() const { return settings_; }

    const Palette& activePalette() const;
    const BackgroundPreset& activeBackground() const;
    int particleCount() const;
    std::string fontFamily() const;
    double pointSize(double base) const;
    std::string styleSheet() const;

    Status setPalette(const std::string& name);
    Status setBackground(const std::string& name);
    Status setFontScale(double scale);

private:
    Database& db_;
    ThemeSettings settings_;
};

}  // namespace eden
