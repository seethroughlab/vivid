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
    if (j.contains("audio_device_name") && j["audio_device_name"].is_string())
        s.audio_device_name = j["audio_device_name"].get<std::string>();
    if (j.contains("audio_sample_rate") && j["audio_sample_rate"].is_number_unsigned())
        s.audio_sample_rate = j["audio_sample_rate"].get<uint32_t>();
    if (j.contains("audio_period_frames") && j["audio_period_frames"].is_number_unsigned())
        s.audio_period_frames = j["audio_period_frames"].get<uint32_t>();
    if (j.contains("audio_fallback_to_default") && j["audio_fallback_to_default"].is_boolean())
        s.audio_fallback_to_default = j["audio_fallback_to_default"].get<bool>();
    if (j.contains("audio_input_enabled") && j["audio_input_enabled"].is_boolean())
        s.audio_input_enabled = j["audio_input_enabled"].get<bool>();
    if (j.contains("audio_input_name") && j["audio_input_name"].is_string())
        s.audio_input_name = j["audio_input_name"].get<std::string>();
    if (j.contains("midi_input_source") && j["midi_input_source"].is_number_integer())
        s.midi_input_source = j["midi_input_source"].get<int32_t>();
    if (j.contains("midi_input_channel") && j["midi_input_channel"].is_number_integer())
        s.midi_input_channel = j["midi_input_channel"].get<int>();
    return s;
}

bool save_app_settings(const AppSettings& s, const std::string& path) {
    if (path.empty()) return false;
    json j = { {"reduce_motion", s.reduce_motion},
               {"audio_device_name", s.audio_device_name},
               {"audio_sample_rate", s.audio_sample_rate},
               {"audio_period_frames", s.audio_period_frames},
               {"audio_fallback_to_default", s.audio_fallback_to_default},
               {"audio_input_enabled", s.audio_input_enabled},
               {"audio_input_name", s.audio_input_name},
               {"midi_input_source", s.midi_input_source},
               {"midi_input_channel", s.midi_input_channel} };
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << j.dump(2);
    return static_cast<bool>(out);
}

audio::DevicePrefs device_prefs_from(const AppSettings& s) {
    audio::DevicePrefs p;
    p.requested_name      = s.audio_device_name;       // "" => system default output
    p.sample_rate         = s.audio_sample_rate;       // 0  => device native rate
    p.period_frames       = s.audio_period_frames;
    p.fallback_to_default = s.audio_fallback_to_default;
    p.enable_input        = s.audio_input_enabled;     // ADR-0032 Phase D1
    p.input_name          = s.audio_input_name;        // "" => system default input
    return p;
}

}  // namespace vivid
