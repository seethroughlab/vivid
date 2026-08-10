#pragma once
#include <cstdint>
#include <string>

#include "audio/audio_device_manager.h"   // audio::DevicePrefs (no miniaudio — safe to include here)

// App-level (not per-project) settings, persisted to
// ~/Library/Application Support/Vivid/settings.json. These are viewer/accessibility preferences that
// should follow the person, not the document — unlike output identity (an Output-node param) or window
// geometry (window.json). Kept deliberately tiny; grow it only for genuinely app-global toggles.
namespace vivid {

struct AppSettings {
    // UX Ph4 F1 — reduce motion / flash limit. When on, the visual output is temporally low-passed so
    // rapid full-frame luminance swings are damped (photosensitivity accommodation). Off by default.
    bool reduce_motion = false;

    // ADR-0032 Phase A — the preferred audio OUTPUT device (machine-level, follows the person). Empty
    // name => follow the system default. Sample rate 0 => open at the device's native rate.
    std::string audio_device_name;
    uint32_t    audio_sample_rate = 0;
    uint32_t    audio_period_frames = 1024;
    bool        audio_fallback_to_default = true;

    // ADR-0032 Phase D1 — hardware audio INPUT (capture). Off by default => the device stays
    // playback-only exactly as today. When enabled, the device opens duplex; the empty input name
    // follows the system default input.
    bool        audio_input_enabled = false;
    std::string audio_input_name;
};

// Absolute path to the settings file (user_data_dir()/settings.json); empty if no data dir.
std::string app_settings_path();
// Read settings from `path`; a missing/invalid file yields defaults.
AppSettings load_app_settings(const std::string& path);
// Write settings to `path`; returns false on I/O error.
bool save_app_settings(const AppSettings& s, const std::string& path);

// Map persisted settings → the audio device manager's open/reopen prefs (both output and input). Kept
// as one helper so every open path (launch + both device-switch handlers) carries BOTH directions'
// prefs — switching the output must not silently drop an enabled input, and vice versa.
audio::DevicePrefs device_prefs_from(const AppSettings& s);

}  // namespace vivid
