#include "app/ThemeManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "common/Strings.hpp"

namespace eden {

bool parseHexColor(const std::string& hex, Rgb& out) {
    if (hex.size() != 7 || hex[0] != '#') return false;
    unsigned value = 0;
    if (std::sscanf(hex.c_str() + 1, "%6x", &value) != 1) return false;
    out.r = static_cast<int>((value >> 16) & 0xFF);
    out.g = static_cast<int>((value >> 8) & 0xFF);
    out.b = static_cast<int>(value & 0xFF);
    return true;
}

std::string toHexColor(const Rgb& color) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", std::clamp(color.r, 0, 255), std::clamp(color.g, 0, 255),
                  std::clamp(color.b, 0, 255));
    return buffer;
}

std::string mixColors(const std::string& a, const std::string& b, double amount) {
    Rgb first;
    Rgb second;
    if (!parseHexColor(a, first) || !parseHexColor(b, second)) return a;
    amount = std::clamp(amount, 0.0, 1.0);
    Rgb mixed;
    mixed.r = static_cast<int>(std::lround(first.r + (second.r - first.r) * amount));
    mixed.g = static_cast<int>(std::lround(first.g + (second.g - first.g) * amount));
    mixed.b = static_cast<int>(std::lround(first.b + (second.b - first.b) * amount));
    return toHexColor(mixed);
}

std::string withAlpha(const std::string& hex, double alpha) {
    Rgb color;
    if (!parseHexColor(hex, color)) return hex;
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "rgba(%d, %d, %d, %.2f)", color.r, color.g, color.b, std::clamp(alpha, 0.0, 1.0));
    return buffer;
}

double relativeLuminance(const Rgb& color) {
    auto channel = [](int value) {
        const double c = value / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b);
}

double contrastRatio(const std::string& foreground, const std::string& background) {
    Rgb a;
    Rgb b;
    if (!parseHexColor(foreground, a) || !parseHexColor(background, b)) return 1.0;
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double lighter = std::max(la, lb);
    const double darker = std::min(la, lb);
    return (lighter + 0.05) / (darker + 0.05);
}

const std::vector<Palette>& builtinPalettes() {
    static const std::vector<Palette> palettes = {
        {"ruby-night", "Ruby Night", true, "#12060a", "#1f0a10", "#2b1019", "#6b3a2a", "#f7eedc", "#cdbd9f", "#e3b94a",
         "#e0486f", "#3fbf8a", "#5b8def", "#a77be0", "#f4efe6", "#e3b94a", "#ff6b6b", "#5a1f33"},
        {"emerald-garden", "Emerald Garden", true, "#04120c", "#0a1f16", "#112b1f", "#2f6b4d", "#eaf6ec", "#a9cdb6",
         "#e0bf55", "#e0627f", "#4fd69c", "#66a3ff", "#b58cf0", "#f1f5ef", "#4fd69c", "#ff7b7b", "#1c4d37"},
        {"sapphire-deep", "Sapphire Deep", true, "#060b1c", "#0d1530", "#142044", "#34508f", "#eef2ff", "#b4c0e6",
         "#ebc65a", "#ee6a8d", "#53d4a0", "#7aa7ff", "#b794f4", "#f3f4fb", "#7aa7ff", "#ff7b86", "#223a73"},
        {"amethyst-dusk", "Amethyst Dusk", true, "#0f0818", "#1a0f2a", "#261640", "#5d3f91", "#f5eefe", "#c9b8e6",
         "#ecc45e", "#f06a95", "#56d6a1", "#75a3ff", "#bf94f7", "#f6f1fb", "#bf94f7", "#ff7d8a", "#3d2570"},
        {"pearl-light", "Pearl Light", false, "#f7f3ea", "#ffffff", "#efe8d8", "#cdbf9f", "#1f1a12", "#5c5240", "#8a6a0a",
         "#a3173f", "#0f7a50", "#1d4fb3", "#6a38b0", "#fffdf7", "#8a6a0a", "#b3261e", "#f1dfa4"},
        {"high-contrast", "High Contrast", true, "#000000", "#000000", "#101010", "#ffffff", "#ffffff", "#e6e6e6",
         "#ffd54a", "#ff7a9c", "#5dffb1", "#8ab4ff", "#d2a8ff", "#ffffff", "#ffd54a", "#ff8a8a", "#333333"},
    };
    return palettes;
}

const std::vector<BackgroundPreset>& builtinBackgrounds() {
    static const std::vector<BackgroundPreset> presets = {
        {"crimson-clouds", "Crimson Clouds", "#2a0710", "#0b0206", "#ff8a6b", 40, 0.15, true, 9.0, false, 0, false, false, false},
        {"golden-river", "Golden River", "#1a0d10", "#07050a", "#f2c94c", 140, 0.6, false, 0.0, false, 0, false, false, true},
        {"jewel-rain", "Jewel Rain", "#0a1020", "#03050c", "#8fb7ff", 30, 0.2, false, 0.0, true, 120, false, false, false},
        {"aurora", "Aurora", "#06121a", "#020509", "#7dffcf", 60, 0.25, false, 0.0, false, 0, true, true, false},
        {"deep-space", "Deep Space", "#04040c", "#010104", "#dfe6ff", 220, 0.05, false, 0.0, false, 0, false, true, false},
    };
    return presets;
}

const Palette* findPalette(const std::string& name) {
    for (const Palette& palette : builtinPalettes()) {
        if (palette.name == name) return &palette;
    }
    return nullptr;
}

const BackgroundPreset* findBackground(const std::string& name) {
    for (const BackgroundPreset& preset : builtinBackgrounds()) {
        if (preset.name == name) return &preset;
    }
    return nullptr;
}

Status ThemeManager::load() {
    SettingsRepository store(db_);
    ThemeSettings loaded;
    loaded.palette = store.getOr("theme.palette", loaded.palette);
    loaded.background = store.getOr("theme.background", loaded.background);
    auto number = [&](const std::string& key, double fallback) {
        try {
            return std::stod(store.getOr(key, std::to_string(fallback)));
        } catch (const std::exception&) {
            return fallback;
        }
    };
    loaded.particleDensity = std::clamp(number("theme.particleDensity", loaded.particleDensity), 0.0, 1.0);
    loaded.fontScale = std::clamp(number("theme.fontScale", loaded.fontScale), 0.7, 2.5);
    loaded.reducedMotion = store.getBoolOr("theme.reducedMotion", loaded.reducedMotion);
    loaded.dyslexiaFont = store.getBoolOr("theme.dyslexiaFont", loaded.dyslexiaFont);
    loaded.highContrast = store.getBoolOr("theme.highContrast", loaded.highContrast);
    if (findPalette(loaded.palette) == nullptr) loaded.palette = ThemeSettings().palette;
    if (findBackground(loaded.background) == nullptr) loaded.background = ThemeSettings().background;
    settings_ = loaded;
    return Status::success();
}

Status ThemeManager::save() const {
    SettingsRepository store(db_);
    Status results[] = {
        store.set("theme.palette", settings_.palette),
        store.set("theme.background", settings_.background),
        store.set("theme.particleDensity", std::to_string(settings_.particleDensity)),
        store.set("theme.fontScale", std::to_string(settings_.fontScale)),
        store.setBool("theme.reducedMotion", settings_.reducedMotion),
        store.setBool("theme.dyslexiaFont", settings_.dyslexiaFont),
        store.setBool("theme.highContrast", settings_.highContrast),
    };
    for (const Status& status : results) {
        if (!status.ok()) return status;
    }
    return Status::success();
}

const Palette& ThemeManager::activePalette() const {
    const Palette* palette = findPalette(settings_.highContrast ? "high-contrast" : settings_.palette);
    return palette != nullptr ? *palette : builtinPalettes().front();
}

const BackgroundPreset& ThemeManager::activeBackground() const {
    const BackgroundPreset* preset = findBackground(settings_.background);
    return preset != nullptr ? *preset : builtinBackgrounds().front();
}

int ThemeManager::particleCount() const {
    if (settings_.reducedMotion) return 0;
    return static_cast<int>(std::lround(activeBackground().particleCount * settings_.particleDensity));
}

std::string ThemeManager::fontFamily() const {
    if (settings_.dyslexiaFont) return "'OpenDyslexic', 'Atkinson Hyperlegible', 'Comic Sans MS', sans-serif";
    return "'Cormorant Garamond', 'Georgia', 'Times New Roman', serif";
}

double ThemeManager::pointSize(double base) const { return base * settings_.fontScale; }

Status ThemeManager::setPalette(const std::string& name) {
    if (findPalette(name) == nullptr) return makeError(ErrorCode::NotFound, "unknown palette: " + name);
    settings_.palette = name;
    return save();
}

Status ThemeManager::setBackground(const std::string& name) {
    if (findBackground(name) == nullptr) return makeError(ErrorCode::NotFound, "unknown background: " + name);
    settings_.background = name;
    return save();
}

Status ThemeManager::setFontScale(double scale) {
    if (scale < 0.7 || scale > 2.5) return makeError(ErrorCode::InvalidArgument, "font scale must be between 0.7 and 2.5");
    settings_.fontScale = scale;
    return save();
}

std::string ThemeManager::styleSheet() const {
    const Palette& p = activePalette();
    const double base = pointSize(11.0);
    auto pt = [](double value) {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%.1fpt", value);
        return std::string(buffer);
    };
    const std::string family = fontFamily();
    const std::string glow = withAlpha(p.gold, 0.35);
    const std::string hover = mixColors(p.surface, p.gold, 0.14);
    const std::string pressed = mixColors(p.surface, p.gold, 0.26);
    const std::string headerTop = mixColors(p.surfaceAlt, p.gold, 0.12);
    std::ostringstream css;
    css << "QWidget { background-color: " << p.background << "; color: " << p.text << "; font-family: " << family
        << "; font-size: " << pt(base) << "; }\n";
    css << "QMainWindow, QDialog { background-color: " << p.background << "; }\n";
    css << "QLabel { background: transparent; }\n";
    css << "QLabel#muted { color: " << p.mutedText << "; }\n";
    css << "QLabel#title { color: " << p.gold << "; font-size: " << pt(base * 1.7) << "; font-weight: 600; }\n";
    css << "QPushButton { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 " << headerTop << ", stop:1 "
        << p.surface << "); border: 1px solid " << p.gold << "; border-radius: 8px; padding: 6px 16px; color: " << p.text
        << "; }\n";
    css << "QPushButton:hover { background-color: " << hover << "; border-color: " << mixColors(p.gold, p.pearl, 0.4) << "; }\n";
    css << "QPushButton:pressed { background-color: " << pressed << "; }\n";
    css << "QPushButton:disabled { color: " << p.mutedText << "; border-color: " << p.border << "; }\n";
    css << "QPushButton:focus { border: 2px solid " << p.accent << "; }\n";
    css << "QToolBar { background-color: " << p.surface << "; border-bottom: 1px solid " << p.border << "; spacing: 6px; padding: 4px; }\n";
    css << "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 5px 9px; }\n";
    css << "QToolButton:hover { border-color: " << p.gold << "; background-color: " << hover << "; }\n";
    css << "QLineEdit, QPlainTextEdit, QTextEdit, QTextBrowser, QSpinBox, QComboBox { background-color: " << p.surface
        << "; border: 1px solid " << p.border << "; border-radius: 6px; padding: 5px; selection-background-color: "
        << p.selection << "; selection-color: " << p.text << "; }\n";
    css << "QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus { border: 2px solid " << p.accent << "; }\n";
    css << "QListView, QTreeView, QTableView { background-color: " << p.background << "; border: 1px solid " << p.border
        << "; alternate-background-color: " << p.surface << "; outline: 0; }\n";
    css << "QListView::item:selected, QTreeView::item:selected { background-color: " << p.selection << "; border: 1px solid "
        << p.gold << "; }\n";
    css << "QWidget#libraryPage { background: transparent; }\n";
    css << "QListView#library { background: transparent; border: none; }\n";
    css << "QDockWidget { titlebar-close-icon: none; }\n";
    css << "QDockWidget::title { background-color: " << p.surfaceAlt << "; padding: 6px; border-bottom: 1px solid " << p.gold << "; }\n";
    css << "QTabWidget::pane { border: 1px solid " << p.border << "; }\n";
    css << "QTabBar::tab { background: " << p.surface << "; border: 1px solid " << p.border
        << "; padding: 6px 14px; border-top-left-radius: 6px; border-top-right-radius: 6px; }\n";
    css << "QTabBar::tab:selected { background: " << p.surfaceAlt << "; border-bottom-color: " << p.gold << "; color: " << p.gold << "; }\n";
    css << "QScrollBar:vertical { background: " << p.background << "; width: 12px; margin: 0; }\n";
    css << "QScrollBar::handle:vertical { background: " << mixColors(p.border, p.gold, 0.3)
        << "; border-radius: 5px; min-height: 28px; }\n";
    css << "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }\n";
    css << "QProgressBar { background: " << p.surface << "; border: 1px solid " << p.border << "; border-radius: 5px; text-align: center; height: 12px; }\n";
    css << "QProgressBar::chunk { background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 " << p.ruby << ", stop:0.5 "
        << p.gold << ", stop:1 " << p.emerald << "); border-radius: 4px; }\n";
    css << "QSlider::groove:horizontal { background: " << p.border << "; height: 4px; border-radius: 2px; }\n";
    css << "QSlider::handle:horizontal { background: " << p.gold << "; width: 14px; margin: -6px 0; border-radius: 7px; border: 1px solid " << glow << "; }\n";
    css << "QStatusBar { background-color: " << p.surface << "; border-top: 1px solid " << p.border << "; }\n";
    css << "QMenu { background-color: " << p.surface << "; border: 1px solid " << p.gold << "; }\n";
    css << "QMenu::item:selected { background-color: " << p.selection << "; }\n";
    css << "QToolTip { background-color: " << p.surfaceAlt << "; color: " << p.text << "; border: 1px solid " << p.gold << "; }\n";
    return css.str();
}

}  // namespace eden
