#pragma once
#include <string>

// App-level window sizing: on first launch the window opens at a fraction of the current
// monitor's work area (capped), centered; after that the user's manually-resized size +
// position are remembered in ~/Library/Application Support/Vivid/window.json and restored
// (clamped to fit the current monitor). Window size is app-level — not per project.
//
// compute_launch_rect() is pure (no GLFW/filesystem) so the sizing policy is unit-tested;
// main.cpp supplies the monitor work area from GLFW and applies the result.
namespace vivid {

// Launch policy constants (see plan): "almost full" on a laptop, bounded on a superwide.
constexpr float kLaunchFraction = 0.90f;   // fraction of the monitor work area on first launch
constexpr int   kLaunchMaxW     = 2200;    // hard cap so a superwide isn't filled
constexpr int   kLaunchMaxH     = 1350;
constexpr int   kLaunchMinW     = 640;     // never smaller than this
constexpr int   kLaunchMinH     = 480;

struct WindowPrefs {
    int  w = 0, h = 0, x = 0, y = 0;
    bool has_size = false;   // a remembered size exists (restore path vs first-launch %)
    bool has_pos  = false;
};

struct LaunchRect { int x = 0, y = 0, w = 0, h = 0; };

// The final window rect for a work area [wa*] (all in screen coordinates):
//  - prefs.has_size: restore the saved size clamped to [kLaunchMin .. work area], and the
//    saved position clamped fully on-screen (recentred if it has none / doesn't fit);
//  - otherwise: frac * work area, capped at (maxW,maxH), centered in the work area.
LaunchRect compute_launch_rect(int waX, int waY, int waW, int waH,
                               const WindowPrefs& prefs, int maxW, int maxH, float frac);

// Absolute path to the prefs file (user_data_dir()/window.json); empty if no data dir.
std::string window_prefs_path();
// Read prefs from `path`; a missing/invalid file yields has_size = has_pos = false.
WindowPrefs load_window_prefs(const std::string& path);
// Write prefs to `path`; returns false on I/O error.
bool save_window_prefs(const WindowPrefs& prefs, const std::string& path);

}  // namespace vivid
