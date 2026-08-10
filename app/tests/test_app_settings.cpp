// Headless test for the app-level settings store (app/src/app/app_settings.*): a missing/invalid file
// yields defaults, and a save→load round-trip preserves the reduce-motion toggle. (UX Ph4 F1)
#include "app/app_settings.h"
#include "test_helpers.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace vivid;

int main() {
    const std::string path = (std::filesystem::temp_directory_path() / "vivid_app_settings_test.json").string();
    std::error_code ec; std::filesystem::remove(path, ec);

    // Missing file → defaults (reduce_motion off).
    CHECK(load_app_settings(path).reduce_motion == false);
    // Empty path → defaults, no crash.
    CHECK(load_app_settings("").reduce_motion == false);

    // Round-trip true.
    CHECK(save_app_settings({ /*reduce_motion*/ true }, path));
    CHECK(load_app_settings(path).reduce_motion == true);

    // Round-trip false (overwrites).
    CHECK(save_app_settings({ /*reduce_motion*/ false }, path));
    CHECK(load_app_settings(path).reduce_motion == false);

    // Malformed JSON → defaults, no throw.
    { std::ofstream(path, std::ios::trunc) << "{ not valid json"; }
    CHECK(load_app_settings(path).reduce_motion == false);

    // Wrong-typed value → falls back to the default rather than throwing.
    { std::ofstream(path, std::ios::trunc) << "{ \"reduce_motion\": \"yes\" }"; }
    CHECK(load_app_settings(path).reduce_motion == false);

    // ADR-0032 Phase A — the audio-device fields. Defaults: empty name, native rate (0), 1024, fallback on.
    { AppSettings d; CHECK(d.audio_device_name.empty()); CHECK(d.audio_sample_rate == 0u);
      CHECK(d.audio_period_frames == 1024u); CHECK(d.audio_fallback_to_default == true); }

    // ADR-0032 Phase D1 — input (capture) fields default off / empty.
    { AppSettings d; CHECK(d.audio_input_enabled == false); CHECK(d.audio_input_name.empty()); }

    // Round-trip the device fields alongside reduce_motion (they share the file — a partial save must
    // NOT clobber the other's values; this is the persistence bug-guard).
    AppSettings s;
    s.reduce_motion = true;
    s.audio_device_name = "USB Interface";
    s.audio_sample_rate = 44100;
    s.audio_period_frames = 512;
    s.audio_fallback_to_default = false;
    s.audio_input_enabled = true;
    s.audio_input_name = "Built-in Microphone";
    CHECK(save_app_settings(s, path));
    AppSettings r = load_app_settings(path);
    CHECK(r.reduce_motion == true);
    CHECK(r.audio_device_name == "USB Interface");
    CHECK(r.audio_sample_rate == 44100u);
    CHECK(r.audio_period_frames == 512u);
    CHECK(r.audio_fallback_to_default == false);
    CHECK(r.audio_input_enabled == true);
    CHECK(r.audio_input_name == "Built-in Microphone");

    // ADR-0032 Phase D1 — device_prefs_from carries BOTH directions' prefs (so no open path drops one).
    audio::DevicePrefs dp = device_prefs_from(r);
    CHECK(dp.requested_name == "USB Interface");
    CHECK(dp.sample_rate == 44100u);
    CHECK(dp.period_frames == 512u);
    CHECK(dp.fallback_to_default == false);
    CHECK(dp.enable_input == true);
    CHECK(dp.input_name == "Built-in Microphone");

    // Wrong-typed device fields → fall back to defaults, no throw (hand-edited settings.json).
    { std::ofstream(path, std::ios::trunc)
          << "{ \"audio_device_name\": 5, \"audio_sample_rate\": \"fast\", \"audio_period_frames\": -3 }"; }
    AppSettings bad = load_app_settings(path);
    CHECK(bad.audio_device_name.empty());
    CHECK(bad.audio_sample_rate == 0u);
    CHECK(bad.audio_period_frames == 1024u);

    std::filesystem::remove(path, ec);
    return vivid::test::summary("test_app_settings");
}
