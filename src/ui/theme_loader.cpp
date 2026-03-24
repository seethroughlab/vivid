#include "ui/theme_loader.h"
#include "runtime/platform.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <spawn.h>

#ifndef VIVID_CORE_VERSION
#define VIVID_CORE_VERSION "0.1.0"
#endif

extern "C" char** environ;
#include <set>

namespace vivid::ui {

// -----------------------------------------------------------------------
// Embedded fallback JSON themes
// -----------------------------------------------------------------------
// These are the 3 built-in themes encoded as JSON. Hex colors are the
// closest uint8 approximation of the original float values (max error
// ~0.002 per channel, imperceptible).

static const char kDarkSteelJson[] = R"j({
    "vivid_version": "0.1.0",
    "name": "Dark Steel",
    "corner_radius": 0,
    "node_bg": "#1f2126",
    "node_sel_bg": "#2e384d",
    "accent": "#598cd9",
    "slider_fill": "#406bad",
    "inspector_bg": "#1a1c21",
    "dim_text": "#8c949e",
    "bright_text": "#e6ebf2",
    "popup_bg": "rgba(36, 38, 46, 0.97)",
    "input_field_bg": "#14171c",
    "separator": "#383d47",
    "scrollbar_track": "#1f2126",
    "scrollbar_thumb": "#4d525c",
    "button_bg": "#383d47",
    "button_hover": "#474d59",
    "scrim": "rgba(0, 0, 0, 0.55)",
    "wire_color": "rgba(128, 153, 166, 0.7)",
    "wire_sel_color": "rgba(153, 191, 217, 0.9)",
    "slider_track": "#2e3038",
    "dark_bg": "#121417",
    "group_header_bg": "#24262b"
})j";

static const char kMidnightJson[] = R"j({
    "vivid_version": "0.1.0",
    "name": "Midnight",
    "corner_radius": 4,
    "node_bg": "#171a29",
    "node_sel_bg": "#242e52",
    "accent": "#6680e6",
    "slider_fill": "#4d61bf",
    "inspector_bg": "#121424",
    "dim_text": "#8087a6",
    "bright_text": "#d9e0f2",
    "popup_bg": "rgba(26, 28, 48, 0.97)",
    "input_field_bg": "#0f121f",
    "separator": "#2e334d",
    "scrollbar_track": "#171a29",
    "scrollbar_thumb": "#40476b",
    "button_bg": "#2e334d",
    "button_hover": "#3d4261",
    "scrim": "rgba(0, 0, 5, 0.6)",
    "wire_color": "rgba(115, 140, 184, 0.7)",
    "wire_sel_color": "rgba(140, 173, 235, 0.9)",
    "slider_track": "#24263d",
    "dark_bg": "#0d0f1a",
    "group_header_bg": "#1a1c2e"
})j";

static const char kSlateJson[] = R"j({
    "vivid_version": "0.1.0",
    "name": "Slate",
    "corner_radius": 6,
    "node_bg": "#262421",
    "node_sel_bg": "#3d3833",
    "accent": "#b8804d",
    "slider_fill": "#996b40",
    "inspector_bg": "#1f1c1a",
    "dim_text": "#948c85",
    "bright_text": "#ebe6de",
    "popup_bg": "rgba(43, 41, 38, 0.97)",
    "input_field_bg": "#1a1714",
    "separator": "#423d38",
    "scrollbar_track": "#262421",
    "scrollbar_thumb": "#57524d",
    "button_bg": "#423d38",
    "button_hover": "#524d47",
    "scrim": "rgba(5, 3, 0, 0.55)",
    "wire_color": "rgba(148, 140, 128, 0.7)",
    "wire_sel_color": "rgba(191, 173, 148, 0.9)",
    "slider_track": "#33302b",
    "dark_bg": "#14120f",
    "group_header_bg": "#292624"
})j";

static const char kEmeraldJson[] = R"j({
    "vivid_version": "0.1.0",
    "name": "Emerald",
    "corner_radius": 3,
    "node_bg": "#1a2420",
    "node_sel_bg": "#243830",
    "accent": "#4caf50",
    "slider_fill": "#388e3c",
    "inspector_bg": "#151e1a",
    "dim_text": "#7a9488",
    "bright_text": "#dce8e0",
    "popup_bg": "rgba(28, 38, 34, 0.97)",
    "input_field_bg": "#111a15",
    "separator": "#2e3d35",
    "scrollbar_track": "#1a2420",
    "scrollbar_thumb": "#3d5248",
    "button_bg": "#2e3d35",
    "button_hover": "#3a4d42",
    "scrim": "rgba(0, 5, 2, 0.55)",
    "wire_color": "rgba(100, 170, 140, 0.7)",
    "wire_sel_color": "rgba(130, 210, 170, 0.9)",
    "slider_track": "#243028",
    "dark_bg": "#0f1612",
    "group_header_bg": "#1e2b25"
})j";

static const char kCrimsonJson[] = R"j({
    "vivid_version": "0.1.0",
    "name": "Crimson",
    "corner_radius": 2,
    "node_bg": "#241a1c",
    "node_sel_bg": "#3d2830",
    "accent": "#d94452",
    "slider_fill": "#b33040",
    "inspector_bg": "#1e1416",
    "dim_text": "#9e858a",
    "bright_text": "#f0e6e8",
    "popup_bg": "rgba(40, 28, 32, 0.97)",
    "input_field_bg": "#1a1012",
    "separator": "#3d3035",
    "scrollbar_track": "#241a1c",
    "scrollbar_thumb": "#5c464c",
    "button_bg": "#3d3035",
    "button_hover": "#4d3d42",
    "scrim": "rgba(5, 0, 0, 0.55)",
    "wire_color": "rgba(170, 120, 130, 0.7)",
    "wire_sel_color": "rgba(217, 150, 160, 0.9)",
    "slider_track": "#302428",
    "dark_bg": "#160f11",
    "group_header_bg": "#2b1e22"
})j";

static const char kVaporJson[] = R"j({
    "vivid_version": "0.1.0",
    "name": "Vapor",
    "corner_radius": 5,
    "node_bg": "#1e1826",
    "node_sel_bg": "#302445",
    "accent": "#e050a0",
    "slider_fill": "#a040b8",
    "inspector_bg": "#181220",
    "dim_text": "#8878a0",
    "bright_text": "#e8daf0",
    "popup_bg": "rgba(32, 24, 42, 0.97)",
    "input_field_bg": "#130e1a",
    "separator": "#352845",
    "scrollbar_track": "#1e1826",
    "scrollbar_thumb": "#4a3868",
    "button_bg": "#352845",
    "button_hover": "#453858",
    "scrim": "rgba(5, 0, 8, 0.6)",
    "wire_color": "rgba(160, 120, 190, 0.7)",
    "wire_sel_color": "rgba(210, 150, 230, 0.9)",
    "slider_track": "#281e35",
    "dark_bg": "#100c16",
    "group_header_bg": "#241c30"
})j";

static const char kCarbonJson[] = R"j({
    "vivid_version": "0.1.0",
    "name": "Carbon",
    "corner_radius": 0,
    "node_bg": "#151515",
    "node_sel_bg": "#1a2528",
    "accent": "#00bcd4",
    "slider_fill": "#0097a7",
    "inspector_bg": "#101010",
    "dim_text": "#686868",
    "bright_text": "#d0d0d0",
    "popup_bg": "rgba(18, 18, 18, 0.97)",
    "input_field_bg": "#0a0a0a",
    "separator": "#2a2a2a",
    "scrollbar_track": "#151515",
    "scrollbar_thumb": "#3a3a3a",
    "button_bg": "#2a2a2a",
    "button_hover": "#353535",
    "scrim": "rgba(0, 0, 0, 0.6)",
    "wire_color": "rgba(80, 150, 160, 0.7)",
    "wire_sel_color": "rgba(100, 200, 215, 0.9)",
    "slider_track": "#1e1e1e",
    "dark_bg": "#080808",
    "group_header_bg": "#1a1a1a"
})j";

static const char kMonokaiJson[] = R"j({
    "vivid_version": "0.1.0",
    "name": "Monokai",
    "corner_radius": 4,
    "node_bg": "#272822",
    "node_sel_bg": "#3e3d32",
    "accent": "#a6e22e",
    "slider_fill": "#8cc41e",
    "inspector_bg": "#1e1f1a",
    "dim_text": "#75715e",
    "bright_text": "#f8f8f2",
    "popup_bg": "rgba(40, 41, 35, 0.97)",
    "input_field_bg": "#1a1b15",
    "separator": "#3e3d32",
    "scrollbar_track": "#272822",
    "scrollbar_thumb": "#52514a",
    "button_bg": "#3e3d32",
    "button_hover": "#4e4d40",
    "scrim": "rgba(2, 2, 0, 0.55)",
    "wire_color": "rgba(150, 148, 130, 0.7)",
    "wire_sel_color": "rgba(190, 200, 150, 0.9)",
    "slider_track": "#33332b",
    "dark_bg": "#181910",
    "group_header_bg": "#2e2e26"
})j";

struct EmbeddedTheme {
    const char* id;
    const char* name;
    const char* json;
    size_t json_len;
};

static const EmbeddedTheme kEmbeddedThemes[] = {
    {"dark_steel", "Dark Steel", kDarkSteelJson, sizeof(kDarkSteelJson) - 1},
    {"midnight",   "Midnight",   kMidnightJson,  sizeof(kMidnightJson) - 1},
    {"slate",      "Slate",      kSlateJson,     sizeof(kSlateJson) - 1},
    {"emerald",    "Emerald",    kEmeraldJson,   sizeof(kEmeraldJson) - 1},
    {"crimson",    "Crimson",    kCrimsonJson,   sizeof(kCrimsonJson) - 1},
    {"vapor",      "Vapor",      kVaporJson,     sizeof(kVaporJson) - 1},
    {"carbon",     "Carbon",     kCarbonJson,    sizeof(kCarbonJson) - 1},
    {"monokai",    "Monokai",    kMonokaiJson,   sizeof(kMonokaiJson) - 1},
};
static constexpr int kNumEmbedded = static_cast<int>(std::size(kEmbeddedThemes));

static bool is_builtin_id(const std::string& id) {
    for (int i = 0; i < kNumEmbedded; i++) {
        if (id == kEmbeddedThemes[i].id) return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// Color parsing
// -----------------------------------------------------------------------

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static int hex_byte(const char* s) {
    int hi = hex_digit(s[0]);
    int lo = hex_digit(s[1]);
    if (hi < 0 || lo < 0) return -1;
    return hi * 16 + lo;
}

static bool parse_hex_color(const char* str, float* out, int* components) {
    if (str[0] != '#') return false;
    size_t len = std::strlen(str + 1);
    if (len == 6) {
        for (int i = 0; i < 3; i++) {
            int v = hex_byte(str + 1 + i * 2);
            if (v < 0) return false;
            out[i] = v / 255.0f;
        }
        *components = 3;
        return true;
    } else if (len == 8) {
        for (int i = 0; i < 4; i++) {
            int v = hex_byte(str + 1 + i * 2);
            if (v < 0) return false;
            out[i] = v / 255.0f;
        }
        *components = 4;
        return true;
    }
    return false;
}

static bool parse_rgba_color(const char* str, float* out) {
    // rgba(R, G, B, A) — R,G,B are 0-255 ints, A is 0-1 float
    int r, g, b;
    float a;
    if (std::sscanf(str, "rgba( %d , %d , %d , %f )", &r, &g, &b, &a) == 4) {
        out[0] = r / 255.0f;
        out[1] = g / 255.0f;
        out[2] = b / 255.0f;
        out[3] = a;
        return true;
    }
    return false;
}

// Parse a color from a JSON string value. Returns component count (3 or 4), or 0 on failure.
static int parse_color_str(const std::string& str, float* out) {
    int components = 0;
    if (!str.empty() && str[0] == '#') {
        if (parse_hex_color(str.c_str(), out, &components)) return components;
    } else if (str.compare(0, 5, "rgba(") == 0) {
        if (parse_rgba_color(str.c_str(), out)) return 4;
    }
    return 0;
}

static bool read_color3(const nlohmann::json& root, const char* key, std::array<float, 3>& arr) {
    auto it = root.find(key);
    if (it == root.end() || !it->is_string()) return false;
    float buf[4];
    int n = parse_color_str(it->get<std::string>(), buf);
    if (n >= 3) {
        arr = {buf[0], buf[1], buf[2]};
        return true;
    }
    return false;
}

static bool read_color4(const nlohmann::json& root, const char* key, std::array<float, 4>& arr) {
    auto it = root.find(key);
    if (it == root.end() || !it->is_string()) return false;
    float buf[4];
    int n = parse_color_str(it->get<std::string>(), buf);
    if (n == 4) {
        arr = {buf[0], buf[1], buf[2], buf[3]};
        return true;
    } else if (n == 3) {
        arr = {buf[0], buf[1], buf[2], 1.0f};
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// Default style (Dark Steel values, used for missing JSON fields)
// -----------------------------------------------------------------------

static UIStyle default_style() {
    UIStyle s;
    s.name = "Dark Steel";
    s.id = "dark_steel";
    s.corner_radius = 0.0f;

    s.node_bg       = { 0.12f, 0.13f, 0.15f };
    s.node_sel_bg   = { 0.18f, 0.22f, 0.30f };
    s.accent        = { 0.35f, 0.55f, 0.85f };
    s.slider_fill   = { 0.25f, 0.42f, 0.68f };
    s.inspector_bg  = { 0.10f, 0.11f, 0.13f };
    s.dim_text      = { 0.55f, 0.58f, 0.62f };
    s.bright_text   = { 0.90f, 0.92f, 0.95f };

    s.popup_bg      = { 0.14f, 0.15f, 0.18f, 0.97f };
    s.input_field_bg = { 0.08f, 0.09f, 0.11f };
    s.separator     = { 0.22f, 0.24f, 0.28f };
    s.scrollbar_track = { 0.12f, 0.13f, 0.15f };
    s.scrollbar_thumb = { 0.30f, 0.32f, 0.36f };
    s.button_bg     = { 0.22f, 0.24f, 0.28f };
    s.button_hover  = { 0.28f, 0.30f, 0.35f };
    s.scrim         = { 0.0f, 0.0f, 0.0f, 0.55f };

    s.wire_color    = { 0.5f, 0.6f, 0.65f, 0.7f };
    s.wire_sel_color = { 0.6f, 0.75f, 0.85f, 0.9f };
    s.wire_thickness       = 1.0f;
    s.wire_hover_thickness = 3.0f;
    s.wire_param_thickness = 1.5f;

    s.slider_track  = { 0.18f, 0.19f, 0.22f };
    s.dark_bg       = { 0.07f, 0.08f, 0.09f };
    s.group_header_bg = { 0.14f, 0.15f, 0.17f };

    return s;
}

// -----------------------------------------------------------------------
// JSON parsing
// -----------------------------------------------------------------------

// Returns the major version component (first integer before '.'), or -1 on parse failure.
static int major_version_of(const std::string& semver) {
    if (semver.empty()) return -1;
    const char* s = semver.c_str();
    if (*s == 'v' || *s == 'V') ++s;
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s || v < 0) return -1;
    return static_cast<int>(v);
}

static std::optional<UIStyle> parse_theme_root(const nlohmann::json& root) {
    if (!root.is_object()) return std::nullopt;

    // Start with defaults so missing fields get sensible values
    UIStyle s = default_style();

    if (auto it = root.find("vivid_version"); it != root.end() && it->is_string()) {
        s.vivid_version = it->get<std::string>();
        int theme_major = major_version_of(s.vivid_version);
        int core_major  = major_version_of(VIVID_CORE_VERSION);
        if (theme_major >= 0 && core_major >= 0 && theme_major != core_major) {
            std::fprintf(stderr,
                "[theme] Major version mismatch: theme vivid_version=%s, core=%s\n",
                s.vivid_version.c_str(), VIVID_CORE_VERSION);
        }
    }
    if (auto it = root.find("name"); it != root.end() && it->is_string())
        s.name = it->get<std::string>();
    if (auto it = root.find("corner_radius"); it != root.end() && it->is_number())
        s.corner_radius = it->get<float>();

    read_color3(root, "node_bg", s.node_bg);
    read_color3(root, "node_sel_bg", s.node_sel_bg);
    read_color3(root, "accent", s.accent);
    read_color3(root, "slider_fill", s.slider_fill);
    read_color3(root, "inspector_bg", s.inspector_bg);
    read_color3(root, "dim_text", s.dim_text);
    read_color3(root, "bright_text", s.bright_text);

    read_color4(root, "popup_bg", s.popup_bg);
    read_color3(root, "input_field_bg", s.input_field_bg);
    read_color3(root, "separator", s.separator);
    read_color3(root, "scrollbar_track", s.scrollbar_track);
    read_color3(root, "scrollbar_thumb", s.scrollbar_thumb);
    read_color3(root, "button_bg", s.button_bg);
    read_color3(root, "button_hover", s.button_hover);
    read_color4(root, "scrim", s.scrim);

    read_color4(root, "wire_color", s.wire_color);
    read_color4(root, "wire_sel_color", s.wire_sel_color);
    if (auto it = root.find("wire_thickness"); it != root.end() && it->is_number())
        s.wire_thickness = it->get<float>();
    if (auto it = root.find("wire_hover_thickness"); it != root.end() && it->is_number())
        s.wire_hover_thickness = it->get<float>();
    if (auto it = root.find("wire_param_thickness"); it != root.end() && it->is_number())
        s.wire_param_thickness = it->get<float>();

    read_color3(root, "slider_track", s.slider_track);
    read_color3(root, "dark_bg", s.dark_bg);
    read_color3(root, "group_header_bg", s.group_header_bg);

    return s;
}

std::optional<UIStyle> parse_theme_json(const char* json, size_t len) {
    try {
        auto j = nlohmann::json::parse(json, json + len);
        return parse_theme_root(j);
    } catch (const nlohmann::json::parse_error& e) {
        std::fprintf(stderr, "[vivid] Theme JSON parse error: %s\n", e.what());
        return std::nullopt;
    }
}

static std::optional<UIStyle> load_theme_file(const std::string& path) {
    try {
        std::ifstream ifs(path);
        if (!ifs) {
            std::fprintf(stderr, "[vivid] Failed to read theme %s: could not open file\n",
                         path.c_str());
            return std::nullopt;
        }
        auto j = nlohmann::json::parse(ifs);
        return parse_theme_root(j);
    } catch (const nlohmann::json::parse_error& e) {
        std::fprintf(stderr, "[vivid] Failed to read theme %s: %s\n",
                     path.c_str(), e.what());
        return std::nullopt;
    }
}

// -----------------------------------------------------------------------
// Theme directory
// -----------------------------------------------------------------------

std::string get_themes_dir() {
    return vivid::get_config_dir() + "/themes";
}

// -----------------------------------------------------------------------
// Discovery
// -----------------------------------------------------------------------

std::vector<ThemeInfo> discover_themes() {
    std::vector<ThemeInfo> result;
    std::string dir = get_themes_dir();
    std::set<std::string> found_ids;

    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;

            std::string id = entry.path().stem().string();
            std::string name = id;

            // Peek at the "name" field
            try {
                std::ifstream ifs(entry.path());
                auto j = nlohmann::json::parse(ifs);
                if (j.is_object()) {
                    auto it = j.find("name");
                    if (it != j.end() && it->is_string())
                        name = it->get<std::string>();
                }
            } catch (...) {}

            found_ids.insert(id);
            result.push_back({name, id, entry.path().string(), is_builtin_id(id)});
        }
    }

    // Ensure embedded fallbacks for any missing built-in themes
    for (int i = 0; i < kNumEmbedded; i++) {
        if (found_ids.count(kEmbeddedThemes[i].id) == 0) {
            result.push_back({kEmbeddedThemes[i].name,
                              kEmbeddedThemes[i].id, "", true});
        }
    }

    return result;
}

// -----------------------------------------------------------------------
// Loading
// -----------------------------------------------------------------------

std::optional<UIStyle> load_theme(const std::string& theme_id,
                                  const std::vector<ThemeInfo>& themes) {
    const ThemeInfo* info = nullptr;
    for (const auto& t : themes) {
        if (t.id == theme_id) { info = &t; break; }
    }
    if (!info) return std::nullopt;

    std::optional<UIStyle> result;

    // Try file first
    if (!info->path.empty()) {
        result = load_theme_file(info->path);
    }

    // Fall back to embedded
    if (!result && info->is_builtin) {
        for (int i = 0; i < kNumEmbedded; i++) {
            if (theme_id == kEmbeddedThemes[i].id) {
                result = parse_theme_json(kEmbeddedThemes[i].json,
                                          kEmbeddedThemes[i].json_len);
                break;
            }
        }
    }

    if (result) {
        result->id = theme_id;
    }
    return result;
}

std::vector<UIStyle> load_all_themes(const std::vector<ThemeInfo>& themes) {
    std::vector<UIStyle> result;
    for (const auto& t : themes) {
        auto style = load_theme(t.id, themes);
        if (style) result.push_back(std::move(*style));
    }
    return result;
}

// -----------------------------------------------------------------------
// First-launch: write default theme files
// -----------------------------------------------------------------------

void ensure_default_themes() {
    namespace fs = std::filesystem;
    std::string dir = get_themes_dir();

    std::error_code ec;
    fs::create_directories(dir, ec);

    // Only write if directory is empty (no .json files)
    bool has_json = false;
    if (fs::is_directory(dir, ec)) {
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                has_json = true;
                break;
            }
        }
    }

    if (has_json) return;

    // Write all embedded themes as files
    for (int i = 0; i < kNumEmbedded; i++) {
        std::string path = dir + "/" + kEmbeddedThemes[i].id + ".json";
        std::ofstream ofs(path);
        if (ofs) {
            ofs.write(kEmbeddedThemes[i].json,
                      static_cast<std::streamsize>(kEmbeddedThemes[i].json_len));
        } else {
            std::fprintf(stderr, "[vivid] Failed to write default theme: %s\n",
                         path.c_str());
        }
    }
}

// -----------------------------------------------------------------------
// Open themes folder in OS file manager
// -----------------------------------------------------------------------

void open_themes_folder() {
    std::string dir = get_themes_dir();

    // Ensure directory exists
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);

#if defined(__APPLE__)
    pid_t pid;
    const char* argv[] = { "/usr/bin/open", dir.c_str(), nullptr };
    posix_spawn(&pid, argv[0], nullptr, nullptr,
                const_cast<char* const*>(argv), environ);
#elif defined(_WIN32)
    // No posix_spawn on Windows — but no shell interpolation either
    std::string cmd = "explorer \"" + dir + "\"";
    std::system(cmd.c_str());
#else
    pid_t pid;
    const char* argv[] = { "xdg-open", dir.c_str(), nullptr };
    posix_spawn(&pid, argv[0], nullptr, nullptr,
                const_cast<char* const*>(argv), environ);
#endif
}

} // namespace vivid::ui
