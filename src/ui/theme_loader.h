#pragma once

#include "ui/ui_style.h"
#include <string>
#include <vector>
#include <optional>

namespace vivid::ui {

struct ThemeInfo {
    std::string name;        // display name from JSON "name" field
    std::string id;          // filename stem (e.g. "dark_steel")
    std::string path;        // full path (empty for embedded fallbacks)
    bool is_builtin;
};

// Get the themes directory path ({config_dir}/themes/)
std::string get_themes_dir();

// Discover available themes (embedded fallbacks + user themes from themes/ dir)
std::vector<ThemeInfo> discover_themes();

// Load a single theme by id from a list of discovered themes
std::optional<UIStyle> load_theme(const std::string& theme_id,
                                  const std::vector<ThemeInfo>& themes);

// Load all discovered themes into UIStyle objects
std::vector<UIStyle> load_all_themes(const std::vector<ThemeInfo>& themes);

// Parse a UIStyle from a JSON string (id is NOT set — caller must set it)
std::optional<UIStyle> parse_theme_json(const char* json, size_t len);

// Write default theme files to themes/ directory if it's empty
void ensure_default_themes();

// Open the themes directory in the OS file manager
void open_themes_folder();

} // namespace vivid::ui
