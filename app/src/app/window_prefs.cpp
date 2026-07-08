#include "app/window_prefs.h"
#include "platform/platform.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace vivid {

using nlohmann::json;

static int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

LaunchRect compute_launch_rect(int waX, int waY, int waW, int waH,
                               const WindowPrefs& p, int maxW, int maxH, float frac) {
    // Guard against a degenerate work area (no monitor reported).
    if (waW <= 0 || waH <= 0) { waX = 0; waY = 0; waW = 1280; waH = 800; }

    LaunchRect r;
    if (p.has_size) {
        // Restore: clamp the saved size to something that fits this monitor.
        r.w = clampi(p.w, std::min(kLaunchMinW, waW), waW);
        r.h = clampi(p.h, std::min(kLaunchMinH, waH), waH);
    } else {
        // First launch: a fraction of the work area, capped.
        r.w = std::min(static_cast<int>(std::lround(frac * waW)), maxW);
        r.h = std::min(static_cast<int>(std::lround(frac * waH)), maxH);
        r.w = clampi(r.w, std::min(kLaunchMinW, waW), waW);
        r.h = clampi(r.h, std::min(kLaunchMinH, waH), waH);
    }

    // Position: use the saved position only if it keeps the window fully on this monitor;
    // otherwise center within the work area.
    const bool pos_ok = p.has_pos && p.has_size &&
                        p.x >= waX && p.y >= waY &&
                        p.x + r.w <= waX + waW && p.y + r.h <= waY + waH;
    if (pos_ok) { r.x = p.x; r.y = p.y; }
    else        { r.x = waX + (waW - r.w) / 2; r.y = waY + (waH - r.h) / 2; }
    return r;
}

std::string window_prefs_path() {
    const std::string dir = platform::user_data_dir();
    if (dir.empty()) return {};
    return (std::filesystem::path(dir) / "window.json").string();
}

std::string editor_window_prefs_path() {
    const std::string dir = platform::user_data_dir();
    if (dir.empty()) return {};
    return (std::filesystem::path(dir) / "editor_window.json").string();
}

WindowPrefs load_window_prefs(const std::string& path) {
    WindowPrefs p;
    if (path.empty()) return p;
    std::ifstream in(path);
    if (!in) return p;
    json j;
    try { in >> j; } catch (...) { return p; }
    if (!j.is_object()) return p;
    if (j.contains("w") && j.contains("h")) {
        p.w = j.value("w", 0);
        p.h = j.value("h", 0);
        p.has_size = p.w > 0 && p.h > 0;
    }
    if (j.contains("x") && j.contains("y")) {
        p.x = j.value("x", 0);
        p.y = j.value("y", 0);
        p.has_pos = true;
    }
    return p;
}

bool save_window_prefs(const WindowPrefs& p, const std::string& path) {
    if (path.empty()) return false;
    json j = { {"w", p.w}, {"h", p.h}, {"x", p.x}, {"y", p.y} };
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << j.dump(2);
    return static_cast<bool>(out);
}

}  // namespace vivid
