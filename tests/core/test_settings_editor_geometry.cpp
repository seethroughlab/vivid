// Round-trip test for Settings::editor_window_geometry_by_type (Phase 3).
// Points HOME (and XDG_CONFIG_HOME on Linux) at a ScopedTempDir so the
// existing public load_settings/save_settings API can be exercised against a
// sandboxed config directory without touching the user's real settings.

#include "runtime/core/settings.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "test_helpers.h"

int main() {
    std::fprintf(stderr, "=== Test: Settings editor_window_geometry round-trip ===\n\n");

    ScopedTempDir sandbox("settings_editor_geom");
    ScopedEnvVar scoped_home("HOME", sandbox.str());
    ScopedEnvVar scoped_xdg("XDG_CONFIG_HOME", sandbox.str());

    // --- default-constructed Settings has an empty geometry map ---
    {
        vivid::Settings s;
        check(s.editor_window_geometry_by_type.empty(),
              "default Settings: geometry map is empty");
    }

    // --- save → load round-trips all fields verbatim ---
    {
        vivid::Settings s;
        s.editor_window_geometry_by_type["drum_sequencer"] =
            vivid::EditorWindowGeometry{ 120, 240, 900, 520 };
        s.editor_window_geometry_by_type["mseg"] =
            vivid::EditorWindowGeometry{ -1, -1, 1200, 700 };
        s.editor_window_geometry_by_type["tracker"] =
            vivid::EditorWindowGeometry{ 50, 50, 0, 0 };

        vivid::save_settings(s);

        vivid::Settings loaded = vivid::load_settings();
        check(loaded.editor_window_geometry_by_type.size() == 3,
              "loaded geometry map size = 3");

        auto ds = loaded.editor_window_geometry_by_type.find("drum_sequencer");
        check(ds != loaded.editor_window_geometry_by_type.end(),
              "drum_sequencer entry preserved");
        if (ds != loaded.editor_window_geometry_by_type.end()) {
            check(ds->second.x == 120,    "drum_sequencer.x = 120");
            check(ds->second.y == 240,    "drum_sequencer.y = 240");
            check(ds->second.width == 900,  "drum_sequencer.width = 900");
            check(ds->second.height == 520, "drum_sequencer.height = 520");
        }

        auto mseg = loaded.editor_window_geometry_by_type.find("mseg");
        check(mseg != loaded.editor_window_geometry_by_type.end(),
              "mseg entry preserved");
        if (mseg != loaded.editor_window_geometry_by_type.end()) {
            check(mseg->second.x == -1 && mseg->second.y == -1,
                  "mseg sentinel position x=-1, y=-1 preserved (OS-placed)");
            check(mseg->second.width == 1200 && mseg->second.height == 700,
                  "mseg width/height preserved");
        }

        auto tr = loaded.editor_window_geometry_by_type.find("tracker");
        check(tr != loaded.editor_window_geometry_by_type.end(),
              "tracker entry preserved");
        if (tr != loaded.editor_window_geometry_by_type.end()) {
            check(tr->second.width == 0 && tr->second.height == 0,
                  "tracker width=0/height=0 preserved (metadata defaults)");
        }
    }

    // --- omitting the editor_window_geometry key on load does not error ---
    {
        // Save a Settings with no geometry, then load.
        vivid::Settings s;
        s.workspace_root = "/tmp/vivid-test-ws";
        vivid::save_settings(s);
        vivid::Settings loaded = vivid::load_settings();
        check(loaded.editor_window_geometry_by_type.empty(),
              "load_settings with missing editor_window_geometry succeeds with empty map");
        check(loaded.workspace_root == "/tmp/vivid-test-ws",
              "unrelated fields still load correctly");
    }

    // --- updates overwrite, not merge ---
    {
        vivid::Settings s;
        s.editor_window_geometry_by_type["a"] =
            vivid::EditorWindowGeometry{ 1, 2, 100, 100 };
        vivid::save_settings(s);

        vivid::Settings s2;
        s2.editor_window_geometry_by_type["b"] =
            vivid::EditorWindowGeometry{ 3, 4, 200, 200 };
        vivid::save_settings(s2);

        vivid::Settings loaded = vivid::load_settings();
        check(loaded.editor_window_geometry_by_type.size() == 1,
              "save replaces the map (size after second save = 1)");
        auto b = loaded.editor_window_geometry_by_type.find("b");
        check(b != loaded.editor_window_geometry_by_type.end(),
              "second-save entry 'b' present");
        check(loaded.editor_window_geometry_by_type.find("a") ==
              loaded.editor_window_geometry_by_type.end(),
              "first-save entry 'a' replaced");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
