#pragma once
// Player settings (graphics, display, frame rate), kept in settings.ini next to
// the executable. Read at startup; Save() writes the current values
// back, for in-game option menus.

#include <string>

namespace swrots {

struct Settings {
    // Display
    int width = 1280;         // window client size (ignored in fullscreen)
    int height = 720;
    bool fullscreen = false;  // borderless window covering the monitor
    bool vsync = false;
    bool stretch = false;     // fill the window, ignoring the aspect ratio (no black bars)
    // Rendering
    int resolutionScale = 0;  // internal resolution = Xbox resolution (640x480) x this, 1-8; 0 = auto
    bool widescreen = true;   // 16:9 (the game renders anamorphic widescreen); off: 4:3 pillarbox
    int anisotropy = 16;      // texture filtering, 1 (off) - 16
    bool bloom = true;        // the game's full-screen bloom (soft glow around bright areas)
    // Game
    int fpsLimit = 30;        // 30 (original) or 60 (experimental)
};

const Settings& GetSettings();
Settings& EditSettings();

// Loads `path` (creating it with defaults if missing); later Save() calls write there.
void LoadSettings(const std::wstring& path);
void SaveSettings();

} // namespace swrots
