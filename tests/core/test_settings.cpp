#include "runtime/core/settings.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>
#include "test_helpers.h"

namespace fs = std::filesystem;

// Redirect settings to a temp directory by overriding HOME.
// get_config_dir() uses HOME on macOS → ~/Library/Application Support/Vivid/
struct TempHome {
    std::string old_home;
    std::string tmp_dir;

    TempHome() {
        const char* h = std::getenv("HOME");
        if (h) old_home = h;
        tmp_dir = "/tmp/vivid_test_settings_home";
        fs::remove_all(tmp_dir);
        fs::create_directories(tmp_dir);
        setenv("HOME", tmp_dir.c_str(), 1);
    }

    ~TempHome() {
        if (!old_home.empty())
            setenv("HOME", old_home.c_str(), 1);
        fs::remove_all(tmp_dir);
    }

    std::string config_dir() const {
#if defined(__APPLE__)
        return tmp_dir + "/Library/Application Support/Vivid";
#else
        return tmp_dir + "/.config/vivid";
#endif
    }

    std::string settings_path() const {
        return config_dir() + "/settings.json";
    }
};

int main() {
    // =================================================================
    // Test 1: Round-trip — save then load, verify all fields preserved
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: Round-trip ===\n");
        TempHome home;

        vivid::Settings s;
        s.window_x = 100;
        s.window_y = 200;
        s.window_width = 1920;
        s.window_height = 1080;
        s.bezier_wires = true;
        s.editor = "VSCode";
        s.editor_command = "code {file}";
        s.style_id = "midnight";
        s.operator_clone_destination_mode = "core_explicit";
        s.project_operator_root = "/tmp/vivid_project_ops";
        s.project_package_name = "vivid-project";

        vivid::save_settings(s);
        check(fs::exists(home.settings_path()), "settings file created");

        vivid::Settings loaded = vivid::load_settings();
        check(loaded.window_x == 100, "window_x preserved");
        check(loaded.window_y == 200, "window_y preserved");
        check(loaded.window_width == 1920, "window_width preserved");
        check(loaded.window_height == 1080, "window_height preserved");
        check(loaded.bezier_wires == true, "bezier_wires preserved");
        check(loaded.editor == "VSCode", "editor preserved");
        check(loaded.editor_command == "code {file}", "editor_command preserved");
        check(loaded.style_id == "midnight", "style_id preserved");
        check(loaded.operator_clone_destination_mode == "core_explicit",
              "operator_clone_destination_mode preserved");
        check(loaded.project_operator_root == "/tmp/vivid_project_ops",
              "project_operator_root preserved");
        check(loaded.project_package_name == "vivid-project",
              "project_package_name preserved");
    }

    // =================================================================
    // Test 2: Missing file — load returns defaults
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: Missing file ===\n");
        TempHome home;
        // Don't save anything — file doesn't exist

        vivid::Settings loaded = vivid::load_settings();
        check(loaded.window_x == -1, "default window_x = -1");
        check(loaded.window_y == -1, "default window_y = -1");
        check(loaded.window_width == 1280, "default window_width = 1280");
        check(loaded.window_height == 800, "default window_height = 800");
        check(loaded.bezier_wires == false, "default bezier_wires = false");
        check(loaded.editor.empty(), "default editor is empty");
        check(loaded.editor_command.empty(), "default editor_command is empty");
        check(loaded.style_id.empty(), "default style_id is empty");
        check(loaded.operator_clone_destination_mode == "project_default",
              "default operator_clone_destination_mode is project_default");
        check(loaded.project_operator_root.empty(), "default project_operator_root is empty");
        check(loaded.project_package_name.empty(), "default project_package_name is empty");
    }

    // =================================================================
    // Test 3: Malformed JSON — load returns defaults without crash
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: Malformed JSON ===\n");
        TempHome home;
        fs::create_directories(home.config_dir());

        {
            std::ofstream ofs(home.settings_path());
            ofs << "{ this is not valid json !!!";
        }

        vivid::Settings loaded = vivid::load_settings();
        check(loaded.window_width == 1280, "malformed JSON returns default width");
        check(loaded.window_height == 800, "malformed JSON returns default height");
    }

    // =================================================================
    // Test 4: Window size clamping
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: Window size clamping ===\n");
        TempHome home;
        fs::create_directories(home.config_dir());

        // Write settings with tiny window sizes
        {
            std::ofstream ofs(home.settings_path());
            ofs << R"({"window_width": 50, "window_height": 30})";
        }

        vivid::Settings loaded = vivid::load_settings();
        check(loaded.window_width == 320, "width < 320 clamped to 320");
        check(loaded.window_height == 240, "height < 240 clamped to 240");
    }

    // =================================================================
    // Test 5: Zero-size window clamping
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: Zero-size clamping ===\n");
        TempHome home;
        fs::create_directories(home.config_dir());

        {
            std::ofstream ofs(home.settings_path());
            ofs << R"({"window_width": 0, "window_height": 0})";
        }

        vivid::Settings loaded = vivid::load_settings();
        check(loaded.window_width == 320, "width 0 clamped to 320");
        check(loaded.window_height == 240, "height 0 clamped to 240");
    }

    // =================================================================
    // Test 6: Empty optional fields omitted from JSON
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: Empty optional fields ===\n");
        TempHome home;

        vivid::Settings s;
        // Leave editor, editor_command, style_id empty
        vivid::save_settings(s);

        std::string content;
        {
            std::ifstream ifs(home.settings_path());
            content = {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
        }

        // Empty strings should not be written to JSON
        check(content.find("\"editor\"") == std::string::npos,
              "empty editor not written to JSON");
        check(content.find("\"editor_command\"") == std::string::npos,
              "empty editor_command not written to JSON");
        check(content.find("\"style_id\"") == std::string::npos,
              "empty style_id not written to JSON");
        check(content.find("\"project_operator_root\"") == std::string::npos,
              "empty project_operator_root not written to JSON");
        check(content.find("\"project_package_name\"") == std::string::npos,
              "empty project_package_name not written to JSON");

        // But it should still load cleanly
        vivid::Settings loaded = vivid::load_settings();
        check(loaded.editor.empty(), "editor loads as empty");
        check(loaded.editor_command.empty(), "editor_command loads as empty");
        check(loaded.style_id.empty(), "style_id loads as empty");
        check(loaded.operator_clone_destination_mode == "project_default",
              "operator_clone_destination_mode defaults correctly");
        check(loaded.project_operator_root.empty(), "project_operator_root loads as empty");
        check(loaded.project_package_name.empty(), "project_package_name loads as empty");
    }

    // =================================================================
    // Test 7: Negative window size clamping
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: Negative window size ===\n");
        TempHome home;
        fs::create_directories(home.config_dir());

        {
            std::ofstream ofs(home.settings_path());
            ofs << R"({"window_width": -100, "window_height": -50})";
        }

        vivid::Settings loaded = vivid::load_settings();
        check(loaded.window_width == 320, "negative width clamped to 320");
        check(loaded.window_height == 240, "negative height clamped to 240");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
