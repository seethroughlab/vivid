#include "app/app_settings.h"
#include "platform/platform.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace vivid {

using nlohmann::json;

std::string app_settings_path() {
    const std::string dir = platform::user_data_dir();
    if (dir.empty()) return {};
    return (std::filesystem::path(dir) / "settings.json").string();
}

AppSettings load_app_settings(const std::string& path) {
    AppSettings s;
    if (path.empty()) return s;
    std::ifstream in(path);
    if (!in) return s;
    json j;
    try { in >> j; } catch (...) { return s; }
    if (!j.is_object()) return s;
    // Guard the type explicitly: json::value() throws on a present-but-wrong-typed value, and a
    // hand-edited settings.json (PRD §7) could hold anything. A bad value falls back to the default.
    if (j.contains("reduce_motion") && j["reduce_motion"].is_boolean())
        s.reduce_motion = j["reduce_motion"].get<bool>();
    return s;
}

bool save_app_settings(const AppSettings& s, const std::string& path) {
    if (path.empty()) return false;
    json j = { {"reduce_motion", s.reduce_motion} };
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << j.dump(2);
    return static_cast<bool>(out);
}

}  // namespace vivid
