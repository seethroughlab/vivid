#include "ui/style/theme_loader.h"
#include "ui/style/ui_style.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "test_helpers.h"

namespace fs = std::filesystem;

// Color tolerance for hex round-trip (max error = 0.5/255 ≈ 0.002)
static constexpr float kColorTol = 0.004f;

static void check_near(float actual, float expected, const char* msg) {
    if (std::fabs(actual - expected) > kColorTol) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f, diff %f)\n",
                     msg, expected, actual, std::fabs(actual - expected));
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_color3(const std::array<float, 3>& actual,
                          const std::array<float, 3>& expected,
                          const char* name) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s[0]", name);
    check_near(actual[0], expected[0], buf);
    std::snprintf(buf, sizeof(buf), "%s[1]", name);
    check_near(actual[1], expected[1], buf);
    std::snprintf(buf, sizeof(buf), "%s[2]", name);
    check_near(actual[2], expected[2], buf);
}

static void check_color4(const std::array<float, 4>& actual,
                          const std::array<float, 4>& expected,
                          const char* name) {
    char buf[128];
    for (int i = 0; i < 4; i++) {
        std::snprintf(buf, sizeof(buf), "%s[%d]", name, i);
        check_near(actual[i], expected[i], buf);
    }
}

// Redirect HOME so theme discovery uses a temp directory
struct TempHome {
    std::string old_home;
    std::string tmp_dir;

    TempHome() {
        const char* h = std::getenv("HOME");
        if (h) old_home = h;
        tmp_dir = "/tmp/vivid_test_theme_loader";
        fs::remove_all(tmp_dir);
        fs::create_directories(tmp_dir);
        setenv("HOME", tmp_dir.c_str(), 1);
    }

    ~TempHome() {
        if (!old_home.empty())
            setenv("HOME", old_home.c_str(), 1);
        fs::remove_all(tmp_dir);
    }

    std::string themes_dir() const {
#if defined(__APPLE__)
        return tmp_dir + "/Library/Application Support/Vivid/themes";
#else
        return tmp_dir + "/.config/vivid/themes";
#endif
    }
};

int main() {
    // =================================================================
    // Test 1: Parse embedded Dark Steel JSON
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: Parse embedded Dark Steel ===\n");

        auto themes = vivid::ui::discover_themes();
        auto style = vivid::ui::load_theme("dark_steel", themes);
        check(style.has_value(), "dark_steel loaded");

        if (style) {
            check(style->name == "Dark Steel", "name = Dark Steel");
            check_float(style->corner_radius, 0.0f, "corner_radius = 0");
        }
    }

    // =================================================================
    // Test 2: Parse embedded Midnight JSON
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: Parse embedded Midnight ===\n");

        auto themes = vivid::ui::discover_themes();
        auto style = vivid::ui::load_theme("midnight", themes);
        check(style.has_value(), "midnight loaded");

        if (style) {
            check(style->name == "Midnight", "name = Midnight");
            check_float(style->corner_radius, 4.0f, "corner_radius = 4");
        }
    }

    // =================================================================
    // Test 3: Parse embedded Slate JSON
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: Parse embedded Slate ===\n");

        auto themes = vivid::ui::discover_themes();
        auto style = vivid::ui::load_theme("slate", themes);
        check(style.has_value(), "slate loaded");

        if (style) {
            check(style->name == "Slate", "name = Slate");
            check_float(style->corner_radius, 6.0f, "corner_radius = 6");
        }
    }

    // =================================================================
    // Test 4: Round-trip — embedded JSON vs builtin_styles() values
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: Round-trip vs builtin_styles() ===\n");

        auto builtins = vivid::ui::builtin_styles();
        auto themes = vivid::ui::discover_themes();
        auto loaded = vivid::ui::load_all_themes(themes);

        check(loaded.size() >= 3, "at least 3 themes loaded");

        for (const auto& expected : builtins) {
            // Find matching loaded theme
            const vivid::ui::UIStyle* actual = nullptr;
            for (const auto& s : loaded) {
                if (s.id == expected.id) { actual = &s; break; }
            }

            char label[256];
            std::snprintf(label, sizeof(label), "found loaded theme '%s'",
                          expected.id.c_str());
            check(actual != nullptr, label);
            if (!actual) continue;

            std::snprintf(label, sizeof(label), "%s: name", expected.id.c_str());
            check(actual->name == expected.name, label);

            std::snprintf(label, sizeof(label), "%s: corner_radius", expected.id.c_str());
            check_float(actual->corner_radius, expected.corner_radius, label);

            // Check all color fields
            #define CHECK_C3(field) { \
                std::snprintf(label, sizeof(label), "%s: " #field, expected.id.c_str()); \
                check_color3(actual->field, expected.field, label); \
            }
            #define CHECK_C4(field) { \
                std::snprintf(label, sizeof(label), "%s: " #field, expected.id.c_str()); \
                check_color4(actual->field, expected.field, label); \
            }

            CHECK_C3(node_bg);
            CHECK_C3(node_sel_bg);
            CHECK_C3(accent);
            CHECK_C3(slider_fill);
            CHECK_C3(inspector_bg);
            CHECK_C3(dim_text);
            CHECK_C3(bright_text);
            CHECK_C4(popup_bg);
            CHECK_C3(input_field_bg);
            CHECK_C3(separator);
            CHECK_C3(scrollbar_track);
            CHECK_C3(scrollbar_thumb);
            CHECK_C3(button_bg);
            CHECK_C3(button_hover);
            CHECK_C4(scrim);
            CHECK_C4(wire_color);
            CHECK_C4(wire_sel_color);
            CHECK_C3(slider_track);
            CHECK_C3(dark_bg);
            CHECK_C3(group_header_bg);

            #undef CHECK_C3
            #undef CHECK_C4
        }
    }

    // =================================================================
    // Test 5: Malformed JSON — returns nullopt
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: Malformed JSON ===\n");

        const char* bad = "{ not valid json !!!";
        auto result = vivid::ui::parse_theme_json(bad, std::strlen(bad));
        check(!result.has_value(), "malformed JSON returns nullopt");
    }

    // =================================================================
    // Test 6: Missing fields — defaults used
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: Missing fields use defaults ===\n");

        const char* minimal = R"({"name": "Minimal"})";
        auto result = vivid::ui::parse_theme_json(minimal, std::strlen(minimal));
        check(result.has_value(), "minimal JSON parses");

        if (result) {
            check(result->name == "Minimal", "name = Minimal");
            // Missing fields should get Dark Steel defaults
            check_float(result->corner_radius, 0.0f, "default corner_radius");
            check_near(result->node_bg[0], 0.12f, "default node_bg[0]");
            check_near(result->accent[2], 0.85f, "default accent[2]");
        }
    }

    // =================================================================
    // Test 7: Hex color parsing — #RRGGBB
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: Hex #RRGGBB parsing ===\n");

        const char* json = R"({"name": "HexTest", "node_bg": "#ff8000"})";
        auto result = vivid::ui::parse_theme_json(json, std::strlen(json));
        check(result.has_value(), "hex theme parsed");

        if (result) {
            check_near(result->node_bg[0], 1.0f, "R = ff → 1.0");
            check_near(result->node_bg[1], 128.0f / 255.0f, "G = 80 → 0.502");
            check_near(result->node_bg[2], 0.0f, "B = 00 → 0.0");
        }
    }

    // =================================================================
    // Test 8: Hex color parsing — #RRGGBBAA
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 8: Hex #RRGGBBAA parsing ===\n");

        const char* json = R"({"name": "HexAlpha", "popup_bg": "#ff000080"})";
        auto result = vivid::ui::parse_theme_json(json, std::strlen(json));
        check(result.has_value(), "hex+alpha theme parsed");

        if (result) {
            check_near(result->popup_bg[0], 1.0f, "R = ff → 1.0");
            check_near(result->popup_bg[1], 0.0f, "G = 00 → 0.0");
            check_near(result->popup_bg[2], 0.0f, "B = 00 → 0.0");
            check_near(result->popup_bg[3], 128.0f / 255.0f, "A = 80 → 0.502");
        }
    }

    // =================================================================
    // Test 9: rgba() color parsing
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 9: rgba() parsing ===\n");

        const char* json = R"j({"name": "RGBA", "scrim": "rgba(255, 128, 0, 0.75)"})j";
        auto result = vivid::ui::parse_theme_json(json, std::strlen(json));
        check(result.has_value(), "rgba theme parsed");

        if (result) {
            check_near(result->scrim[0], 1.0f, "R = 255 → 1.0");
            check_near(result->scrim[1], 128.0f / 255.0f, "G = 128 → 0.502");
            check_near(result->scrim[2], 0.0f, "B = 0 → 0.0");
            check_near(result->scrim[3], 0.75f, "A = 0.75");
        }
    }

    // =================================================================
    // Test 10: File-based theme loading
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 10: File-based theme ===\n");
        TempHome home;

        // Write a custom theme file
        std::string dir = home.themes_dir();
        fs::create_directories(dir);

        std::string path = dir + "/custom_neon.json";
        {
            std::ofstream ofs(path);
            ofs << R"({
                "name": "Neon",
                "corner_radius": 8,
                "accent": "#00ff00"
            })";
        }

        auto themes = vivid::ui::discover_themes();

        // Should find custom_neon + 3 embedded fallbacks
        check(themes.size() >= 4, "at least 4 themes discovered");

        bool found_neon = false;
        for (const auto& t : themes) {
            if (t.id == "custom_neon") {
                found_neon = true;
                check(t.name == "Neon", "custom theme name = Neon");
                check(!t.is_builtin, "custom theme is not builtin");
                check(!t.path.empty(), "custom theme has file path");
            }
        }
        check(found_neon, "custom_neon theme discovered");

        auto style = vivid::ui::load_theme("custom_neon", themes);
        check(style.has_value(), "custom theme loaded");
        if (style) {
            check(style->name == "Neon", "loaded name = Neon");
            check_float(style->corner_radius, 8.0f, "loaded corner_radius = 8");
            check_near(style->accent[1], 1.0f, "accent G = 1.0 (green)");
        }
    }

    // =================================================================
    // Test 11: ensure_default_themes writes files
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 11: ensure_default_themes ===\n");
        TempHome home;

        vivid::ui::ensure_default_themes();

        std::string dir = home.themes_dir();
        check(fs::exists(dir + "/dark_steel.json"), "dark_steel.json written");
        check(fs::exists(dir + "/midnight.json"), "midnight.json written");
        check(fs::exists(dir + "/slate.json"), "slate.json written");
        check(fs::exists(dir + "/emerald.json"), "emerald.json written");
        check(fs::exists(dir + "/crimson.json"), "crimson.json written");
        check(fs::exists(dir + "/vapor.json"), "vapor.json written");
        check(fs::exists(dir + "/carbon.json"), "carbon.json written");
        check(fs::exists(dir + "/monokai.json"), "monokai.json written");
    }

    // =================================================================
    // Test 12: ensure_default_themes is idempotent (doesn't overwrite)
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 12: ensure_default_themes idempotent ===\n");
        TempHome home;
        std::string dir = home.themes_dir();
        fs::create_directories(dir);

        // Write a custom file
        {
            std::ofstream ofs(dir + "/my_theme.json");
            ofs << R"({"name": "My Theme"})";
        }

        vivid::ui::ensure_default_themes();

        // Should NOT write defaults because directory already has .json files
        check(!fs::exists(dir + "/dark_steel.json"),
              "dark_steel.json not written (dir not empty)");
    }

    // =================================================================
    // Test 13: Nonexistent theme returns nullopt
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 13: Nonexistent theme ===\n");

        auto themes = vivid::ui::discover_themes();
        auto result = vivid::ui::load_theme("does_not_exist", themes);
        check(!result.has_value(), "nonexistent theme returns nullopt");
    }

    // =================================================================
    // Test 14: File overrides embedded for builtin id
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 14: File overrides embedded ===\n");
        TempHome home;
        std::string dir = home.themes_dir();
        fs::create_directories(dir);

        // Write a dark_steel.json with a modified accent color
        {
            std::ofstream ofs(dir + "/dark_steel.json");
            ofs << R"({
                "name": "Dark Steel Custom",
                "corner_radius": 2,
                "accent": "#ff0000"
            })";
        }

        auto themes = vivid::ui::discover_themes();
        auto style = vivid::ui::load_theme("dark_steel", themes);
        check(style.has_value(), "dark_steel loaded from file");
        if (style) {
            check(style->name == "Dark Steel Custom", "name from file");
            check_float(style->corner_radius, 2.0f, "corner_radius from file");
            check_near(style->accent[0], 1.0f, "accent R from file = 1.0");
            check_near(style->accent[1], 0.0f, "accent G from file = 0.0");
        }
    }

    // Test 15: vivid_version parsed into UIStyle
    {
        std::fprintf(stderr, "\n=== Test 15: vivid_version parsed ===\n");
        const char* json = R"({"vivid_version": "0.1.0", "name": "Test"})";
        auto style = vivid::ui::parse_theme_json(json, std::strlen(json));
        check(style.has_value(), "theme with vivid_version parses ok");
        if (style)
            check(style->vivid_version == "0.1.0", "vivid_version stored in UIStyle");
    }

    // Test 16: absent vivid_version → empty field, no crash
    {
        std::fprintf(stderr, "\n=== Test 16: absent vivid_version → empty ===\n");
        const char* json = R"({"name": "No Version"})";
        auto style = vivid::ui::parse_theme_json(json, std::strlen(json));
        check(style.has_value(), "theme without vivid_version parses ok");
        if (style)
            check(style->vivid_version.empty(), "vivid_version empty when absent");
    }

    // Test 17: major version mismatch → warning only, no rejection
    {
        std::fprintf(stderr, "\n=== Test 17: major version mismatch warning ===\n");
        const char* json = R"({"vivid_version": "99.0.0", "name": "Future Theme"})";
        auto style = vivid::ui::parse_theme_json(json, std::strlen(json));
        check(style.has_value(), "theme with mismatched major still loads");
        if (style) {
            check(style->vivid_version == "99.0.0", "mismatched vivid_version stored");
            check(style->name == "Future Theme",    "name still parsed after version warning");
        }
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
