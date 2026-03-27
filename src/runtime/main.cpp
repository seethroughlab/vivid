#include "runtime/gpu_context.h"
#include "runtime/fullscreen_blit.h"
#include "runtime/output_window.h"
#include "ui/thumbnail_cache.h"
#include "ui/thumbnail_renderer.h"
#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/runtime_core.h"
#include "runtime/audio_engine.h"
#include "runtime/cadence_bridge.h"
#include "runtime/compiled_graph.h"
#include "runtime/file_watcher.h"
#include "runtime/hot_reload.h"
#include "runtime/runtime_api.h"
#include "ui/renderer_2d.h"
#include "ui/node_graph.h"
#include "ui/graph_snapshot.h"
#include "ui/ui_command_sink.h"
#include "runtime/builtin_operators.h"
#include "runtime/control_server.h"
#include "runtime/capture_coordinator.h"
#include "runtime/system_midi.h"
#include "runtime/settings.h"
#include "runtime/editor_detect.h"
#include "runtime/operator_info_cache.h"
#include "runtime/runtime_command_sink.h"
#include "runtime/file_drop_registry.h"
#include "runtime/crash_guard.h"
#include "ui/ui_style.h"
#include "ui/theme_loader.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/data_driven_filter.h"
#include "operator_api/thumbnail.h"
#include "operator_api/types.h"
#include "operator_api/input_state.h"
#include "common/gpu_util.h"
#include "export/export_pipeline.h"
#include "runtime/package_compiler.h"
#include "runtime/package_manager.h"
#include "runtime/package_catalog.h"
#include "runtime/package_scaffolder.h"
#include "runtime/app_update_manager.h"
#include "runtime/platform.h"
#include "runtime/operator_creator.h"
#include "runtime/operator_destination_policy.h"
#include "ui/file_dialog.h"
#include <fstream>
#include <sstream>
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <GLFW/glfw3.h>
#include <stb_image_write.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <string>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>
#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#ifdef __APPLE__
#include "runtime/macos_frame_timer.h"
#include "runtime/macos_menu.h"
#include "runtime/sparkle_bridge.h"
#endif

// #16191D in sRGB → linear: pow(x/255, 2.2)
static constexpr double kClearLinear[4]  = { 0.00699, 0.00821, 0.01041, 1.0 };
// #16191D as raw unorm (no gamma conversion)
static constexpr double kClearRaw[4]     = { 0.0863, 0.0980, 0.1137, 1.0 };

// Thumbnail size: node width × 16:10 aspect
static constexpr uint32_t kThumbW = 140;
static constexpr uint32_t kThumbH = 88;

// Default GPU texture resolution for nodes without explicit size
static constexpr uint32_t kDefaultTexW = 800;
static constexpr uint32_t kDefaultTexH = 600;

static bool is_srgb_format(WGPUTextureFormat fmt) {
    switch (fmt) {
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_BGRA8UnormSrgb:
            return true;
        default:
            return false;
    }
}

using vivid::to_sv;

#ifndef VIVID_CORE_VERSION
#define VIVID_CORE_VERSION "0.1.0"
#endif

static std::string url_encode(const std::string& text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
            continue;
        }
        out.push_back('%');
        out.push_back(kHex[(c >> 4) & 0x0F]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

static std::string platform_label() {
#if defined(__APPLE__)
    return "macOS";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

static void stbi_write_to_vec(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<const uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

static std::string now_epoch_seconds_str() {
    auto now = std::chrono::system_clock::now();
    auto sec = std::chrono::time_point_cast<std::chrono::seconds>(now)
                   .time_since_epoch().count();
    return std::to_string(static_cast<long long>(sec));
}

static std::vector<std::string> json_str_array(const nlohmann::json& arr) {
    std::vector<std::string> out;
    if (!arr.is_array()) return out;
    for (const auto& v : arr) {
        if (v.is_string()) out.emplace_back(v.get<std::string>());
    }
    return out;
}

enum class UITestActionType {
    Wait,
    MouseMove,
    MouseButton,
    Key,
    CharInput,
    Screenshot,
    Checkpoint,
};

struct UITestAction {
    UITestActionType type = UITestActionType::Wait;
    int frames = 0;
    float x = 0.0f;
    float y = 0.0f;
    int button = GLFW_MOUSE_BUTTON_LEFT;
    int mouse_action = GLFW_PRESS;
    int key = 0;
    int key_action = GLFW_PRESS;
    int mods = 0;
    unsigned int codepoint = 0;
    std::string screenshot_path;
    int screenshot_delay = 0;
    std::string checkpoint_label;
};

struct UITestScript {
    std::vector<UITestAction> actions;
    size_t next_action = 0;
    int wait_frames_remaining = 0;
    std::filesystem::path source_dir;
    std::vector<std::string> pending_checkpoint_labels;
};

struct UITestNodeState {
    std::string node_id;
    std::string type_name;
    bool missing_operator = false;
    bool has_layout = false;
    float layout_x = 0.0f;
    float layout_y = 0.0f;
    std::unordered_map<std::string, std::string> file_param_values;
};

struct UITestConnectionState {
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
    bool invalid = false;
};

struct UITestObservedState {
    std::vector<UITestNodeState> nodes;
    std::vector<UITestConnectionState> connections;
    std::vector<std::string> selected_node_ids;
    bool chooser_open = false;
    bool file_drop_chooser_open = false;
    bool role_chooser_open = false;
    vivid::ui::FileDialogTestStats file_dialog_stats;
};

struct UITestCheckpointState {
    std::string label;
    UITestObservedState state;
};

struct UITestDumpState {
    bool has_final_state = false;
    UITestObservedState final_state;
    std::vector<UITestCheckpointState> checkpoints;
};

static std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static bool parse_ui_test_modifiers(const nlohmann::json& arr, int& mods, std::string& error) {
    mods = 0;
    if (arr.is_null()) return true;
    if (!arr.is_array()) {
        error = "mods must be an array";
        return false;
    }
    for (const auto& value : arr) {
        if (!value.is_string()) {
            error = "mods entries must be strings";
            return false;
        }
        std::string mod = lower_copy(value.get<std::string>());
        if (mod == "shift") mods |= GLFW_MOD_SHIFT;
        else if (mod == "control" || mod == "ctrl") mods |= GLFW_MOD_CONTROL;
        else if (mod == "super" || mod == "cmd" || mod == "meta") mods |= GLFW_MOD_SUPER;
        else if (mod == "alt" || mod == "option") mods |= GLFW_MOD_ALT;
        else {
            error = "unknown modifier: " + mod;
            return false;
        }
    }
    return true;
}

static bool parse_ui_test_mouse_button(const std::string& button_name, int& button) {
    std::string name = lower_copy(button_name);
    if (name == "left") {
        button = GLFW_MOUSE_BUTTON_LEFT;
        return true;
    }
    if (name == "right") {
        button = GLFW_MOUSE_BUTTON_RIGHT;
        return true;
    }
    if (name == "middle") {
        button = GLFW_MOUSE_BUTTON_MIDDLE;
        return true;
    }
    return false;
}

static bool parse_ui_test_action_name(const std::string& action_name, int& action) {
    std::string name = lower_copy(action_name);
    if (name == "press") {
        action = GLFW_PRESS;
        return true;
    }
    if (name == "release") {
        action = GLFW_RELEASE;
        return true;
    }
    if (name == "repeat") {
        action = GLFW_REPEAT;
        return true;
    }
    return false;
}

static bool parse_ui_test_key_name(const std::string& key_name, int& key) {
    static const std::unordered_map<std::string, int> named_keys = {
        {"tab", GLFW_KEY_TAB},
        {"enter", GLFW_KEY_ENTER},
        {"escape", GLFW_KEY_ESCAPE},
        {"esc", GLFW_KEY_ESCAPE},
        {"delete", GLFW_KEY_DELETE},
        {"backspace", GLFW_KEY_BACKSPACE},
        {"left", GLFW_KEY_LEFT},
        {"right", GLFW_KEY_RIGHT},
        {"up", GLFW_KEY_UP},
        {"down", GLFW_KEY_DOWN},
        {"space", GLFW_KEY_SPACE},
    };
    std::string name = lower_copy(key_name);
    auto it = named_keys.find(name);
    if (it != named_keys.end()) {
        key = it->second;
        return true;
    }
    if (name.size() == 1) {
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        if (c >= 'A' && c <= 'Z') {
            key = GLFW_KEY_A + (c - 'A');
            return true;
        }
        if (c >= '0' && c <= '9') {
            key = GLFW_KEY_0 + (c - '0');
            return true;
        }
    }
    return false;
}

static bool load_ui_test_script(const std::string& script_path,
                                UITestScript& script,
                                std::string& error) {
    nlohmann::json root;
    try {
        std::ifstream ifs(script_path);
        if (!ifs) {
            error = "failed to read script JSON";
            return false;
        }
        root = nlohmann::json::parse(ifs);
    } catch (const std::exception&) {
        error = "failed to read script JSON";
        return false;
    }

    const nlohmann::json* actions_ptr = &root;
    if (root.is_object() && root.contains("actions"))
        actions_ptr = &root["actions"];
    if (!actions_ptr->is_array()) {
        error = "script must be a JSON array or an object with an 'actions' array";
        return false;
    }
    const auto& actions = *actions_ptr;

    script = {};
    script.source_dir = std::filesystem::path(script_path).parent_path();
    for (const auto& item : actions) {
        if (!item.is_object()) {
            error = "script actions must be objects";
            return false;
        }
        auto type_it = item.find("type");
        if (type_it == item.end() || !type_it->is_string()) {
            error = "script action missing string 'type'";
            return false;
        }

        UITestAction action;
        std::string type = lower_copy(type_it->get<std::string>());
        if (type == "wait") {
            action.type = UITestActionType::Wait;
            auto frames_it = item.find("frames");
            action.frames = (frames_it != item.end() && frames_it->is_number_integer())
                ? frames_it->get<int>()
                : 1;
        } else if (type == "mouse_move") {
            auto x_it = item.find("x");
            auto y_it = item.find("y");
            if (x_it == item.end() || y_it == item.end() || !x_it->is_number() || !y_it->is_number()) {
                error = "mouse_move requires numeric x and y";
                return false;
            }
            action.type = UITestActionType::MouseMove;
            action.x = x_it->get<float>();
            action.y = y_it->get<float>();
        } else if (type == "mouse_button") {
            auto button_it = item.find("button");
            auto action_it = item.find("action");
            nlohmann::json mods_val = item.contains("mods") ? item["mods"] : nlohmann::json(nullptr);
            if (button_it == item.end() || action_it == item.end() ||
                !button_it->is_string() || !action_it->is_string() ||
                !parse_ui_test_mouse_button(button_it->get<std::string>(), action.button) ||
                !parse_ui_test_action_name(action_it->get<std::string>(), action.mouse_action) ||
                !parse_ui_test_modifiers(mods_val, action.mods, error)) {
                if (error.empty())
                    error = "mouse_button requires valid button/action/mods";
                return false;
            }
            action.type = UITestActionType::MouseButton;
        } else if (type == "key") {
            auto key_it = item.find("key");
            auto action_it = item.find("action");
            nlohmann::json mods_val = item.contains("mods") ? item["mods"] : nlohmann::json(nullptr);
            if (key_it == item.end() || action_it == item.end() ||
                !key_it->is_string() || !action_it->is_string() ||
                !parse_ui_test_key_name(key_it->get<std::string>(), action.key) ||
                !parse_ui_test_action_name(action_it->get<std::string>(), action.key_action) ||
                !parse_ui_test_modifiers(mods_val, action.mods, error)) {
                if (error.empty())
                    error = "key requires valid key/action/mods";
                return false;
            }
            action.type = UITestActionType::Key;
        } else if (type == "char") {
            auto value_it = item.find("value");
            if (value_it == item.end() || !value_it->is_string()) {
                error = "char requires string value";
                return false;
            }
            std::string value = value_it->get<std::string>();
            if (value.empty()) {
                error = "char value must not be empty";
                return false;
            }
            action.type = UITestActionType::CharInput;
            action.codepoint = static_cast<unsigned int>(static_cast<unsigned char>(value[0]));
        } else if (type == "screenshot") {
            auto path_it = item.find("path");
            if (path_it == item.end() || !path_it->is_string()) {
                error = "screenshot requires string path";
                return false;
            }
            action.type = UITestActionType::Screenshot;
            action.screenshot_path = path_it->get<std::string>();
            auto delay_it = item.find("delay_frames");
            if (delay_it != item.end() && delay_it->is_number_integer())
                action.screenshot_delay = delay_it->get<int>();
        } else if (type == "checkpoint") {
            auto label_it = item.find("label");
            if (label_it == item.end() || !label_it->is_string()) {
                error = "checkpoint requires string label";
                return false;
            }
            action.type = UITestActionType::Checkpoint;
            action.checkpoint_label = label_it->get<std::string>();
        } else {
            error = "unknown script action type: " + type;
            return false;
        }

        script.actions.push_back(std::move(action));
    }

    return true;
}

static vivid::ui::FileDialogTestStats current_file_dialog_test_stats() {
#ifdef __APPLE__
    return vivid::ui::file_dialog_test_stats();
#else
    return {};
#endif
}

static void reset_file_dialog_test_stats_runtime() {
#ifdef __APPLE__
    vivid::ui::reset_file_dialog_test_stats();
#endif
}

static UITestObservedState capture_ui_test_observed_state(
    const vivid::ui::GraphSnapshot& snapshot,
    const vivid::ui::NodeGraphUI& graph_ui) {
    UITestObservedState out;
    out.nodes.reserve(snapshot.nodes.size());
    for (const auto& node : snapshot.nodes) {
        UITestNodeState state;
        state.node_id = node.node_id;
        state.type_name = node.type_name;
        state.missing_operator = node.missing_operator;
        state.has_layout = node.has_layout;
        state.layout_x = node.layout_x;
        state.layout_y = node.layout_y;
        state.file_param_values = node.file_param_values;
        out.nodes.push_back(std::move(state));
    }
    out.connections.reserve(snapshot.connections.size());
    for (const auto& conn : snapshot.connections) {
        UITestConnectionState state;
        state.from_node = conn.from_node;
        state.from_port = conn.from_port;
        state.to_node = conn.to_node;
        state.to_port = conn.to_port;
        state.invalid = conn.invalid;
        out.connections.push_back(std::move(state));
    }
    out.selected_node_ids = graph_ui.selected_node_ids_for_test();
    std::sort(out.selected_node_ids.begin(), out.selected_node_ids.end());
    out.chooser_open = graph_ui.chooser_open_for_test();
    out.file_drop_chooser_open = graph_ui.file_drop_chooser_open_for_test();
    out.file_dialog_stats = current_file_dialog_test_stats();
    return out;
}

static std::string sanitize_json_string(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (c >= 0x20 && c < 0x7f) {
            out.push_back(static_cast<char>(c));
        } else if (c == '\n' || c == '\r' || c == '\t') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('?');
        }
    }
    return out;
}

static nlohmann::json encode_ui_test_state(const UITestObservedState& state) {
    nlohmann::json root = nlohmann::json::object();
    root["node_count"] = static_cast<int64_t>(state.nodes.size());
    root["connection_count"] = static_cast<int64_t>(state.connections.size());
    root["chooser_open"] = state.chooser_open;
    root["file_drop_chooser_open"] = state.file_drop_chooser_open;
    root["role_chooser_open"] = state.role_chooser_open;
    root["native_file_dialog_count"] = state.file_dialog_stats.invocation_count;

    nlohmann::json dialog_stats = nlohmann::json::object();
    dialog_stats["invocation_count"] = state.file_dialog_stats.invocation_count;
    dialog_stats["open_file_count"] = state.file_dialog_stats.open_file_count;
    dialog_stats["open_directory_count"] = state.file_dialog_stats.open_directory_count;
    dialog_stats["save_file_count"] = state.file_dialog_stats.save_file_count;
    dialog_stats["save_directory_count"] = state.file_dialog_stats.save_directory_count;
    root["file_dialog_stats"] = dialog_stats;

    nlohmann::json selected = nlohmann::json::array();
    for (const auto& node_id : state.selected_node_ids) {
        selected.push_back(sanitize_json_string(node_id));
    }
    root["selected_node_ids"] = selected;

    nlohmann::json nodes = nlohmann::json::array();
    for (const auto& node : state.nodes) {
        nlohmann::json item = nlohmann::json::object();
        item["node_id"] = sanitize_json_string(node.node_id);
        item["type_name"] = sanitize_json_string(node.type_name);
        item["missing_operator"] = node.missing_operator;
        item["has_layout"] = node.has_layout;
        item["layout_x"] = node.layout_x;
        item["layout_y"] = node.layout_y;
        nlohmann::json file_params = nlohmann::json::object();
        std::vector<std::string> keys;
        keys.reserve(node.file_param_values.size());
        for (const auto& [key, _] : node.file_param_values)
            keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys) {
            auto it = node.file_param_values.find(key);
            file_params[sanitize_json_string(key)] = sanitize_json_string(it->second);
        }
        item["file_params"] = file_params;
        nodes.push_back(item);
    }
    root["nodes"] = nodes;

    nlohmann::json connections = nlohmann::json::array();
    for (const auto& conn : state.connections) {
        nlohmann::json item = nlohmann::json::object();
        item["from_node"] = sanitize_json_string(conn.from_node);
        item["from_port"] = sanitize_json_string(conn.from_port);
        item["to_node"] = sanitize_json_string(conn.to_node);
        item["to_port"] = sanitize_json_string(conn.to_port);
        item["invalid"] = conn.invalid;
        connections.push_back(item);
    }
    root["connections"] = connections;
    return root;
}

static bool write_ui_test_dump_file(const std::string& path,
                                    const UITestDumpState& dump,
                                    std::string& error) {
    nlohmann::json root = nlohmann::json::object();
    root["has_final_state"] = dump.has_final_state;
    if (dump.has_final_state) {
        root["final_state"] = encode_ui_test_state(dump.final_state);
    }

    nlohmann::json checkpoints = nlohmann::json::array();
    for (const auto& checkpoint : dump.checkpoints) {
        nlohmann::json item = nlohmann::json::object();
        item["label"] = sanitize_json_string(checkpoint.label);
        item["state"] = encode_ui_test_state(checkpoint.state);
        checkpoints.push_back(item);
    }
    root["checkpoints"] = checkpoints;

    try {
        std::ofstream ofs(path);
        if (!ofs) {
            error = "failed to write JSON";
            return false;
        }
        ofs << root.dump(4);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

static std::string trim_copy(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

static std::vector<std::string> split_csv(const std::string& csv) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= csv.size()) {
        size_t comma = csv.find(',', pos);
        if (comma == std::string::npos) comma = csv.size();
        std::string tok = trim_copy(csv.substr(pos, comma - pos));
        if (!tok.empty()) out.push_back(tok);
        pos = comma + 1;
    }
    return out;
}

static std::string join_csv(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ", ";
        out += items[i];
    }
    return out;
}

static bool load_example_entry_from_graph(const std::filesystem::path& graph_path,
                                          const std::filesystem::path& graphs_root,
                                          vivid::ui::ExampleEntry& out) {
    nlohmann::json root;
    try {
        std::ifstream ifs(graph_path);
        if (!ifs) return false;
        root = nlohmann::json::parse(ifs);
    } catch (const std::exception&) {
        return false;
    }
    if (!root.is_object()) return false;

    std::string rel = std::filesystem::relative(graph_path, graphs_root).generic_string();
    std::string stem = graph_path.stem().string();
    out.path = rel;
    out.id = stem;
    out.title = stem;
    out.summary = "";
    out.difficulty = "intermediate";
    out.featured_rank = 1000;

    if (root.contains("meta") && root["meta"].is_object()) {
        const auto& meta = root["meta"];
        if (meta.contains("id") && meta["id"].is_string()) out.id = meta["id"].get<std::string>();
        if (meta.contains("title") && meta["title"].is_string()) out.title = meta["title"].get<std::string>();
        if (meta.contains("description") && meta["description"].is_string()) out.summary = meta["description"].get<std::string>();
        if (meta.contains("difficulty") && meta["difficulty"].is_string()) out.difficulty = meta["difficulty"].get<std::string>();
        if (meta.contains("featured_rank") && meta["featured_rank"].is_number_integer()) {
            out.featured_rank = meta["featured_rank"].get<int>();
        }
        if (meta.contains("estimated_minutes") && meta["estimated_minutes"].is_number_integer()) {
            out.estimated_minutes = meta["estimated_minutes"].get<int>();
        }
        out.tags = json_str_array(meta.value("tags", nlohmann::json()));
        out.envs = json_str_array(meta.value("envs", nlohmann::json()));
        out.requires_packages = json_str_array(meta.value("requires_packages", nlohmann::json()));
    }

    if (out.title.empty()) out.title = stem;
    if (out.id.empty()) out.id = stem;
    return true;
}

static bool load_graph_meta_edit_data(const std::string& graph_path,
                                      vivid::ui::GraphMetaEditData& out,
                                      std::string& error) {
    nlohmann::json root;
    try {
        std::ifstream ifs(graph_path);
        if (!ifs) {
            error = "Failed to read graph JSON";
            return false;
        }
        root = nlohmann::json::parse(ifs);
    } catch (const std::exception&) {
        error = "Failed to read graph JSON";
        return false;
    }
    if (!root.is_object()) {
        error = "Graph JSON root must be an object";
        return false;
    }
    out = {};
    out.path = graph_path;
    auto stem = std::filesystem::path(graph_path).stem().string();
    out.id = stem;
    out.title = stem;
    out.difficulty = "intermediate";
    out.featured_rank = "1000";
    if (root.contains("meta") && root["meta"].is_object()) {
        const auto& meta = root["meta"];
        if (meta.contains("id") && meta["id"].is_string()) out.id = meta["id"].get<std::string>();
        if (meta.contains("title") && meta["title"].is_string()) out.title = meta["title"].get<std::string>();
        if (meta.contains("description") && meta["description"].is_string()) out.description = meta["description"].get<std::string>();
        if (meta.contains("difficulty") && meta["difficulty"].is_string()) out.difficulty = meta["difficulty"].get<std::string>();
        if (meta.contains("featured_rank") && meta["featured_rank"].is_number_integer())
            out.featured_rank = std::to_string(meta["featured_rank"].get<int>());
        out.tags_csv = join_csv(json_str_array(meta.value("tags", nlohmann::json())));
        out.envs_csv = join_csv(json_str_array(meta.value("envs", nlohmann::json())));
        out.requires_packages_csv = join_csv(json_str_array(meta.value("requires_packages", nlohmann::json())));
    }
    return true;
}

static bool save_graph_meta_edit_data(const vivid::ui::GraphMetaEditData& in, std::string& error) {
    nlohmann::json root;
    try {
        std::ifstream ifs(in.path);
        if (!ifs) {
            error = "Failed to read graph JSON for save";
            return false;
        }
        root = nlohmann::json::parse(ifs);
    } catch (const std::exception&) {
        error = "Failed to read graph JSON for save";
        return false;
    }
    if (!root.is_object()) {
        error = "Graph JSON root must be an object";
        return false;
    }

    if (!root.contains("meta") || !root["meta"].is_object()) {
        root["meta"] = nlohmann::json::object();
    }
    auto& meta = root["meta"];

    meta["id"] = trim_copy(in.id);
    meta["title"] = trim_copy(in.title);
    meta["description"] = trim_copy(in.description);
    meta["difficulty"] = trim_copy(in.difficulty);
    meta["tags"] = split_csv(in.tags_csv);
    meta["envs"] = split_csv(in.envs_csv);
    meta["requires_packages"] = split_csv(in.requires_packages_csv);
    int rank = 1000;
    try {
        if (!trim_copy(in.featured_rank).empty()) rank = std::stoi(trim_copy(in.featured_rank));
    } catch (...) {}
    meta["featured_rank"] = rank;

    try {
        std::ofstream ofs(in.path);
        if (!ofs) {
            error = "Failed to write graph JSON";
            return false;
        }
        ofs << root.dump(4) << '\n';
        return true;
    } catch (const std::exception&) {
        error = "Failed to write graph JSON";
        return false;
    }
}

static std::vector<vivid::ui::ExampleEntry>
discover_examples_recursive(const std::filesystem::path& graphs_root) {
    std::vector<vivid::ui::ExampleEntry> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(graphs_root, ec)) return out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(graphs_root, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".json") continue;
        vivid::ui::ExampleEntry item;
        if (load_example_entry_from_graph(e.path(), graphs_root, item))
            out.push_back(std::move(item));
    }
    std::sort(out.begin(), out.end(), [](const vivid::ui::ExampleEntry& a,
                                         const vivid::ui::ExampleEntry& b) {
        if (a.featured_rank != b.featured_rank) return a.featured_rank < b.featured_rank;
        return a.title < b.title;
    });
    return out;
}

static std::vector<vivid::ui::ExampleEntry>
discover_examples_with_packages(const std::filesystem::path& graphs_root,
                                vivid::PackageManager* pkg_manager) {
    std::vector<vivid::ui::ExampleEntry> out = discover_examples_recursive(graphs_root);
    if (!pkg_manager) return out;

    std::unordered_set<std::string> seen_paths;
    for (const auto& e : out) seen_paths.insert(e.path);

    for (const auto& pkg : pkg_manager->list()) {
        if (pkg.path.empty()) continue;
        std::error_code ec;
        std::filesystem::path pkg_graphs_root = std::filesystem::path(pkg.path) / "graphs";
        if (!std::filesystem::is_directory(pkg_graphs_root, ec)) continue;

        auto pkg_examples = discover_examples_recursive(pkg_graphs_root);
        for (auto& e : pkg_examples) {
            std::filesystem::path abs_path = pkg_graphs_root / e.path;
            std::string open_path = abs_path.lexically_normal().string();
            if (!seen_paths.insert(open_path).second) continue;
            e.path = open_path;

            if (e.requires_packages.empty()) {
                e.requires_packages.push_back(pkg.name);
            }
            out.push_back(std::move(e));
        }
    }

    std::sort(out.begin(), out.end(), [](const vivid::ui::ExampleEntry& a,
                                         const vivid::ui::ExampleEntry& b) {
        if (a.featured_rank != b.featured_rank) return a.featured_rank < b.featured_rank;
        return a.title < b.title;
    });
    return out;
}

static std::string resolve_graph_input_path(const std::string& input,
                                            const std::filesystem::path& graphs_root,
                                            const std::vector<vivid::ui::ExampleEntry>& examples) {
    if (input.empty()) return input;
    std::filesystem::path p(input);
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return p.string();
    if (p.is_relative()) {
        std::filesystem::path in_graphs = graphs_root / p;
        if (std::filesystem::exists(in_graphs, ec)) return in_graphs.string();
    }
    const std::string filename = p.filename().string();
    for (const auto& e : examples) {
        if (e.id == input || e.id == filename ||
            std::filesystem::path(e.path).filename().string() == filename) {
            std::filesystem::path candidate = graphs_root / e.path;
            if (std::filesystem::exists(candidate, ec)) return candidate.string();
        }
    }
    return input;
}

static std::filesystem::path expand_tilde_path(const std::string& input) {
    if (input.empty()) return {};
    if (input[0] != '~') return std::filesystem::path(input);
    const char* home = std::getenv("HOME");
    if (!home) return std::filesystem::path(input);
    if (input.size() == 1) return std::filesystem::path(home);
    if (input[1] == '/' || input[1] == '\\')
        return std::filesystem::path(home) / input.substr(2);
    return std::filesystem::path(input);
}

static void refresh_window_title(GLFWwindow* window, const std::string& graph_path) {
    if (!window) return;
    std::string title = "Vivid";
    if (!graph_path.empty()) {
        std::string file = std::filesystem::path(graph_path).filename().string();
        if (!file.empty()) {
            title += " - ";
            title += file;
        }
    } else {
        title += " - Unsaved Document";
    }
    glfwSetWindowTitle(window, title.c_str());
}

static std::filesystem::path default_workspace_root() {
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0')
        return std::filesystem::path(home) / "Documents" / "Vivid";
    return std::filesystem::path(vivid::get_config_dir()) / "workspace";
}

struct ScaffoldDestination {
    std::string root;
    bool package_layout = false;
    std::string package_name;
    std::string warning;
};

static bool resolve_scaffold_destination(const std::string& destination,
                                         const std::string& source_dir,
                                         vivid::PackageManager& pm,
                                         const vivid::Settings* settings,
                                         ScaffoldDestination& out,
                                         std::string& error) {
    vivid::OperatorDestination resolved;
    if (!vivid::resolve_operator_destination(destination, source_dir, pm.list(), settings,
                                             resolved, error)) {
        return false;
    }
    out.root = resolved.root;
    out.package_layout = resolved.package_layout;
    out.package_name = resolved.package_name;
    out.warning = resolved.warning;
    return true;
}

static bool copy_tree_missing(const std::filesystem::path& src,
                              const std::filesystem::path& dst) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(src, ec)) return false;
    fs::create_directories(dst, ec);
    if (ec) return false;

    for (const auto& entry : fs::recursive_directory_iterator(src, ec)) {
        if (ec) return false;
        auto rel = fs::relative(entry.path(), src, ec);
        if (ec) return false;
        auto out = dst / rel;
        if (entry.is_directory()) {
            fs::create_directories(out, ec);
            if (ec) return false;
            continue;
        }
        if (!entry.is_regular_file()) continue;
        if (fs::exists(out, ec)) continue;  // non-destructive: never overwrite user files
        fs::create_directories(out.parent_path(), ec);
        if (ec) return false;
        fs::copy_file(entry.path(), out, fs::copy_options::none, ec);
        if (ec) return false;
    }
    return true;
}

// Copies src → dst, overwriting files where src is strictly newer.
// Used to propagate bundled asset updates into the workspace without
// clobbering files that the user has modified more recently.
static bool copy_tree_overwrite_newer(const std::filesystem::path& src,
                                      const std::filesystem::path& dst) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(src, ec)) return false;
    fs::create_directories(dst, ec);
    if (ec) return false;

    for (const auto& entry : fs::recursive_directory_iterator(src, ec)) {
        if (ec) return false;
        auto rel = fs::relative(entry.path(), src, ec);
        if (ec) return false;
        auto out = dst / rel;
        if (entry.is_directory()) {
            fs::create_directories(out, ec);
            if (ec) return false;
            continue;
        }
        if (!entry.is_regular_file()) continue;
        fs::create_directories(out.parent_path(), ec);
        if (ec) return false;
        if (fs::exists(out, ec)) {
            auto src_time = fs::last_write_time(entry.path(), ec);
            if (ec) continue;
            auto dst_time = fs::last_write_time(out, ec);
            if (ec) {
                // dst exists but can't read time — skip to be safe
                continue;
            }
            if (src_time <= dst_time) continue;  // dst is same age or newer — keep it
        }
        fs::copy_file(entry.path(), out, fs::copy_options::overwrite_existing, ec);
        // Non-fatal: log but continue if one file fails
        if (ec) {
            std::fprintf(stderr, "[vivid] Workspace sync warning: failed to update %s\n",
                         out.string().c_str());
            ec.clear();
        }
    }
    return true;
}

static bool ensure_workspace_seeded(const std::filesystem::path& resources_dir,
                                    vivid::Settings& settings,
                                    std::filesystem::path& workspace_root) {
    namespace fs = std::filesystem;
    bool settings_changed = false;

    if (settings.workspace_root.empty()) {
        settings.workspace_root = default_workspace_root().string();
        settings_changed = true;
    }
    workspace_root = expand_tilde_path(settings.workspace_root);
    if (workspace_root.empty()) {
        workspace_root = default_workspace_root();
    }
    std::string normalized_root = workspace_root.lexically_normal().string();
    if (normalized_root != settings.workspace_root) {
        settings.workspace_root = normalized_root;
        settings_changed = true;
    }

    fs::path src_graphs = resources_dir / "graphs";
    fs::path src_assets = resources_dir / "assets";
    fs::path dst_graphs = workspace_root / "graphs";
    fs::path dst_assets = workspace_root / "assets";

    std::error_code ec;
    bool needs_seed =
        settings.workspace_seeded_version != VIVID_CORE_VERSION ||
        !fs::is_directory(dst_graphs, ec) ||
        !fs::is_directory(dst_assets, ec);

    if (!needs_seed) return settings_changed;

    bool seed_ok = true;
    if (fs::is_directory(src_graphs, ec)) {
        if (!copy_tree_missing(src_graphs, dst_graphs)) {
            std::fprintf(stderr, "[vivid] Workspace seed warning: failed to copy graphs to %s\n",
                         dst_graphs.string().c_str());
            seed_ok = false;
        }
    } else {
        std::fprintf(stderr, "[vivid] Workspace seed warning: missing bundled graphs at %s\n",
                     src_graphs.string().c_str());
        seed_ok = false;
    }
    if (fs::is_directory(src_assets, ec)) {
        if (!copy_tree_missing(src_assets, dst_assets)) {
            std::fprintf(stderr, "[vivid] Workspace seed warning: failed to copy assets to %s\n",
                         dst_assets.string().c_str());
            seed_ok = false;
        }
    } else {
        std::fprintf(stderr, "[vivid] Workspace seed warning: missing bundled assets at %s\n",
                     src_assets.string().c_str());
        seed_ok = false;
    }

    if (seed_ok) {
        settings.workspace_seeded_version = VIVID_CORE_VERSION;
        settings_changed = true;
    }

    // Always sync bundle → workspace for files where the bundle copy is newer.
    // This propagates graph/asset updates from fresh builds without clobbering
    // files the user has modified more recently.
    if (fs::is_directory(src_graphs, ec))
        copy_tree_overwrite_newer(src_graphs, dst_graphs);
    if (fs::is_directory(src_assets, ec))
        copy_tree_overwrite_newer(src_assets, dst_assets);

    return settings_changed;
}

static std::atomic<uint64_t> g_monitor_topology_serial{0};

static void monitor_callback(GLFWmonitor* /*monitor*/, int event) {
    const char* ev = (event == GLFW_CONNECTED) ? "connected" :
                     (event == GLFW_DISCONNECTED) ? "disconnected" : "unknown";
    const uint64_t serial = g_monitor_topology_serial.fetch_add(1, std::memory_order_relaxed) + 1;
    std::fprintf(stderr, "[vivid] Monitor topology changed: %s (serial=%llu)\n",
                 ev, static_cast<unsigned long long>(serial));
}

static bool monitor_connected(GLFWmonitor* monitor) {
    if (!monitor) return false;
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i) {
        if (monitors[i] == monitor) return true;
    }
    return false;
}

static GLFWmonitor* monitor_for_window(GLFWwindow* window) {
    if (!window) return glfwGetPrimaryMonitor();
    int wx = 0, wy = 0, ww = 0, wh = 0;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);

    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* best = glfwGetPrimaryMonitor();
    long best_overlap = -1;
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0, mw = 0, mh = 0;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        int ix = std::max(wx, mx);
        int iy = std::max(wy, my);
        int ax = std::min(wx + ww, mx + mw);
        int ay = std::min(wy + wh, my + mh);
        long overlap = 0;
        if (ax > ix && ay > iy)
            overlap = static_cast<long>(ax - ix) * static_cast<long>(ay - iy);
        if (overlap > best_overlap) {
            best_overlap = overlap;
            best = monitors[i];
        }
    }
    return best;
}

static GLFWmonitor* monitor_for_target(int target, GLFWwindow* window) {
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    if (target == 1) return primary;
    if (target == 2) {
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) {
            if (monitors[i] != primary) return monitors[i];
        }
        return primary;
    }
    return monitor_for_window(window);
}

static void clamp_window_rect_to_monitor(GLFWmonitor* monitor, int* x, int* y, int* w, int* h) {
    if (!x || !y || !w || !h) return;
    if (!monitor) monitor = glfwGetPrimaryMonitor();
    if (!monitor) return;
    int mx = 0, my = 0, mw = 0, mh = 0;
    glfwGetMonitorWorkarea(monitor, &mx, &my, &mw, &mh);
    *w = std::max(640, std::min(*w, mw));
    *h = std::max(480, std::min(*h, mh));
    *x = std::max(mx, std::min(*x, mx + mw - *w));
    *y = std::max(my, std::min(*y, my + mh - *h));
}


// ---------------------------------------------------------------------------
// build_graph_snapshot — produces a GraphSnapshot from runtime state
// ---------------------------------------------------------------------------
static vivid::ui::GraphSnapshot build_graph_snapshot(
        const vivid::Graph& graph,
        const vivid::RuntimeCore& runtime,
        vivid::AudioEngine* audio_engine,
        vivid::OperatorRegistry& registry,
        OperatorInfoCache& op_cache,
        vivid::SystemMidiListener* system_midi = nullptr,
        const vivid::RuntimeAPI* runtime_api = nullptr,
        vivid::CaptureCoordinator* capture_coordinator = nullptr,
        const vivid::ControlServer* control_server = nullptr) {
    vivid::ui::GraphSnapshot snap;

    const auto* cg = runtime.compiled_graph();
    if (!cg) return snap;
    const auto& compiled_nodes = cg->nodes;
    const auto& conns = graph.connections();

    // Nodes
    snap.nodes.resize(compiled_nodes.size());
    for (size_t i = 0; i < compiled_nodes.size(); ++i) {
        const auto& cn = compiled_nodes[i];
        auto& sn = snap.nodes[i];
        sn.node_id = cn.node_id;
        sn.type_name = runtime.type_name(static_cast<uint32_t>(i));
        sn.active_cadence = cn.active_cadence;
        sn.is_gpu = cn.is_gpu();
        sn.is_audio_capable = (cn.cadence_capability == VIVID_CADENCE_AUDIO_CAPABLE);
        {
            const auto* ndef = graph.find_node(cn.node_id);
            sn.cadence_override = ndef ? ndef->cadence_override : vivid::CadenceOverride::Auto;
        }
        sn.is_gpu_sink = cn.is_gpu_sink();
        sn.is_generator = cn.gpu ? cn.gpu->texture_input_port_indices.empty() && !cn.is_gpu_sink() : true;
        sn.input_port_indices = cn.input_port_indices;
        sn.output_port_indices = cn.output_port_indices;
        sn.analysis_output_port_indices = cn.audio ? cn.audio->analysis_output_port_indices
                                                   : std::unordered_map<std::string, uint32_t>{};
        sn.param_indices = cn.param_indices;
        sn.param_values = cn.param_values;
        sn.param_lock_flags = cn.param_lock_flags;
        sn.output_values = cn.output_values;
        sn.output_spreads = cn.output_spreads;
        sn.output_string_values = cn.output_string_values;
        sn.output_string_spreads = cn.output_string_spreads;
        for (const auto& [name, idx] : cn.file_param_indices)
            sn.file_param_values[name] = cn.file_param_storage[idx];
        sn.gpu_tex_width = cn.gpu ? cn.gpu->tex_width : 0;
        sn.gpu_tex_height = cn.gpu ? cn.gpu->tex_height : 0;
        sn.gpu_tex_inherited = cn.gpu ? cn.gpu->tex_inherited : false;
        sn.errored       = cn.errored || (cn.gpu && cn.gpu->shader_error);
        sn.error_message = cn.errored                        ? cn.error_message
                         : (cn.gpu && cn.gpu->shader_error) ? cn.gpu->shader_error_msg
                         : cn.error_message;   // compile/build error (node still running)
        sn.missing_operator = cn.missing_operator;
        if (cn.missing_operator) {
            // Try to find the specific package with an ABI mismatch for this operator
            std::string pkg_name;
            for (const auto& d : registry.abi_mismatch_diagnostics()) {
                if (d.plugin_name == sn.type_name) { pkg_name = d.package_name; break; }
            }
            if (!pkg_name.empty()) {
                sn.error_message = "\"" + sn.type_name + "\" failed to load (ABI mismatch).\n"
                    "Package '" + pkg_name + "' may need rebuild.\n"
                    "Run: vivid rebuild " + pkg_name;
            } else if (registry.has_abi_mismatch_diagnostics()) {
                sn.error_message = "Operator \"" + sn.type_name + "\" not found.\n"
                    "ABI mismatch detected \xe2\x80\x94 plugins were built against a different Vivid version.\n"
                    "Run 'vivid rebuild <package>' to recompile, then reload.";
            } else {
                sn.error_message = "Operator \"" + sn.type_name + "\" not found.\n"
                    "The package providing this operator may not be installed or linked.\n"
                    "Install/link the package, then reload the graph.";
            }
        }

        // Solo state
        if (runtime.is_solo_active()) {
            const auto& solo_set = runtime.solo_active_set();
            std::string solo_id = (runtime.solo_node_idx() >= 0 &&
                                   runtime.solo_node_idx() < static_cast<int>(compiled_nodes.size()))
                                  ? compiled_nodes[runtime.solo_node_idx()].node_id : "";
            sn.soloed = (cn.node_id == solo_id);
            sn.solo_dimmed = (i < solo_set.size() && !solo_set[i]);
        }

        // Layout from graph
        const auto* ndef = graph.find_node(cn.node_id);
        if (ndef && ndef->has_layout()) {
            sn.layout_x = ndef->layout_x;
            sn.layout_y = ndef->layout_y;
            sn.has_layout = true;
        }

        // Operator info (cached; pass per-instance loader as fallback for WGSLFilter nodes)
        sn.op_info = op_cache.get(sn.type_name, registry, cn.loader);

        // Per-operator presets
        sn.preset_names = graph.list_presets(cn.node_id);
        sn.factory_preset_names = registry.factory_preset_names(sn.type_name);
        if (runtime_api)
            sn.active_preset = runtime_api->active_preset(cn.node_id);

        // State-preset mappings (for StateMachine nodes)
        const auto* spm = graph.find_state_mapping(cn.node_id);
        if (spm)
            sn.state_preset_map = spm->state_presets;

        // Index
        snap.node_index[cn.node_id] = i;
    }

    // Connections — preserve graph truth even when an endpoint no longer resolves.
    snap.connections.reserve(conns.size());
    for (size_t i = 0; i < conns.size(); ++i) {
        bool from_is_param = false;
        bool to_is_param   = false;
        bool invalid = false;
        bool from_endpoint_missing = false;
        bool to_endpoint_missing = false;
        std::string invalid_reason;

        // Determine if source is a param (not an output port)
        auto ni_it = snap.node_index.find(conns[i].from_node);
        if (ni_it != snap.node_index.end()) {
            const auto& src = snap.nodes[ni_it->second];
            if (src.output_port_indices.count(conns[i].from_port) == 0 &&
                src.analysis_output_port_indices.count(conns[i].from_port) == 0) {
                if (src.param_indices.count(conns[i].from_port) == 0) {
                    invalid = true;
                    from_endpoint_missing = true;
                    invalid_reason = "missing source endpoint";
                } else {
                    from_is_param = true;
                    {
                        static std::unordered_set<std::string> warned;
                        std::string key = conns[i].from_node + "/" + conns[i].from_port
                                        + "->" + conns[i].to_node + "/" + conns[i].to_port + ":src";
                        if (warned.insert(key).second)
                            std::fprintf(stderr, "[vivid] snapshot: %s/%s -> %s/%s: "
                                "source '%s' not in output ports, resolved as param\n",
                                conns[i].from_node.c_str(), conns[i].from_port.c_str(),
                                conns[i].to_node.c_str(), conns[i].to_port.c_str(),
                                conns[i].from_port.c_str());
                    }
                }
            }
        } else {
            invalid = true;
            from_endpoint_missing = true;
            invalid_reason = "missing source node";
        }

        // Determine if destination is a param (not an input port)
        auto dest_it = snap.node_index.find(conns[i].to_node);
        if (dest_it != snap.node_index.end()) {
            const auto& dest = snap.nodes[dest_it->second];
            if (dest.input_port_indices.count(conns[i].to_port) == 0) {
                if (dest.param_indices.count(conns[i].to_port) == 0) {
                    invalid = true;
                    to_endpoint_missing = true;
                    if (invalid_reason.empty())
                        invalid_reason = "missing destination endpoint";
                    else
                        invalid_reason += "; missing destination endpoint";
                } else {
                    to_is_param = true;
                    {
                        static std::unordered_set<std::string> warned;
                        std::string key = conns[i].from_node + "/" + conns[i].from_port
                                        + "->" + conns[i].to_node + "/" + conns[i].to_port + ":dst";
                        if (warned.insert(key).second)
                            std::fprintf(stderr, "[vivid] snapshot: %s/%s -> %s/%s: "
                                "destination '%s' not in input ports, resolved as param\n",
                                conns[i].from_node.c_str(), conns[i].from_port.c_str(),
                                conns[i].to_node.c_str(), conns[i].to_port.c_str(),
                                conns[i].to_port.c_str());
                    }
                }
            }
        } else {
            invalid = true;
            to_endpoint_missing = true;
            if (invalid_reason.empty())
                invalid_reason = "missing destination node";
            else
                invalid_reason += "; missing destination node";
        }

        auto& c = snap.connections.emplace_back();
        c.from_node    = conns[i].from_node;
        c.from_port    = conns[i].from_port;
        c.to_node      = conns[i].to_node;
        c.to_port      = conns[i].to_port;
        c.from_min     = conns[i].from_min;
        c.from_max     = conns[i].from_max;
        c.to_min       = conns[i].to_min;
        c.to_max       = conns[i].to_max;
        c.clamp        = conns[i].clamp;
        c.from_is_param = from_is_param;
        c.to_is_param   = to_is_param;
        c.invalid = invalid;
        c.from_endpoint_missing = from_endpoint_missing;
        c.to_endpoint_missing = to_endpoint_missing;
        c.invalid_reason = invalid_reason;
    }

    // Audio analysis
    if (audio_engine) {
        const auto& analysis = audio_engine->analysis_read();
        for (const auto& ns : compiled_nodes) {
            int ae_idx = audio_engine->audio_node_index(ns.node_id);
            if (ae_idx >= 0) {
                snap.audio_index[ns.node_id] = ae_idx;
            }
        }
        snap.audio_analysis.resize(analysis.waveform.size());
        for (size_t i = 0; i < analysis.waveform.size(); ++i) {
            snap.audio_analysis[i].peak = (i < analysis.peak.size()) ? analysis.peak[i] : 0.0f;
            snap.audio_analysis[i].waveform = analysis.waveform[i];
        }

        snap.audio_underrun_count = audio_engine->underrun_count();
        snap.audio_underrun_active = audio_engine->last_buffer_underrun();
        snap.audio_load = audio_engine->audio_load();
        snap.audio_sample_rate = vivid::AudioEngine::kSampleRate;
        snap.audio_buffer_size = vivid::AudioEngine::kBufferSize;
        snap.audio_node_count = audio_engine->node_count();
    }

    // Operator catalog
    snap.operator_types = registry.type_names();
    std::sort(snap.operator_types.begin(), snap.operator_types.end());
    for (const auto& tn : snap.operator_types) {
        auto info = op_cache.get(tn, registry);
        if (info) snap.operator_catalog[tn] = info;
    }

    // WGSL preset names (for filter selector UI)
    snap.wgsl_preset_names = registry.wgsl_preset_names();

    // MIDI mappings
    const auto& mappings = graph.midi_mappings();
    snap.midi_mappings.resize(mappings.size());
    for (size_t i = 0; i < mappings.size(); ++i) {
        auto& sm = snap.midi_mappings[i];
        const auto& gm = mappings[i];
        sm.node_id = gm.node_id;
        sm.param_name = gm.param_name;
        sm.cc_number = gm.cc_number;
        sm.channel = gm.channel;
        sm.range_min = gm.range_min;
        sm.range_max = gm.range_max;
        snap.midi_mapping_index[gm.node_id + "\t" + gm.param_name] = i;
    }

    // Pending CC events from system MIDI listener
    if (system_midi) {
        const auto& events = system_midi->last_drained_events();
        snap.pending_cc_events.resize(events.size());
        for (size_t i = 0; i < events.size(); ++i) {
            snap.pending_cc_events[i] = {events[i].channel, events[i].cc_number, events[i].value};
        }
    }

    // Variations
    const auto& vars = graph.variations();
    snap.variations.resize(vars.size());
    for (size_t i = 0; i < vars.size(); ++i) {
        snap.variations[i].name = vars[i].name;
    }
    snap.active_variation = graph.active_variation();
    snap.quantize_clock_node = graph.quantize_clock_node();

    // Sticky notes
    const auto& sticky = graph.sticky_notes();
    snap.sticky_notes.resize(sticky.size());
    for (size_t i = 0; i < sticky.size(); ++i) {
        auto& ss = snap.sticky_notes[i];
        const auto& gs = sticky[i];
        ss.id = gs.id;
        ss.text = gs.text;
        ss.x = gs.x;
        ss.y = gs.y;
        ss.width = gs.width;
        ss.height = gs.height;
        ss.color = gs.color;
    }
    if (runtime_api) {
        snap.variation_dirty = runtime_api->variation_dirty();
        snap.graph_dirty = runtime_api->graph_dirty();
        snap.queued_variation = runtime_api->pending_variation_idx();
    }

    // Solo state
    if (runtime.is_solo_active() && runtime_api)
        snap.solo_node_id = runtime_api->solo_node_id();

    // Recording state
    if (capture_coordinator) {
        snap.is_recording = capture_coordinator->is_recording();
        if (snap.is_recording) {
            snap.recording_frame_count = capture_coordinator->recording_frame_count();
            snap.recording_duration_sec = capture_coordinator->recording_duration_sec();
        }
    }

    // MCP server ping timestamps
    if (control_server) {
        snap.mcp_main_last_ping_ms  = control_server->mcp_last_ping_ms("vivid");
        snap.mcp_opdev_last_ping_ms = control_server->mcp_last_ping_ms("opdev");
    }

    return snap;
}

static void emit_clear_pass(WGPUCommandEncoder encoder, WGPUTextureView view, const double clear[4]) {
    WGPURenderPassColorAttachment color_att{};
    color_att.view = view;
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_att.resolveTarget = nullptr;
    color_att.loadOp = WGPULoadOp_Clear;
    color_att.storeOp = WGPUStoreOp_Store;
    color_att.clearValue = { clear[0], clear[1], clear[2], clear[3] };
    WGPURenderPassDescriptor rp_desc{};
    rp_desc.label = to_sv("Clear Pass");
    rp_desc.colorAttachmentCount = 1;
    rp_desc.colorAttachments = &color_att;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

static void poll_hot_reload(vivid::FileWatcher& fw, vivid::HotReloader& hr,
                            vivid::RuntimeCore& runtime, vivid::OperatorRegistry& registry,
                            vivid::AudioEngine& audio_engine, bool has_audio,
                            OperatorInfoCache* op_cache = nullptr,
                            const std::string& operators_dir = {}) {
    auto changes = fw.poll_changes();
    for (const auto& change : changes) {
        hr.queue_rebuild(change.target_name);
    }

    auto ready = hr.poll_ready();
    for (const auto& result : ready) {
        if (!result.success) {
            // Propagate compile errors to all nodes of this type so the UI can surface them.
            // Nodes keep running (old dylib still live); errored=true is NOT set.
            const std::string* type_name_ptr = registry.type_name_for_target(result.target_name);
            if (type_name_ptr) {
                const std::string& err = result.error_output.empty()
                    ? "Build failed (no output captured)" : result.error_output;
                if (auto* cg = runtime.compiled_graph()) {
                    for (auto& cn : cg->nodes) {
                        if (cn.type_name == *type_name_ptr)
                            cn.error_message = err;
                    }
                }
            }
            continue;
        }

        const std::string* type_name_ptr = registry.type_name_for_target(result.target_name);
        if (!type_name_ptr) {
            // New operator (just scaffolded) — load its dylib into the registry
            if (registry.register_loaded_operator(result.staged_dylib_path)) {
                // Register file watch for the new operator's source files
                if (!operators_dir.empty()) {
                    // Scan all env subdirs for the target directory
                    for (const char* domain : {"control", "audio", "gpu"}) {
                        std::string cpp_path = operators_dir + "/" + domain + "/" +
                                               result.target_name + "/" + result.target_name + ".cpp";
                        if (std::filesystem::exists(cpp_path)) {
                            fw.add_watch(cpp_path, result.target_name);
                            break;
                        }
                    }
                }
                std::fprintf(stderr, "[vivid] New operator '%s' loaded\n",
                    result.target_name.c_str());
            } else {
                std::fprintf(stderr, "[vivid] Hot-reload: failed to load new target '%s'\n",
                    result.target_name.c_str());
            }
            continue;
        }
        const std::string& tn = *type_name_ptr;

        std::fprintf(stderr, "[vivid] Hot-reload: reloading %s...\n", tn.c_str());

        bool is_audio_op = runtime.has_audio_cadence_type(tn);

        // Two-phase audio reload: destroy old instances BEFORE the dylib swap
        // so we can safely call the old dylib's destroy function.
        if (is_audio_op && has_audio) {
            audio_engine.pre_reload_operator(tn);
        }

        if (runtime.reload_operator(tn, registry, result.staged_dylib_path)) {
            bool audio_reload_ok = true;
            if (is_audio_op && has_audio) {
                audio_reload_ok = audio_engine.post_reload_operator(tn, registry);
            }
            if (!audio_reload_ok) {
                std::fprintf(stderr, "[vivid] Hot-reload: %s audio reload FAILED\n", tn.c_str());
            } else {
                if (auto* cg = runtime.compiled_graph()) {
                    for (auto& cn : cg->nodes) {
                        if (cn.type_name == tn)
                            cn.error_message.clear();
                    }
                }
                if (op_cache) op_cache->invalidate(tn);
                std::fprintf(stderr, "[vivid] Hot-reload: %s reloaded successfully\n", tn.c_str());
            }
        } else {
            std::fprintf(stderr, "[vivid] Hot-reload: %s reload FAILED\n", tn.c_str());
            // Recreate audio instances from the old (still-loaded) dylib
            if (is_audio_op && has_audio) {
                audio_engine.post_reload_operator(tn, registry);
            }
        }
    }
}

static int add_watch_for_resolved_package(vivid::FileWatcher& fw, const vivid::PackageInfo& pkg) {
    namespace fs = std::filesystem;
    int count = 0;
    fs::path ops_dir = fs::path(pkg.path) / "operators";
    if (fs::exists(ops_dir)) {
        std::error_code ec_domain;
        for (const auto& domain_entry : fs::directory_iterator(ops_dir, ec_domain)) {
            if (ec_domain) break;
            if (!domain_entry.is_directory()) continue;

            std::error_code ec_op;
            for (const auto& op_entry : fs::directory_iterator(domain_entry.path(), ec_op)) {
                if (ec_op) break;
                if (!op_entry.is_directory()) continue;

                std::string op_name = op_entry.path().filename().string();
                std::string target = "pkg:" + pkg.name + ":" + op_name;

                std::error_code ec_file;
                for (const auto& file_entry : fs::directory_iterator(op_entry.path(), ec_file)) {
                    if (ec_file) break;
                    if (!file_entry.is_regular_file()) continue;
                    std::string fname = file_entry.path().filename().string();
                    if (fname.size() < 5 || fname.substr(fname.size() - 4) != ".cpp") continue;
                    if (fw.add_watch(file_entry.path().string(), target)) count++;
                }
            }
        }
    }

    fs::path src_dir = fs::path(pkg.path) / "src";
    if (fs::exists(src_dir)) {
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(src_dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".cpp") continue;
            std::string op_name = entry.path().stem().string();
            std::string target = "pkg:" + pkg.name + ":" + op_name;
            if (fw.add_watch(entry.path().string(), target)) count++;
        }
    }
    return count;
}

static void draw_custom_thumbnails(const vivid::RuntimeCore& runtime,
                                   vivid::ui::ThumbnailCache& cache,
                                   vivid::ui::NodeGraphUI& graph_ui,
                                   WGPUDevice device,
                                   WGPUQueue queue,
                                   WGPUCommandEncoder encoder,
                                   double time,
                                   double delta_time,
                                   uint64_t frame,
                                   uint32_t thumb_w,
                                   uint32_t thumb_h,
                                   WGPUTextureFormat thumb_format) {
    const auto* cg_thumb = runtime.compiled_graph();
    if (!cg_thumb) return;
    for (const auto& cn : cg_thumb->nodes) {
        if (cn.missing_operator) {
            // Missing-operator placeholders are intentionally degraded UI state.
            // Do not issue custom thumbnail GPU work in that state; fall back to
            // default node bodies instead of risking stale/invalid thumbnail submit.
            graph_ui.set_custom_thumbnail_nodes({});
            return;
        }
    }

    std::unordered_set<std::string> custom_thumb_ids;
    for (const auto& cn : cg_thumb->nodes) {
        if (!cn.loader || !cn.instance || cn.missing_operator) continue;
        if (!cn.loader->has_draw_thumbnail()) continue;
        WGPUTextureView thumb_view = cache.get_or_create(cn.node_id);
        if (!thumb_view) continue;
        WGPUTexture thumb_tex = cache.get_texture(cn.node_id);

        VividThumbnailContext tctx{};
        tctx.time = time;
        tctx.delta_time = delta_time;
        tctx.frame = frame;
        tctx.param_values = cn.param_values.data();
        tctx.param_count = static_cast<uint32_t>(cn.param_values.size());
        tctx.output_values = cn.output_values.data();
        tctx.output_count = cn.output_port_count;
        tctx.string_param_values = cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data();
        tctx.string_param_count = static_cast<uint32_t>(cn.file_param_ptrs.size());
        tctx.file_param_values = cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data();
        tctx.file_param_count = static_cast<uint32_t>(cn.file_param_ptrs.size());
        tctx.device = device;
        tctx.queue = queue;
        tctx.command_encoder = encoder;
        tctx.thumbnail_texture = thumb_tex;
        tctx.thumbnail_texture_view = thumb_view;
        tctx.thumbnail_width = thumb_w;
        tctx.thumbnail_height = thumb_h;
        tctx.thumbnail_format = thumb_format;
        tctx.source_output_texture = cn.gpu ? cn.gpu->texture : nullptr;
        tctx.source_output_texture_view = cn.gpu ? cn.gpu->texture_view : nullptr;
        tctx.source_output_width = cn.gpu ? cn.gpu->tex_width : 0;
        tctx.source_output_height = cn.gpu ? cn.gpu->tex_height : 0;
        tctx.source_output_format = thumb_format;
        tctx.input_texture_views =
            (cn.gpu && !cn.gpu->resolved_tex_inputs.empty()) ? const_cast<WGPUTextureView*>(cn.gpu->resolved_tex_inputs.data())
                                                             : nullptr;
        tctx.input_texture_count = cn.gpu ? static_cast<uint32_t>(cn.gpu->resolved_tex_inputs.size()) : 0;
        tctx.operator_errored = 0;
        tctx.operator_error_msg = nullptr;

        cn.loader->draw_thumbnail(cn.instance, &tctx);
        if (tctx.operator_errored) {
            std::fprintf(stderr, "[vivid] thumbnail render error for '%s': %s\n",
                         cn.node_id.c_str(),
                         tctx.operator_error_msg ? tctx.operator_error_msg : "unknown error");
            continue;  // fall back to default thumbnail
        }
        custom_thumb_ids.insert(cn.node_id);
    }
    graph_ui.set_custom_thumbnail_nodes(std::move(custom_thumb_ids));
}

struct SurfaceCaptureResult {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> png_data;
};

static bool capture_surface_png(vivid::GpuContext& gpu,
                                vivid::FrameState& frame,
                                int fb_w, int fb_h,
                                SurfaceCaptureResult& out,
                                std::string& error) {
    if (!gpu.surface_supports_copy_src()) {
        error = "surface does not support interface capture";
        return false;
    }
    const uint32_t ss_w = static_cast<uint32_t>(fb_w);
    const uint32_t ss_h = static_cast<uint32_t>(fb_h);
    const uint32_t bpp = 4;
    const uint32_t unpadded_row = ss_w * bpp;
    static constexpr uint32_t kGpuRowAlignment = 256;
    const uint32_t aligned_row = (unpadded_row + kGpuRowAlignment - 1) & ~(kGpuRowAlignment - 1);
    const uint64_t buf_size = static_cast<uint64_t>(aligned_row) * ss_h;

    WGPUBufferDescriptor staging_desc{};
    staging_desc.label = to_sv("Screenshot Staging");
    staging_desc.size = buf_size;
    staging_desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    staging_desc.mappedAtCreation = false;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(gpu.device(), &staging_desc);
    if (!staging) {
        error = "failed to allocate screenshot staging buffer";
        return false;
    }

    WGPUTexelCopyTextureInfo src{};
    src.texture = frame.texture;
    src.mipLevel = 0;
    src.origin = { 0, 0, 0 };
    src.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = staging;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = aligned_row;
    dst.layout.rowsPerImage = ss_h;

    WGPUExtent3D copy_size = { ss_w, ss_h, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(frame.encoder, &src, &dst, &copy_size);

    gpu.end_frame(frame);

    // Wait for GPU work to complete
    {
        bool work_done = false;
        WGPUQueueWorkDoneCallbackInfo work_cb{};
        work_cb.mode = WGPUCallbackMode_AllowSpontaneous;
        work_cb.callback = [](WGPUQueueWorkDoneStatus, void* ud1, void*) {
            *static_cast<bool*>(ud1) = true;
        };
        work_cb.userdata1 = &work_done;
        wgpuQueueOnSubmittedWorkDone(gpu.queue(), work_cb);
        while (!work_done)
            wgpuDevicePoll(gpu.device(), true, nullptr);
    }

    bool map_done = false;
    WGPUBufferMapCallbackInfo map_cb{};
    map_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    map_cb.callback = [](WGPUMapAsyncStatus, WGPUStringView, void* ud1, void*) {
        *static_cast<bool*>(ud1) = true;
    };
    map_cb.userdata1 = &map_done;
    wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, buf_size, map_cb);
    while (!map_done)
        wgpuDevicePoll(gpu.device(), true, nullptr);

    const uint8_t* mapped = static_cast<const uint8_t*>(
        wgpuBufferGetConstMappedRange(staging, 0, buf_size));
    if (!mapped) {
        wgpuBufferUnmap(staging);
        wgpuBufferRelease(staging);
        error = "failed to map screenshot staging buffer";
        return false;
    }

    std::vector<uint8_t> pixels(ss_w * ss_h * bpp);
    for (uint32_t y = 0; y < ss_h; ++y) {
        const uint8_t* src_row = mapped + y * aligned_row;
        uint8_t* dst_row = pixels.data() + y * unpadded_row;
        for (uint32_t x = 0; x < ss_w; ++x) {
            dst_row[x * 4 + 0] = src_row[x * 4 + 2]; // R <- B
            dst_row[x * 4 + 1] = src_row[x * 4 + 1]; // G <- G
            dst_row[x * 4 + 2] = src_row[x * 4 + 0]; // B <- R
            dst_row[x * 4 + 3] = src_row[x * 4 + 3]; // A <- A
        }
    }

    wgpuBufferUnmap(staging);
    wgpuBufferRelease(staging);

    out.width = ss_w;
    out.height = ss_h;
    out.png_data.clear();
    out.png_data.reserve(ss_w * ss_h);
    stbi_write_png_to_func(stbi_write_to_vec, &out.png_data, ss_w, ss_h, 4,
                           pixels.data(), static_cast<int>(ss_w * bpp));
    if (out.png_data.empty()) {
        error = "PNG encoding failed";
        return false;
    }
    return true;
}

static bool try_capture_screenshot(const std::string& path, vivid::GpuContext& gpu,
                                   vivid::FrameState& frame, int fb_w, int fb_h,
                                   uint64_t frame_count, int delay, GLFWwindow* window) {
    if (path.empty() || static_cast<int>(frame_count) < delay) {
        return false;
    }

    SurfaceCaptureResult capture;
    std::string error;
    if (!capture_surface_png(gpu, frame, fb_w, fb_h, capture, error)) {
        std::fprintf(stderr, "[vivid] Screenshot FAILED: %s\n", error.c_str());
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return true;
    }

    std::ofstream out(path, std::ios::binary);
    if (out) {
        out.write(reinterpret_cast<const char*>(capture.png_data.data()),
                  static_cast<std::streamsize>(capture.png_data.size()));
    }

    if (out.good()) {
        std::fprintf(stderr, "[vivid] Screenshot saved: %s\n", path.c_str());
    } else {
        std::fprintf(stderr, "[vivid] Screenshot FAILED: %s\n", path.c_str());
    }

    glfwSetWindowShouldClose(window, GLFW_TRUE);
    return true;  // frame already submitted
}

// GLFW callback trampolines
struct WindowUserData {
    vivid::ui::NodeGraphUI* graph_ui = nullptr;
    vivid::RuntimeAPI* runtime_api = nullptr;
    vivid::Graph* graph = nullptr;
    std::string working_filters_dir;
    vivid::Settings* settings = nullptr;

    // Input forwarding to operators (when UI hidden)
    std::vector<VividInputEvent> pending_events;
    double raw_mouse_x = 0.0, raw_mouse_y = 0.0;  // window coords
    int buttons_held = 0;   // bitmask: bit 0=left, 1=right, 2=middle
    int current_mods = 0;

    // Drag-and-drop graph loading
    std::string pending_drop_path;
};

static void run_ui_test_script_frame(UITestScript& script,
                                     vivid::ui::NodeGraphUI& graph_ui,
                                     WindowUserData& window_user_data,
                                     std::string& screenshot_path,
                                     int& screenshot_delay,
                                     uint64_t frame_count) {
    if (script.actions.empty() || script.next_action >= script.actions.size())
        return;
    if (script.wait_frames_remaining > 0) {
        --script.wait_frames_remaining;
        return;
    }

    while (script.next_action < script.actions.size()) {
        const auto& action = script.actions[script.next_action++];
        switch (action.type) {
            case UITestActionType::Wait:
                script.wait_frames_remaining = std::max(0, action.frames);
                return;
            case UITestActionType::MouseMove:
                window_user_data.raw_mouse_x = action.x;
                window_user_data.raw_mouse_y = action.y;
                graph_ui.on_mouse_move(action.x, action.y);
                std::fprintf(stderr,
                             "[vivid] UI script mouse_move to (%.1f, %.1f) on frame %llu\n",
                             static_cast<double>(action.x),
                             static_cast<double>(action.y),
                             static_cast<unsigned long long>(frame_count));
                break;
            case UITestActionType::MouseButton:
                window_user_data.current_mods = action.mods;
                if (action.button >= 0 && action.button <= 2) {
                    if (action.mouse_action == GLFW_PRESS)
                        window_user_data.buttons_held |= (1 << action.button);
                    else if (action.mouse_action == GLFW_RELEASE)
                        window_user_data.buttons_held &= ~(1 << action.button);
                }
                graph_ui.on_mouse_button(action.button, action.mouse_action, action.mods);
                std::fprintf(stderr,
                             "[vivid] UI script mouse_button %d/%d on frame %llu\n",
                             action.button, action.mouse_action,
                             static_cast<unsigned long long>(frame_count));
                break;
            case UITestActionType::Key:
                window_user_data.current_mods = action.mods;
                graph_ui.on_key(action.key, action.key_action, action.mods);
                std::fprintf(stderr,
                             "[vivid] UI script key %d/%d mods=%d on frame %llu\n",
                             action.key, action.key_action, action.mods,
                             static_cast<unsigned long long>(frame_count));
                break;
            case UITestActionType::CharInput:
                graph_ui.on_char(action.codepoint);
                std::fprintf(stderr,
                             "[vivid] UI script char %u on frame %llu\n",
                             action.codepoint,
                             static_cast<unsigned long long>(frame_count));
                break;
            case UITestActionType::Screenshot: {
                std::filesystem::path shot_path(action.screenshot_path);
                if (!shot_path.is_absolute())
                    shot_path = script.source_dir / shot_path;
                screenshot_path = shot_path.string();
                screenshot_delay = static_cast<int>(frame_count) + action.screenshot_delay;
                std::fprintf(stderr,
                             "[vivid] UI script scheduled screenshot: %s at frame %d\n",
                             screenshot_path.c_str(), screenshot_delay);
                return;
            }
            case UITestActionType::Checkpoint:
                script.pending_checkpoint_labels.push_back(action.checkpoint_label);
                std::fprintf(stderr,
                             "[vivid] UI script checkpoint queued: %s on frame %llu\n",
                             action.checkpoint_label.c_str(),
                             static_cast<unsigned long long>(frame_count));
                break;
        }
    }
}

static void char_callback(GLFWwindow* w, unsigned int codepoint) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    if (ud->graph_ui && ud->graph_ui->visible()) {
        if (ud->graph_ui->wants_keyboard())
            ud->graph_ui->on_char(codepoint);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_CHAR;
        ev.codepoint = codepoint;
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ev.modifiers = ud->current_mods;
        ud->pending_events.push_back(ev);
    }
}

static void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;

    ud->current_mods = mods;

    // Tilde toggles graph UI visibility (intercept before any dispatch)
    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS && mods == 0) {
        if (ud->graph_ui) ud->graph_ui->toggle_visible();
        return;
    }

    // Cmd+S and Cmd+, are handled by the native macOS menu bar (macos_menu.mm).
    // On non-Apple platforms, fall through to the graph UI key handler.

    if (ud->graph_ui && ud->graph_ui->visible()) {
        ud->graph_ui->on_key(key, action, mods);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_KEY;
        ev.key = key;
        ev.scancode = scancode;
        ev.action = action;
        ev.modifiers = mods;
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ud->pending_events.push_back(ev);
    }
}

static void cursor_pos_callback(GLFWwindow* w, double xpos, double ypos) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    ud->raw_mouse_x = xpos;
    ud->raw_mouse_y = ypos;
    if (ud->graph_ui && ud->graph_ui->visible()) {
        ud->graph_ui->on_mouse_move(static_cast<float>(xpos), static_cast<float>(ypos));
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_MOUSE_MOVE;
        ev.mouse_x = static_cast<float>(xpos);  // will be normalized later
        ev.mouse_y = static_cast<float>(ypos);
        ev.modifiers = ud->current_mods;
        ev.button = -1;
        ud->pending_events.push_back(ev);
    }
}

static void mouse_button_callback(GLFWwindow* w, int button, int action, int mods) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    ud->current_mods = mods;
    // Track button state
    if (button >= 0 && button <= 2) {
        if (action == GLFW_PRESS)
            ud->buttons_held |= (1 << button);
        else if (action == GLFW_RELEASE)
            ud->buttons_held &= ~(1 << button);
    }
    if (ud->graph_ui && ud->graph_ui->visible()) {
        ud->graph_ui->on_mouse_button(button, action, mods);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_MOUSE_BUTTON;
        ev.button = button;
        ev.action = action;
        ev.modifiers = mods;
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ud->pending_events.push_back(ev);
    }
}

static void scroll_callback(GLFWwindow* w, double xoffset, double yoffset) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    if (ud->graph_ui && ud->graph_ui->visible()) {
        int mods = 0;
        if (glfwGetKey(w, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
            glfwGetKey(w, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS)
            mods |= GLFW_MOD_SUPER;
        ud->graph_ui->on_scroll(
            static_cast<float>(xoffset), static_cast<float>(yoffset), mods);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_MOUSE_SCROLL;
        ev.scroll_dx = static_cast<float>(xoffset);
        ev.scroll_dy = static_cast<float>(yoffset);
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ev.modifiers = ud->current_mods;
        ud->pending_events.push_back(ev);
    }
}

static void drop_callback(GLFWwindow* w, int count, const char** paths) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud || count < 1) return;
    ud->pending_drop_path = paths[0];
}

// --- Build/source directory discovery (shared by export, packages, hot-reload) ---
namespace fs = std::filesystem;

struct BuildPaths { std::string source_dir, build_dir; };

static BuildPaths discover_build_paths(const fs::path& exe_dir,
                                       const fs::path& resources_dir,
                                       const std::string& user_src_dir) {
    BuildPaths p;
    // Prefer compile-time build dir (set by CMake on Apple)
#ifdef VIVID_BUILD_DIR
    p.build_dir = VIVID_BUILD_DIR;
    if (!fs::is_directory(p.build_dir))
#endif
    {
#ifdef __APPLE__
        // In a bundle: exe_dir is Contents/MacOS/, build dir is 3 levels up
        p.build_dir = exe_dir.parent_path().parent_path().parent_path().string();
#else
        p.build_dir = exe_dir.string();
#endif
    }

    // Walk up from build_dir looking for source root
    auto c = fs::path(p.build_dir);
    for (int i = 0; i < 3; ++i) {
        if (fs::exists(c / "CMakeLists.txt") && fs::exists(c / "src" / "runtime")) {
            p.source_dir = c.string();
            break;
        }
        c = c.parent_path();
    }
    // Fallback: bundle SDK (Contents/Resources/sdk/)
#ifdef __APPLE__
    if (p.source_dir.empty()) {
        auto sdk_dir = resources_dir / "sdk";
        if (fs::is_directory(sdk_dir / "src" / "operator_api"))
            p.source_dir = sdk_dir.string();
    }
#endif
    if (p.source_dir.empty())
        p.source_dir = user_src_dir;
    return p;
}

int main(int argc, char* argv[]) {
    vivid::install_crash_handlers();

    // Derive exe directory so resource lookup works from any CWD
    auto exe_path = std::filesystem::canonical(std::filesystem::path(argv[0]));
    auto exe_dir = exe_path.parent_path();

    // Resources dir: Contents/Resources/ in a macOS bundle, else same as exe_dir
#ifdef __APPLE__
    auto resources_dir = exe_dir.parent_path() / "Resources";
    auto plugins_dir = exe_dir.parent_path() / "PlugIns";
#else
    auto resources_dir = exe_dir;
#endif

    // --- CLI argument parsing ---
    std::string graph_file;
    std::string screenshot_path;
    std::string screenshot_select_node;
    int screenshot_delay = 5;
    std::string test_drop_path;
    std::vector<int> test_drop_screen_pos;
    int test_drop_frame = 5;
    std::string test_ui_script_path;
    std::string test_dump_ui_state_path;
    bool headless = false;
    std::string src_dir;

    CLI::App app{"Vivid - Real-time audio-visual graph engine\n\n"
                 "Loads a JSON graph file and runs it in real-time.\n"
                 "Control server listens on http://127.0.0.1:9876 for live manipulation."};

    app.add_option("graph", graph_file, "Graph file to load")->type_name("FILE");
    app.add_option("--screenshot", screenshot_path, "Capture a screenshot to PNG and exit")->type_name("FILE");
    app.add_option("--screenshot-delay", screenshot_delay, "Frames to wait before capture (default: 5)");
    app.add_option("--select-node", screenshot_select_node,
                   "Select a node by id before drawing the inspector (useful with --screenshot)")
        ->type_name("NODE_ID");
    app.add_option("--test-drop-path", test_drop_path,
                   "Inject a synthetic drag-and-drop path after startup (testing seam)")
        ->type_name("FILE");
    app.add_option("--test-drop-screen-pos", test_drop_screen_pos,
                   "Synthetic drop location in window coordinates (x y)")
        ->type_name("X Y")
        ->expected(2);
    app.add_option("--test-drop-frame", test_drop_frame,
                   "Frame number at which to inject --test-drop-path (default: 5)");
    app.add_option("--test-ui-script", test_ui_script_path,
                   "Replay a scripted sequence of UI inputs after startup (testing seam)")
        ->type_name("FILE");
    app.add_option("--test-dump-ui-state", test_dump_ui_state_path,
                   "Write a machine-readable final UI/runtime state snapshot (testing seam)")
        ->type_name("FILE");
    app.add_flag("--headless", headless, "Run without displaying a window");
    app.add_option("--src-dir", src_dir, "Source directory for operator hot-reload")->type_name("PATH");

    // --- Export subcommand ---
    std::string export_graph_path;
    std::string export_output;
    std::string export_output_dir;
    bool export_headless = false;
    bool export_control_server = false;
    std::vector<std::string> export_extra_ops;

    auto* export_cmd = app.add_subcommand("export", "Export graph as a standalone binary");
    export_cmd->add_option("--graph", export_graph_path, "Graph file to export")
        ->required()->type_name("FILE");
    export_cmd->add_option("--output", export_output, "Output binary name")
        ->required()->type_name("NAME");
    export_cmd->add_option("--output-dir", export_output_dir, "Export build directory")->type_name("PATH");
    export_cmd->add_flag("--headless", export_headless, "Build headless (no window)");
    export_cmd->add_flag("--control-server", export_control_server, "Include HTTP control server");
    export_cmd->add_option("--extra-operators", export_extra_ops,
        "Additional operator types to include (comma-separated)")->delimiter(',');

    // --- Package management subcommands ---
    std::string install_url;
    std::string uninstall_name;

    auto* install_cmd = app.add_subcommand("install", "Install an operator package");
    install_cmd->add_option("url", install_url, "Git URL or local path")->required();

    auto* uninstall_cmd = app.add_subcommand("uninstall", "Uninstall an operator package");
    uninstall_cmd->add_option("name", uninstall_name, "Package name")->required();

    auto* list_pkg_cmd = app.add_subcommand("list-packages", "List installed operator packages");
    bool list_pkg_verbose = false;
    list_pkg_cmd->add_flag("--verbose", list_pkg_verbose,
                           "Show resolver diagnostics (scope/path/build metadata)");

    std::string link_path;
    auto* link_cmd = app.add_subcommand("link", "Link a local package for development");
    link_cmd->add_option("path", link_path, "Path to package directory")->required();

    std::string unlink_name;
    auto* unlink_cmd = app.add_subcommand("unlink", "Unlink a linked package");
    unlink_cmd->add_option("name", unlink_name, "Package name")->required();

    std::string rebuild_name;
    auto* rebuild_cmd = app.add_subcommand("rebuild", "Recompile operators for a package");
    rebuild_cmd->add_option("name", rebuild_name, "Package name")->required();

    std::string scaffold_pkg_name;
    std::string scaffold_pkg_template = "single";
    std::string scaffold_pkg_output_dir;
    std::string scaffold_pkg_template_root;
    bool scaffold_pkg_force = false;
    auto* scaffold_pkg_cmd = app.add_subcommand("scaffold-package",
        "Scaffold a package skeleton from template");
    scaffold_pkg_cmd->add_option("name", scaffold_pkg_name, "Package name")->required();
    scaffold_pkg_cmd->add_option("--template", scaffold_pkg_template,
                                 "Template variant: single|multi")
        ->check(CLI::IsMember({"single", "multi"}))
        ->default_val("single");
    scaffold_pkg_cmd->add_option("--output-dir", scaffold_pkg_output_dir,
                                 "Parent directory for generated package");
    scaffold_pkg_cmd->add_option("--template-root", scaffold_pkg_template_root,
                                 "Explicit template root (overrides auto-discovery)");
    scaffold_pkg_cmd->add_flag("--force", scaffold_pkg_force,
                               "Overwrite destination if it already exists");

    std::string scaffold_op_name;
    std::string scaffold_op_env = "control";
    std::string scaffold_op_variant;
    std::string scaffold_op_dest = "auto";
    auto* scaffold_op_cmd = app.add_subcommand("scaffold-operator",
        "Scaffold a starter operator source file");
    scaffold_op_cmd->add_option("name", scaffold_op_name, "Operator name (snake_case)")->required();
    scaffold_op_cmd->add_option("--env", scaffold_op_env,
                                "Execution environment: control|audio|gpu")
        ->check(CLI::IsMember({"control", "audio", "gpu"}))
        ->default_val("control");
    scaffold_op_cmd->add_option("--variant", scaffold_op_variant,
                                "Optional template variant (e.g. composite)");
    scaffold_op_cmd->add_option("--dest", scaffold_op_dest,
                                "Destination: auto|core|package:<name>|absolute path")
        ->default_val("auto");
    std::string update_core_version = VIVID_CORE_VERSION;
    bool update_include_all = false;
    auto* check_updates_cmd = app.add_subcommand("package-check-updates",
        "Check installed packages for available updates and compatibility");
    check_updates_cmd->add_option("--core-version", update_core_version,
                                  "Core version to evaluate against vivid_core constraints")
        ->default_val(VIVID_CORE_VERSION);
    check_updates_cmd->add_flag("--all", update_include_all,
                                "Include installed packages even when no update is available");

    bool check_core_force = false;
    auto* check_core_updates_cmd = app.add_subcommand("check-core-updates",
        "Check for available Vivid core application updates");
    check_core_updates_cmd->add_flag("--force", check_core_force,
                                     "Force immediate network refresh");

    app.require_subcommand(0, 1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    if (test_drop_frame < 0) test_drop_frame = 0;
    UITestScript test_ui_script;
    if (!test_ui_script_path.empty()) {
        std::string script_error;
        if (!load_ui_test_script(test_ui_script_path, test_ui_script, script_error)) {
            std::fprintf(stderr, "[vivid] Failed to load --test-ui-script %s: %s\n",
                         test_ui_script_path.c_str(), script_error.c_str());
            return 1;
        }
    }
    if (!test_dump_ui_state_path.empty()) {
        std::filesystem::path dump_path(test_dump_ui_state_path);
        if (!dump_path.is_absolute())
            dump_path = std::filesystem::absolute(dump_path);
        test_dump_ui_state_path = dump_path.string();
    }

    // Resolve build/source directories once (used by export, packages, hot-reload)
    auto build_paths = discover_build_paths(exe_dir, resources_dir, src_dir);
    vivid::Settings settings = vivid::load_settings();

    // --- Handle export subcommand (early exit, no GLFW) ---
    if (export_cmd->parsed()) {
        if (build_paths.source_dir.empty()) {
            std::fprintf(stderr, "[vivid] Cannot determine source directory. "
                         "Use --src-dir or run from a build directory.\n");
            return 1;
        }

        // Build registry to get type→target mappings
        vivid::OperatorRegistry registry;
#ifdef __APPLE__
        registry.scan_deferred(plugins_dir.string().c_str());
#else
        registry.scan_deferred(exe_dir.string().c_str());
#endif
        register_builtin_operators(registry);
        std::string filters_dir = (resources_dir / "filters").string();
        registry.scan_wgsl_presets(filters_dir);

        vivid::ExportOptions opts;
        opts.graph_path = export_graph_path;
        opts.output_name = export_output;
        opts.output_path = export_output;
        opts.output_dir = export_output_dir;
        opts.headless = export_headless;
        opts.control_server = export_control_server;
        opts.extra_operators = export_extra_ops;

        vivid::ExportPipeline pipeline(build_paths.source_dir, build_paths.build_dir);
        if (!pipeline.run(opts, registry)) {
            std::fprintf(stderr, "[vivid] Export failed\n");
            return 1;
        }
        return 0;
    }

    // --- Handle package management subcommands (early exit, no GLFW) ---
    if (install_cmd->parsed() || uninstall_cmd->parsed() || list_pkg_cmd->parsed() ||
        link_cmd->parsed() || unlink_cmd->parsed() || rebuild_cmd->parsed() ||
        check_updates_cmd->parsed() || check_core_updates_cmd->parsed() ||
        scaffold_pkg_cmd->parsed() || scaffold_op_cmd->parsed()) {
        if (scaffold_pkg_cmd->parsed()) {
            vivid::PackageScaffoldOptions opts;
            opts.name = scaffold_pkg_name;
            opts.variant = scaffold_pkg_template;
            opts.output_dir = scaffold_pkg_output_dir;
            opts.template_root = scaffold_pkg_template_root;
            opts.source_dir = build_paths.source_dir;
            opts.force = scaffold_pkg_force;

            auto result = vivid::PackageScaffolder::scaffold(opts);
            if (!result.success) {
                std::fprintf(stderr, "Scaffold failed: %s\n", result.error.c_str());
                return 1;
            }

            std::printf("Scaffolded package: %s\n", result.package_dir.c_str());
            std::printf("Template used: %s\n", result.template_dir.c_str());
            std::printf("Next steps:\n");
            std::printf("  ./build/vivid link %s\n", result.package_dir.c_str());
            std::printf("  ./build/vivid rebuild %s\n", opts.name.c_str());
            return 0;
        }

        vivid::OperatorRegistry registry;
#ifdef __APPLE__
        registry.scan_deferred(plugins_dir.string().c_str());
#else
        registry.scan_deferred(exe_dir.string().c_str());
#endif
        register_builtin_operators(registry);

        vivid::PackageCompiler compiler(build_paths.source_dir, build_paths.build_dir);
        vivid::PackageManager pm(compiler, registry);

        if (scaffold_op_cmd->parsed()) {
            std::string validation_error = vivid::OperatorCreator::validate_name(scaffold_op_name, registry);
            if (!validation_error.empty()) {
                std::fprintf(stderr, "Scaffold failed: %s\n", validation_error.c_str());
                return 1;
            }

            VividExecutionEnv env = VIVID_ENV_FRAME;
            if (scaffold_op_env == "audio")
                env = VIVID_ENV_AUDIO;
            else if (scaffold_op_env == "gpu")
                env = VIVID_ENV_GPU;

            ScaffoldDestination destination;
            std::string dest_error;
            if (!resolve_scaffold_destination(scaffold_op_dest, build_paths.source_dir, pm,
                                              &settings,
                                              destination, dest_error)) {
                std::fprintf(stderr, "Scaffold failed: %s\n", dest_error.c_str());
                return 1;
            }
            if (!destination.warning.empty())
                std::fprintf(stderr, "[vivid] %s\n", destination.warning.c_str());

            VividCreateOperatorRequest req;
            req.name = scaffold_op_name;
            req.env = env;
            req.variant = scaffold_op_variant;
            auto result = vivid::OperatorCreator::create(req, destination.root,
                                                         destination.package_layout);
            if (!result.success) {
                std::fprintf(stderr, "Scaffold failed: %s\n", result.error.c_str());
                return 1;
            }

            vivid::OperatorCreator::open_in_editor(result.cpp_path);
            std::printf("Scaffolded operator: %s\n", result.target_name.c_str());
            std::printf("Source file: %s\n", result.cpp_path.c_str());
            std::printf("Destination root: %s\n", destination.root.c_str());
            if (destination.package_layout) {
                if (!destination.package_name.empty())
                    std::printf("Destination package: %s\n", destination.package_name.c_str());
                std::printf("Next step: ./build/vivid rebuild %s\n",
                            destination.package_name.empty() ? "<package-name>" : destination.package_name.c_str());
            } else {
                std::printf("Next step: cmake --build %s --target %s\n",
                            build_paths.build_dir.c_str(), result.target_name.c_str());
            }
            std::printf("Hint: Use MCP opdev tools for advanced features (custom ports, params, inspectors)\n");
            return 0;
        }

        if (install_cmd->parsed()) {
            auto result = pm.install(install_url);
            if (result.success) {
                std::fprintf(stderr, "Installed %s v%s (%zu operators)\n",
                             result.info.name.c_str(), result.info.version.c_str(),
                             result.info.operators.size() + result.info.gpu_operators.size());
                return 0;
            } else {
                std::fprintf(stderr, "Install failed: %s\n", result.error.c_str());
                for (const auto& cr : result.compile_results) {
                    if (!cr.success)
                        std::fprintf(stderr, "  %s: %s\n", cr.operator_name.c_str(),
                                     cr.error_output.c_str());
                }
                return 1;
            }
        } else if (uninstall_cmd->parsed()) {
            if (pm.uninstall(uninstall_name)) {
                std::fprintf(stderr, "Uninstalled %s\n", uninstall_name.c_str());
                return 0;
            } else {
                std::fprintf(stderr, "Failed to uninstall %s\n", uninstall_name.c_str());
                return 1;
            }
        } else if (list_pkg_cmd->parsed()) {
            auto packages = pm.list();
            if (packages.empty()) {
                std::printf("No packages installed.\n");
            } else {
                for (const auto& pkg : packages) {
                    std::printf("%s v%s  (%zu operators)%s\n",
                                pkg.name.c_str(), pkg.version.c_str(),
                                pkg.operators.size() + pkg.gpu_operators.size(),
                                pkg.linked ? "  [linked]" : "");
                    if (list_pkg_verbose) {
                        std::printf("  scope: %s\n", pkg.source_scope.empty() ? "unknown" : pkg.source_scope.c_str());
                        std::printf("  path: %s\n", pkg.path.c_str());
                        if (!pkg.build_type.empty())
                            std::printf("  build: %s\n", pkg.build_type.c_str());
                    }
                    if (!pkg.vivid_core.empty())
                        std::printf("  vivid_core: %s\n", pkg.vivid_core.c_str());
                    if (!pkg.description.empty())
                        std::printf("  %s\n", pkg.description.c_str());
                    for (const auto& op : pkg.operators)
                        std::printf("    %s\n", op.c_str());
                    for (const auto& op : pkg.gpu_operators)
                        std::printf("    %s (gpu)\n", op.c_str());
                }
            }
            std::printf("Tip: run `vivid package-check-updates` to check for package updates.\n");
            return 0;
        } else if (link_cmd->parsed()) {
            auto result = pm.link(link_path);
            if (result.success) {
                std::fprintf(stderr, "Linked %s v%s (%zu operators)\n",
                             result.info.name.c_str(), result.info.version.c_str(),
                             result.info.operators.size() + result.info.gpu_operators.size());
                return 0;
            } else {
                std::fprintf(stderr, "Link failed: %s\n", result.error.c_str());
                for (const auto& cr : result.compile_results) {
                    if (!cr.success)
                        std::fprintf(stderr, "  %s: %s\n", cr.operator_name.c_str(),
                                     cr.error_output.c_str());
                }
                return 1;
            }
        } else if (unlink_cmd->parsed()) {
            if (pm.unlink(unlink_name)) {
                std::fprintf(stderr, "Unlinked %s\n", unlink_name.c_str());
                return 0;
            } else {
                std::fprintf(stderr, "Failed to unlink %s\n", unlink_name.c_str());
                return 1;
            }
        } else if (rebuild_cmd->parsed()) {
            auto result = pm.rebuild(rebuild_name);
            if (result.success) {
                std::fprintf(stderr, "Rebuilt %s (%zu operators)\n",
                             result.info.name.c_str(),
                             result.info.operators.size() + result.info.gpu_operators.size());
                return 0;
            } else {
                std::fprintf(stderr, "Rebuild failed: %s\n", result.error.c_str());
                for (const auto& cr : result.compile_results) {
                    if (!cr.success)
                        std::fprintf(stderr, "  %s: %s\n", cr.operator_name.c_str(),
                                     cr.error_output.c_str());
                }
                return 1;
            }
        } else if (check_core_updates_cmd->parsed()) {
            vivid::AppUpdateManager updates(VIVID_CORE_VERSION);
            if (check_core_force || updates.fetch_state() == vivid::AppUpdateFetchState::Idle)
                updates.refresh();

            for (int i = 0; i < 200; ++i) {
                auto st = updates.fetch_state();
                if (st != vivid::AppUpdateFetchState::Fetching) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            auto st = updates.fetch_state();
            auto info = updates.latest();
            if (st == vivid::AppUpdateFetchState::Error) {
                std::fprintf(stderr, "Core update check failed: %s\n",
                             updates.fetch_error().c_str());
                return 1;
            }

            std::printf("Core version: %s\n", VIVID_CORE_VERSION);
            std::printf("Appcast: %s\n", vivid::AppUpdateManager::appcast_url().c_str());
            if (info.latest_version.empty()) {
                std::printf("No update metadata available.\n");
            } else if (info.update_available) {
                std::printf("Update available: %s -> %s\n",
                            info.current_version.c_str(),
                            info.latest_version.c_str());
                if (!info.title.empty())
                    std::printf("Title: %s\n", info.title.c_str());
                if (!info.download_url.empty())
                    std::printf("Download: %s\n", info.download_url.c_str());
                if (!info.release_notes_url.empty())
                    std::printf("Release notes: %s\n", info.release_notes_url.c_str());
            } else {
                std::printf("Up to date (%s).\n",
                            info.current_version.empty() ? VIVID_CORE_VERSION : info.current_version.c_str());
            }
            return 0;
        } else if (check_updates_cmd->parsed()) {
            vivid::PackageCatalog catalog(pm);
            catalog.refresh();

            for (int i = 0; i < 200; ++i) {
                auto st = catalog.fetch_state();
                if (st != vivid::CatalogFetchState::Fetching) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            auto entries = catalog.entries();
            auto state = catalog.fetch_state();
            if (entries.empty() && state == vivid::CatalogFetchState::Error) {
                std::fprintf(stderr, "Update check failed: %s\n", catalog.fetch_error().c_str());
                return 1;
            }

            auto class_str = [](vivid::PackageUpdateClass c) -> const char* {
                switch (c) {
                    case vivid::PackageUpdateClass::UpToDate: return "up_to_date";
                    case vivid::PackageUpdateClass::CompatibleUpdate: return "compatible_update";
                    case vivid::PackageUpdateClass::IncompatibleUpdate: return "incompatible_update";
                    case vivid::PackageUpdateClass::RemoteOlderOrEqual: return "remote_older_or_equal";
                    case vivid::PackageUpdateClass::InvalidVersionData: return "invalid_version_data";
                    default: return "unknown";
                }
            };

            int installed_count = 0;
            int updates_available = 0;
            int incompatible_updates = 0;
            for (const auto& e : entries) {
                if (!e.installed) continue;
                installed_count++;

                vivid::PackageInfo installed;
                installed.name = e.name;
                installed.version = e.installed_version;
                auto a = vivid::PackageManager::assess_update(
                    installed, e.version, e.vivid_core, update_core_version);

                if (!update_include_all && !a.update_available) continue;

                std::printf("%s: installed=%s remote=%s class=%s compatible=%s\n",
                            a.package_name.c_str(),
                            a.installed_version.c_str(),
                            a.remote_version.c_str(),
                            class_str(a.classification),
                            a.compatible ? "yes" : "no");
                if (!a.remote_vivid_core.empty())
                    std::printf("  vivid_core: %s\n", a.remote_vivid_core.c_str());
                if (!a.message.empty())
                    std::printf("  %s\n", a.message.c_str());

                if (a.update_available) updates_available++;
                if (a.classification == vivid::PackageUpdateClass::IncompatibleUpdate)
                    incompatible_updates++;
            }

            if (installed_count == 0) {
                std::printf("No installed packages found in catalog.\n");
            } else if (!update_include_all && updates_available == 0) {
                std::printf("No package updates available.\n");
            }

            std::printf("Summary: installed=%d updates_available=%d incompatible_updates=%d core_version=%s\n",
                        installed_count, updates_available, incompatible_updates,
                        update_core_version.c_str());
            return 0;
        }
    }

    // --- GLFW ---
    if (!glfwInit()) {
        std::fprintf(stderr, "[vivid] Failed to init GLFW\n");
        return 1;
    }
    glfwSetMonitorCallback(monitor_callback);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    if (headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    std::filesystem::path workspace_root;
    if (ensure_workspace_seeded(resources_dir, settings, workspace_root)) {
        vivid::save_settings(settings);
    }

    // Clamp saved window size to fit the primary monitor's work area
    {
        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        if (primary) {
            int mx, my, mw, mh;
            glfwGetMonitorWorkarea(primary, &mx, &my, &mw, &mh);
            if (settings.window_width > mw) settings.window_width = mw;
            if (settings.window_height > mh) settings.window_height = mh;
        }
    }

    GLFWwindow* window = glfwCreateWindow(settings.window_width, settings.window_height,
                                           "Vivid", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[vivid] Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    // Restore saved window position, validating it's on a visible monitor
    if (settings.window_x != -1 && settings.window_y != -1) {
        bool on_screen = false;
        int mon_count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&mon_count);
        for (int i = 0; i < mon_count; i++) {
            int mx, my, mw, mh;
            glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
            // Check that at least a 100x100 corner of the window is visible
            if (settings.window_x + 100 > mx && settings.window_x < mx + mw &&
                settings.window_y + 100 > my && settings.window_y < my + mh) {
                on_screen = true;
                break;
            }
        }
        if (on_screen) {
            glfwSetWindowPos(window, settings.window_x, settings.window_y);
        }
    }

    struct DisplayState {
        bool fullscreen = false;
        GLFWmonitor* fullscreen_monitor = nullptr;
        int windowed_x = 100;
        int windowed_y = 100;
        int windowed_w = 1280;
        int windowed_h = 800;
        uint64_t seen_monitor_serial = 0;
        int sink_target = -1;
        bool surface_reconfigure_pending = false;
        int surface_settle_frames = 0;
    } display_state;

    // --- Query physical framebuffer size and DPI scale ---
    int fb_width, fb_height;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float dpi_scale = xscale; // on macOS, xscale == yscale
    std::fprintf(stderr, "[vivid] Framebuffer: %dx%d, DPI scale: %.1f\n",
                 fb_width, fb_height, dpi_scale);

    // --- GPU ---
    vivid::GpuContext gpu;
    if (!gpu.init(window, fb_width, fb_height)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    const double* clear = is_srgb_format(gpu.surface_format()) ? kClearLinear : kClearRaw;

    // --- Offscreen texture format (used by per-node GPU textures) ---
    static constexpr WGPUTextureFormat kOffscreenFormat = WGPUTextureFormat_RGBA16Float;

    // --- Fullscreen blit (per-node texture → surface) ---
    vivid::FullscreenBlit blit;
    if (!blit.init(gpu.device(), gpu.surface_format())) {
        std::fprintf(stderr, "[vivid] Failed to init FullscreenBlit\n");
        gpu.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // --- Output window (separate display for video_out) ---
    vivid::OutputWindow output_window;

    // --- Thumbnail cache + renderer ---
    vivid::ui::ThumbnailCache thumb_cache;
    thumb_cache.init(gpu.device(), gpu.queue(), kThumbW, kThumbH);

    // Separate blit pipeline for offscreen→thumbnail (targets RGBA16Float, not surface format)
    vivid::FullscreenBlit thumb_blit;
    if (!thumb_blit.init(gpu.device(), kOffscreenFormat)) {
        std::fprintf(stderr, "[vivid] Failed to init thumbnail blit\n");
    }

    vivid::ui::ThumbnailRenderer thumb_renderer;
    bool thumb_renderer_ok = thumb_renderer.init(gpu.device(), gpu.queue(), gpu.surface_format());

    // --- Load operator plugins ---
    vivid::OperatorRegistry registry;
#ifdef __APPLE__
    registry.scan_deferred(plugins_dir.string().c_str());
#else
    registry.scan_deferred(exe_dir.string().c_str());
#endif
    register_builtin_operators(registry);

    // --- Load self-describing .wgsl filter presets ---
    std::string filters_dir = (resources_dir / "filters").string();
    registry.scan_wgsl_presets(filters_dir);

    // --- Load factory presets for operators ---
    std::string factory_presets_dir = (resources_dir / "factory_presets").string();
    registry.scan_factory_presets(factory_presets_dir);

    // --- Package management (needs to outlive main loop for catalog/install) ---
    vivid::PackageCompiler pkg_compiler(build_paths.source_dir, build_paths.build_dir);
    vivid::PackageManager pkg_manager(pkg_compiler, registry);
    if (std::getenv("VIVID_SKIP_PACKAGE_SCAN")) {
        std::fprintf(stderr, "[vivid] Skipping installed package scan (VIVID_SKIP_PACKAGE_SCAN)\n");
    } else {
        pkg_manager.scan_installed();
    }
    vivid::PackageCatalog pkg_catalog(pkg_manager);
    pkg_manager.set_resolver([&pkg_catalog](const std::string& name) -> std::string {
        for (const auto& e : pkg_catalog.entries())
            if (e.name == name) return e.url;
        return "";
    });
    // Non-blocking background fetch so update alerts can be shown without delaying startup.
    pkg_catalog.refresh();

    // --- Core app update checks (non-blocking appcast fetch) ---
    vivid::AppUpdateManager app_updates(VIVID_CORE_VERSION);
    app_updates.set_skipped_version(settings.core_update_skipped_version);
    if (settings.core_update_auto_check) {
        app_updates.refresh();
    }

    // --- Recursive graph discovery + graph-level meta ---
    std::filesystem::path bundle_graphs_root = resources_dir / "graphs";
    if (!std::filesystem::is_directory(bundle_graphs_root)) {
        // Compatibility fallback for older flat resource layout.
        bundle_graphs_root = resources_dir;
    }
    std::filesystem::path graphs_root = workspace_root / "graphs";
    if (!std::filesystem::is_directory(graphs_root)) {
        graphs_root = bundle_graphs_root;
    }
    std::vector<vivid::ui::ExampleEntry> discovered_examples =
        discover_examples_with_packages(graphs_root, &pkg_manager);
    graph_file = resolve_graph_input_path(graph_file, graphs_root, discovered_examples);

    // Helper: populate graph.load_diagnostics by comparing saved pkg versions to installed.
    // Must be called after a successful graph.load().
    auto run_graph_package_diagnostics = [&](vivid::Graph& g) {
        g.load_diagnostics.clear();
        auto packages = pkg_manager.list();
        std::unordered_map<std::string, std::string> installed_map;
        for (const auto& p : packages) installed_map[p.name] = p.version;
        for (const auto& node : g.nodes()) {
            if (node.pkg_name.empty()) continue;
            auto it = installed_map.find(node.pkg_name);
            if (it == installed_map.end() || it->second.empty()) continue;
            const std::string& installed_ver = it->second;
            auto cls = vivid::PackageManager::classify_version_delta(node.pkg_version, installed_ver);
            if (cls == vivid::PackageUpdateClass::CompatibleUpdate ||
                cls == vivid::PackageUpdateClass::IncompatibleUpdate) {
                vivid::Graph::LoadDiagnostic diag;
                diag.node_id           = node.id;
                diag.pkg_name          = node.pkg_name;
                diag.saved_version     = node.pkg_version;
                diag.installed_version = installed_ver;
                diag.classification    = (cls == vivid::PackageUpdateClass::IncompatibleUpdate)
                                         ? "incompatible_update" : "compatible_update";
                g.load_diagnostics.push_back(std::move(diag));
                if (cls == vivid::PackageUpdateClass::IncompatibleUpdate) {
                    std::fprintf(stderr,
                        "[graph] Package version mismatch (incompatible): "
                        "node '%s' saved with %s@%s, installed %s\n",
                        node.id.c_str(), node.pkg_name.c_str(),
                        node.pkg_version.c_str(), installed_ver.c_str());
                } else {
                    std::fprintf(stderr,
                        "[graph] Package update: node '%s' %s saved=%s installed=%s\n",
                        node.id.c_str(), node.pkg_name.c_str(),
                        node.pkg_version.c_str(), installed_ver.c_str());
                }
            }
        }
    };

    // Helper: annotate graph nodes with their package provenance (called before save).
    auto annotate_graph_packages = [&](vivid::Graph& g) {
        auto packages = pkg_manager.list();
        std::unordered_map<std::string, std::string> pkg_ver_map;
        for (const auto& p : packages) pkg_ver_map[p.name] = p.version;
        for (auto& node : g.nodes_mut()) {
            const auto* pkg = registry.package_for_type(node.type);
            if (pkg) {
                node.pkg_name    = *pkg;
                node.pkg_version = pkg_ver_map.count(*pkg) ? pkg_ver_map[*pkg] : "";
            }
        }
    };

    // --- Load graph ---
    vivid::Graph graph;
    vivid::RuntimeCore runtime;
    bool graph_loaded = false;

    // Working directory for user filter shaders: {graph_dir}/{graph_stem}_filters/
    std::string working_filters_dir;

    bool initial_load_ok = false;
    if (graph_file.empty()) {
        // No file given: load default graph template (user override → bundled fallback)
        auto user_template = std::filesystem::path(vivid::get_config_dir()) / "default_graph.json";
        auto bundled_template = resources_dir / "default_graph.json";
        if (std::filesystem::exists(user_template))
            initial_load_ok = graph.load(user_template.string().c_str());
        if (!initial_load_ok)
            initial_load_ok = graph.load(bundled_template.string().c_str());
        if (!initial_load_ok)
            std::fprintf(stderr, "[vivid] Error: could not load default graph template\n");
        // Clear source_path so Cmd-S prompts for a save location instead of
        // overwriting the default template.
        if (initial_load_ok)
            graph.set_source_path("");
    } else {
        initial_load_ok = graph.load(graph_file.c_str());
        if (!initial_load_ok)
            std::fprintf(stderr, "[vivid] Graph load failed (non-fatal, continuing)\n");
    }
    if (initial_load_ok) {
        run_graph_package_diagnostics(graph);
        // Register user filters from graph before building the runtime
        if (!graph.filters().empty()) {
            auto gp = std::filesystem::path(graph.source_path());
            auto graph_dir = gp.parent_path();
            auto graph_stem = gp.stem();
            working_filters_dir = (graph_dir / (graph_stem.string() + "_filters")).string();
            std::filesystem::create_directories(working_filters_dir);

            for (const auto& fd : graph.filters()) {
                // Write shader source to working file
                std::string working_path = working_filters_dir + "/" + fd.name + ".wgsl";
                {
                    std::ofstream ofs(working_path);
                    ofs << fd.shader;
                }

                // Build DataDrivenFilterConfig
                auto config = std::make_shared<vivid::DataDrivenFilterConfig>();
                config->name = fd.name;
                config->shader_path = working_path;
                config->source_builtin = fd.source;
                config->time_dependent = fd.time_dependent;
                for (const auto& pd : fd.params) {
                    vivid::DataDrivenFilterConfig::ParamDef cpd;
                    cpd.name = pd.name;
                    cpd.default_value = pd.default_value;
                    cpd.min_value = pd.min_value;
                    cpd.max_value = pd.max_value;
                    config->params.push_back(std::move(cpd));
                }
                registry.register_user_filter(fd.name, config);
            }
        }

        // Load only the operators this graph actually uses
        registry.load_for_graph(graph);

        if (runtime.build(graph, registry)) {
            graph_loaded = true;
        } else {
            std::fprintf(stderr, "[vivid] Runtime build failed (non-fatal, continuing)\n");
        }
    }

    bool has_gpu_ops = graph_loaded && runtime.has_gpu_operators();

    // Allocate per-node GPU textures
    if (has_gpu_ops) {
        runtime.allocate_gpu_textures(gpu.device(), kDefaultTexW, kDefaultTexH, kOffscreenFormat);
    }
    int video_out_idx = has_gpu_ops ? runtime.find_effective_gpu_sink() : -1;

    // --- Audio engine ---
    vivid::AudioEngine audio_engine;
    bool has_audio = false;
    if (graph_loaded && runtime.has_audio_operators()) {
        if (audio_engine.build(runtime)) {
            if (audio_engine.start()) {
                has_audio = true;
            }
        }
    }

    // --- System MIDI listener (for MIDI mapping) ---
    vivid::SystemMidiListener system_midi;
    system_midi.open_all();  // listen on all available MIDI ports

    // --- RuntimeAPI ---
    vivid::RuntimeAPI runtime_api(graph, runtime, audio_engine, registry, &system_midi);
    runtime_api.set_resources_dir(resources_dir.string());

    // --- Control server (MCP HTTP bridge) ---
    vivid::CaptureCoordinator capture_coordinator;
    if (has_audio) capture_coordinator.set_audio_engine(&audio_engine);
    capture_coordinator.set_runtime_api(&runtime_api);
    vivid::ControlServer control_server;
    control_server.set_capture_coordinator(&capture_coordinator);
    control_server.set_package_manager(&pkg_manager);
    control_server.set_package_compiler(&pkg_compiler);
    control_server.set_package_catalog(&pkg_catalog);
    control_server.set_app_update_manager(&app_updates);
    control_server.set_settings(&settings);
    if (!control_server.start(9876)) {
        std::fprintf(stderr, "[vivid] Control server unavailable (port 9876 in use?)\n");
    }
    if (!src_dir.empty())
        control_server.set_src_dir(src_dir);

    auto rebuild_live_runtime_from_graph = [&]() -> bool {
        if (has_audio) {
            audio_engine.shutdown();
            has_audio = false;
        }
        runtime.shutdown();
        thumb_cache.clear();

        if (!runtime.build(graph, registry)) {
            std::fprintf(stderr, "[vivid] Runtime rebuild failed after registry refresh\n");
            graph_loaded = false;
            has_gpu_ops = false;
            video_out_idx = -1;
            capture_coordinator.set_audio_engine(nullptr);
            return false;
        }

        graph_loaded = runtime.compiled_graph() && !runtime.compiled_graph()->nodes.empty();
        has_gpu_ops = runtime.has_gpu_operators();
        if (has_gpu_ops) {
            runtime.allocate_gpu_textures(gpu.device(), kDefaultTexW, kDefaultTexH, kOffscreenFormat);
            video_out_idx = runtime.find_effective_gpu_sink();
        } else {
            video_out_idx = -1;
        }

        if (runtime.has_audio_operators()) {
            if (audio_engine.build(runtime) && audio_engine.start()) {
                has_audio = true;
            }
        }

        capture_coordinator.set_audio_engine(has_audio ? &audio_engine : nullptr);
        return true;
    };

    vivid::ui::Renderer2D text_renderer;
    bool text_renderer_ok = false;
    {
        // Look for font next to executable, or in source tree
        std::string font_path = (resources_dir / "JetBrainsMono-Regular.ttf").string();
        if (!std::filesystem::exists(font_path)) {
            auto alt = exe_dir.parent_path() / "fonts" / "JetBrainsMono-Regular.ttf";
            if (std::filesystem::exists(alt)) font_path = alt.string();
        }
        if (text_renderer.init(gpu.device(), gpu.surface_format(), font_path.c_str(), 16.0f, dpi_scale)) {
            text_renderer_ok = true;
        } else {
            std::fprintf(stderr, "[vivid] Text renderer disabled (font not found)\n");
        }
    }

    RuntimeCommandSink command_sink(runtime_api);
    OperatorInfoCache op_info_cache;
    command_sink.set_registry(&registry);
    command_sink.set_graph(&graph);
    command_sink.set_op_cache(&op_info_cache);
    command_sink.set_working_filters_dir(working_filters_dir);
    command_sink.set_settings(&settings);
    command_sink.set_capture_coordinator(&capture_coordinator);
    command_sink.set_runtime_flags(&has_gpu_ops, &has_audio);
    command_sink.set_package_manager(&pkg_manager);
    vivid::ui::NodeGraphUI graph_ui(command_sink);
    graph_ui.set_dpi_scale(dpi_scale);
    graph_ui.set_bezier_wires(settings.bezier_wires);
    graph_ui.set_show_param_wires(settings.show_param_wires);
    graph_ui.set_pan_gesture(settings.pan_gesture);
    bool screenshot_select_applied = screenshot_select_node.empty();
    bool screenshot_select_warned = false;
    {
        // Resolve mcp/ directory: <bundle>/Contents/Resources/mcp or <exe_dir>/mcp
        auto mcp_dir = resources_dir / "mcp";
        graph_ui.set_mcp_dir(mcp_dir.string());
    }
    auto refresh_discovered_examples = [&]() {
        discovered_examples = discover_examples_with_packages(graphs_root, &pkg_manager);
        graph_ui.set_examples(discovered_examples);
    };

    // Async package action state — mirrors PackageCatalog::refresh() pattern
    std::mutex          pkg_action_mutex;
    enum class PkgActionState { Idle, Running, Done, Error };
    PkgActionState      pkg_action_state{PkgActionState::Idle};
    std::string         pkg_action_error_msg;
    bool                pkg_action_needs_refresh{false};
    std::thread         pkg_action_thread;

    std::vector<vivid::ui::PackageBrowserEntry> pkg_browser_entries_cache;
    auto refresh_package_browser_entries_cache = [&]() {
        {
            std::lock_guard<std::mutex> lk(pkg_action_mutex);
            if (pkg_action_state == PkgActionState::Running)
                return;
        }
        std::vector<vivid::ui::PackageBrowserEntry> out;
        std::unordered_map<std::string, vivid::PackageInfo> installed_map;
        for (const auto& p : pkg_manager.list()) {
            installed_map[p.name] = p;
        }

        // Collect per-package ABI mismatch and loader failure diagnostics
        std::unordered_set<std::string> packages_needing_rebuild;
        for (const auto& d : registry.abi_mismatch_diagnostics())
            if (!d.package_name.empty()) packages_needing_rebuild.insert(d.package_name);
        for (const auto& d : registry.loader_failure_diagnostics())
            if (!d.package_name.empty()) packages_needing_rebuild.insert(d.package_name);

        auto entries = pkg_catalog.entries();
        out.reserve(entries.size() + installed_map.size());
        for (const auto& e : entries) {
            vivid::ui::PackageBrowserEntry ui_e;
            ui_e.name = e.name;
            ui_e.description = e.description;
            ui_e.version = e.version;
            ui_e.author = e.author;
            auto it = installed_map.find(e.name);
            if (it != installed_map.end()) {
                ui_e.installed = true;
                ui_e.linked = it->second.linked;
                ui_e.category = it->second.category;
                ui_e.tags = it->second.tags;
                if (packages_needing_rebuild.count(ui_e.name))  {
                    ui_e.needs_rebuild = true;
                    ui_e.health_detail = "ABI mismatch \xe2\x80\x94 try rebuild";
                }
                installed_map.erase(it);
            }
            out.push_back(std::move(ui_e));
        }
        for (const auto& [name, info] : installed_map) {
            vivid::ui::PackageBrowserEntry ui_e;
            ui_e.name = info.name;
            ui_e.description = info.description;
            ui_e.version = info.version;
            ui_e.author = info.author;
            ui_e.category = info.category;
            ui_e.tags = info.tags;
            ui_e.installed = true;
            ui_e.linked = info.linked;
            if (packages_needing_rebuild.count(ui_e.name)) {
                ui_e.needs_rebuild = true;
                ui_e.health_detail = "ABI mismatch \xe2\x80\x94 try rebuild";
            }
            out.push_back(std::move(ui_e));
        }
        pkg_browser_entries_cache = std::move(out);
    };
    refresh_package_browser_entries_cache();

    vivid::ui::PackageBrowserCallbacks pkg_browser_cbs;
    pkg_browser_cbs.refresh = [&pkg_catalog]() {
        pkg_catalog.refresh();
    };
    pkg_browser_cbs.list_entries = [&pkg_browser_entries_cache]() {
            return pkg_browser_entries_cache;
    };
    pkg_browser_cbs.fetch_state = [&pkg_catalog]() {
        switch (pkg_catalog.fetch_state()) {
            case vivid::CatalogFetchState::Idle: return vivid::ui::PackageBrowserFetchState::Idle;
            case vivid::CatalogFetchState::Fetching: return vivid::ui::PackageBrowserFetchState::Fetching;
            case vivid::CatalogFetchState::Ready: return vivid::ui::PackageBrowserFetchState::Ready;
            case vivid::CatalogFetchState::Error: return vivid::ui::PackageBrowserFetchState::Error;
        }
        return vivid::ui::PackageBrowserFetchState::Error;
    };
    pkg_browser_cbs.fetch_error = [&pkg_catalog]() {
        return pkg_catalog.fetch_error();
    };
    pkg_browser_cbs.update_summary = [&pkg_catalog]() {
        auto s = pkg_catalog.summarize_updates(VIVID_CORE_VERSION);
        vivid::ui::PackageBrowserUpdateSummary out;
        out.installed_packages = s.installed_packages;
        out.updates_available = s.updates_available;
        out.incompatible_updates = s.incompatible_updates;
        return out;
    };
    pkg_browser_cbs.install = [&pkg_catalog,
                               &pkg_action_mutex, &pkg_action_state,
                               &pkg_action_error_msg, &pkg_action_needs_refresh,
                               &pkg_action_thread](
                                   const std::string& name, std::string&) -> bool {
        {
            std::lock_guard<std::mutex> lk(pkg_action_mutex);
            if (pkg_action_state == PkgActionState::Running) return false;
            pkg_action_state = PkgActionState::Running;
            pkg_action_error_msg.clear();
            pkg_action_needs_refresh = false;
        }
        if (pkg_action_thread.joinable()) pkg_action_thread.join();
        pkg_action_thread = std::thread([&pkg_catalog, name,
                     &pkg_action_mutex, &pkg_action_state,
                     &pkg_action_error_msg, &pkg_action_needs_refresh]() {
            auto r = pkg_catalog.install(name);
            std::lock_guard<std::mutex> lk(pkg_action_mutex);
            pkg_action_error_msg = r.success ? "" : r.error;
            pkg_action_needs_refresh = r.success;
            pkg_action_state = r.success ? PkgActionState::Done : PkgActionState::Error;
        });
        return true;
    };
    pkg_browser_cbs.uninstall = [&pkg_catalog,
                                 &pkg_action_mutex, &pkg_action_state,
                                 &pkg_action_error_msg, &pkg_action_needs_refresh,
                                 &pkg_action_thread](
                                     const std::string& name, std::string&) -> bool {
        {
            std::lock_guard<std::mutex> lk(pkg_action_mutex);
            if (pkg_action_state == PkgActionState::Running) return false;
            pkg_action_state = PkgActionState::Running;
            pkg_action_error_msg.clear();
            pkg_action_needs_refresh = false;
        }
        if (pkg_action_thread.joinable()) pkg_action_thread.join();
        pkg_action_thread = std::thread([&pkg_catalog, name,
                     &pkg_action_mutex, &pkg_action_state,
                     &pkg_action_error_msg, &pkg_action_needs_refresh]() {
            bool ok = pkg_catalog.uninstall(name);
            std::lock_guard<std::mutex> lk(pkg_action_mutex);
            pkg_action_error_msg = ok ? "" : "Failed to uninstall " + name;
            pkg_action_needs_refresh = ok;
            pkg_action_state = ok ? PkgActionState::Done : PkgActionState::Error;
        });
        return true;
    };
    pkg_browser_cbs.unlink = [&pkg_manager,
                              &pkg_action_mutex, &pkg_action_state,
                              &pkg_action_error_msg, &pkg_action_needs_refresh,
                              &pkg_action_thread](
                                  const std::string& name, std::string&) -> bool {
        {
            std::lock_guard<std::mutex> lk(pkg_action_mutex);
            if (pkg_action_state == PkgActionState::Running) return false;
            pkg_action_state = PkgActionState::Running;
            pkg_action_error_msg.clear();
            pkg_action_needs_refresh = false;
        }
        if (pkg_action_thread.joinable()) pkg_action_thread.join();
        pkg_action_thread = std::thread([&pkg_manager, name,
                     &pkg_action_mutex, &pkg_action_state,
                     &pkg_action_error_msg, &pkg_action_needs_refresh]() {
            bool ok = pkg_manager.unlink(name);
            std::lock_guard<std::mutex> lk(pkg_action_mutex);
            pkg_action_error_msg = ok ? "" : "Failed to unlink " + name;
            pkg_action_needs_refresh = ok;
            pkg_action_state = ok ? PkgActionState::Done : PkgActionState::Error;
        });
        return true;
    };
    pkg_browser_cbs.link = [&pkg_manager,
                            &pkg_action_mutex, &pkg_action_state,
                            &pkg_action_error_msg, &pkg_action_needs_refresh,
                            &pkg_action_thread](
                                const std::string& path, std::string&) -> bool {
        {
            std::lock_guard<std::mutex> lk(pkg_action_mutex);
            if (pkg_action_state == PkgActionState::Running) return false;
            pkg_action_state = PkgActionState::Running;
            pkg_action_error_msg.clear();
            pkg_action_needs_refresh = false;
        }
        if (pkg_action_thread.joinable()) pkg_action_thread.join();
        pkg_action_thread = std::thread([&pkg_manager, path,
                     &pkg_action_mutex, &pkg_action_state,
                     &pkg_action_error_msg, &pkg_action_needs_refresh]() {
            auto r = pkg_manager.link(path);
            std::lock_guard<std::mutex> lk(pkg_action_mutex);
            pkg_action_error_msg = r.success ? "" : r.error;
            // Refresh examples if the symlink was created (graphs/ dir may exist even if compile failed)
            pkg_action_needs_refresh = r.success || !r.info.path.empty();
            pkg_action_state = r.success ? PkgActionState::Done : PkgActionState::Error;
        });
        return true;
    };
    pkg_browser_cbs.rebuild = [&pkg_manager,
                                &pkg_action_mutex, &pkg_action_state,
                                &pkg_action_error_msg, &pkg_action_needs_refresh,
                                &pkg_action_thread](
                                    const std::string& name, std::string&) -> bool {
        {
            std::lock_guard<std::mutex> lk(pkg_action_mutex);
            if (pkg_action_state == PkgActionState::Running) return false;
            pkg_action_state = PkgActionState::Running;
            pkg_action_error_msg.clear();
            pkg_action_needs_refresh = false;
        }
        if (pkg_action_thread.joinable()) pkg_action_thread.join();
        pkg_action_thread = std::thread([&pkg_manager, name,
                     &pkg_action_mutex, &pkg_action_state,
                     &pkg_action_error_msg, &pkg_action_needs_refresh]() {
            auto r = pkg_manager.rebuild(name);
            std::lock_guard<std::mutex> lk(pkg_action_mutex);
            pkg_action_error_msg = r.success ? "" : r.error;
            pkg_action_needs_refresh = true;
            pkg_action_state = r.success ? PkgActionState::Done : PkgActionState::Error;
        });
        return true;
    };
    graph_ui.set_package_browser_callbacks(std::move(pkg_browser_cbs));
    if (registry.has_abi_mismatch_diagnostics()) {
        auto mismatches = registry.abi_mismatch_diagnostics();
        std::string msg = "Plugin ABI mismatch detected. Rebuild vivid and rerun package rebuild.";
        if (!mismatches.empty()) {
            msg += " First mismatch: ";
            msg += mismatches.front().plugin_name.empty()
                       ? mismatches.front().plugin_path
                       : mismatches.front().plugin_name;
            msg += " (plugin ABI " + std::to_string(mismatches.front().plugin_abi) +
                   ", runtime ABI " + std::to_string(mismatches.front().runtime_abi) + ")";
        }
        graph_ui.notify_pkg_action_complete(false, msg);
    }
    graph_ui.set_examples(discovered_examples);
    graph_ui.set_example_package_checker(
        [&pkg_manager](const std::vector<std::string>& requires, std::string& missing) {
            for (const auto& pkg : requires) {
                if (!pkg.empty() && !pkg_manager.is_installed(pkg)) {
                    missing = pkg;
                    return false;
                }
            }
            missing.clear();
            return true;
        });
    graph_ui.set_core_update_notice_callbacks(
        [&]() {
#ifdef __APPLE__
            std::string err;
            if (!vivid::SparkleBridge::check_for_updates(&err)) {
                auto info = app_updates.latest();
                if (!info.download_url.empty()) {
                    if (!vivid::open_url(info.download_url, &err)) {
                        std::fprintf(stderr, "[vivid] Update install fallback failed: %s\n", err.c_str());
                    }
                } else {
                    std::fprintf(stderr, "[vivid] Sparkle unavailable: %s\n", err.c_str());
                }
            }
#else
            auto info = app_updates.latest();
            std::string err;
            if (!info.download_url.empty() && !vivid::open_url(info.download_url, &err)) {
                std::fprintf(stderr, "[vivid] Update install failed: %s\n", err.c_str());
            }
#endif
        },
        [&]() {
            auto info = app_updates.latest();
            settings.core_update_skipped_version = info.latest_version;
            app_updates.set_skipped_version(info.latest_version);
            vivid::save_settings(settings);
        },
        [&]() {});
    if (graph.has_viewport())
        graph_ui.set_viewport(graph.viewport_pan_x, graph.viewport_pan_y, graph.viewport_zoom);

    // Detect available text editors and set up style options
    {
        auto detected = vivid::detect_editors();
        std::vector<std::string> editor_names, editor_ids;
        int editor_sel = 0;
        for (size_t i = 0; i < detected.size(); ++i) {
            editor_names.push_back(detected[i].name);
            editor_ids.push_back(detected[i].app_id);
            if (detected[i].app_id == settings.editor)
                editor_sel = static_cast<int>(i);
        }
        // If editor is "custom", select that
        if (settings.editor == "custom") {
            for (size_t i = 0; i < editor_ids.size(); ++i) {
                if (editor_ids[i] == "custom") { editor_sel = static_cast<int>(i); break; }
            }
        }
        graph_ui.set_editor_options(std::move(editor_names), std::move(editor_ids),
                                    editor_sel, settings.editor_command);

        vivid::ui::ensure_default_themes();
        auto themes = vivid::ui::discover_themes();
        auto styles = vivid::ui::load_all_themes(themes);
        int style_sel = 0;
        for (size_t i = 0; i < styles.size(); ++i) {
            if (styles[i].id == settings.style_id)
                style_sel = static_cast<int>(i);
        }
        graph_ui.set_style_options(std::move(styles), style_sel, std::move(themes));
    }

    // Wire up custom inspector callback
    graph_ui.set_custom_inspector_callback(
        [&runtime](const std::string& node_id, VividInspectorContext* ctx) {
            const auto* cn = runtime.compiled_graph() ? runtime.compiled_graph()->find_node(node_id) : nullptr;
            if (!cn || !cn->loader || !cn->instance) return;
            cn->loader->draw_inspector(cn->instance, ctx);
        });

    // Set up GLFW input callbacks
    WindowUserData window_user_data;
    window_user_data.graph_ui = &graph_ui;
    window_user_data.runtime_api = &runtime_api;
    window_user_data.graph = &graph;
    window_user_data.working_filters_dir = working_filters_dir;
    window_user_data.settings = &settings;
    glfwSetWindowUserPointer(window, &window_user_data);
    glfwSetCharCallback(window, char_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetDropCallback(window, drop_callback);

    // --- Hot-reload ---
    vivid::FileWatcher file_watcher;
    vivid::HotReloader hot_reloader;
    bool hot_reload_enabled = false;
    auto next_package_watch_rescan_at = std::chrono::steady_clock::time_point{};
    auto refresh_package_watches = [&]() {
        int watched_pkg_files = 0;
        for (const auto& pkg : pkg_manager.list()) {
            watched_pkg_files += add_watch_for_resolved_package(file_watcher, pkg);
        }
        if (watched_pkg_files > 0) {
            std::fprintf(stderr, "[vivid] FileWatcher: watching %d package files (rescan)\n",
                         watched_pkg_files);
        }
    };
    {
        if (src_dir.empty()) {
            auto probe = exe_dir;
            for (int i = 0; i < 5 && probe.has_parent_path(); ++i) {
                probe = probe.parent_path();
                if (std::filesystem::exists(probe / "operators")) {
                    src_dir = probe.string();
                    break;
                }
            }
        }

        if (!src_dir.empty()) {
            std::string operators_dir = src_dir + "/operators";
            runtime.set_operators_src_dir(operators_dir);
            command_sink.set_operators_dir(operators_dir);
            command_sink.set_filters_dir(filters_dir);
            command_sink.set_build_dir(build_paths.build_dir);
            op_info_cache.set_operators_dir(operators_dir);
            // Set working filters dir if not already determined from graph
            if (working_filters_dir.empty() && !graph.source_path().empty()) {
                auto gp = std::filesystem::path(graph.source_path());
                working_filters_dir = (gp.parent_path() / (gp.stem().string() + "_filters")).string();
                command_sink.set_working_filters_dir(working_filters_dir);
            }
            if (file_watcher.start(operators_dir) && hot_reloader.start(build_paths.build_dir)) {
                hot_reload_enabled = true;
                control_server.set_hot_reloader(&hot_reloader);
                command_sink.set_hot_reloader(&hot_reloader);
                std::fprintf(stderr, "[vivid] Hot-reload enabled (watching %s)\n", operators_dir.c_str());

                // Also watch package source files (both operators/ and src/ layouts).
                refresh_package_watches();
                next_package_watch_rescan_at = std::chrono::steady_clock::now() + std::chrono::seconds(1);

                // Set up package compile callback for hot-reloader
                std::string pkg_src_dir = src_dir;
                std::string pkg_build_dir = build_paths.build_dir;
                hot_reloader.set_package_compiler(
                    [&pkg_manager, pkg_src_dir, pkg_build_dir](const std::string& target) -> vivid::ReloadResult {
                        // Parse "pkg:<package_name>:<operator_name>"
                        vivid::ReloadResult result;
                        result.target_name = target;

                        auto first_colon = target.find(':');
                        auto second_colon = target.find(':', first_colon + 1);
                        if (first_colon == std::string::npos || second_colon == std::string::npos) {
                            result.success = false;
                            result.error_output = "Invalid package target: " + target;
                            return result;
                        }

                        std::string pkg_name = target.substr(first_colon + 1, second_colon - first_colon - 1);
                        std::string op_name = target.substr(second_colon + 1);
                        std::string pkg_dir = pkg_manager.resolve_package_path(pkg_name);
                        if (pkg_dir.empty()) {
                            result.success = false;
                            result.error_output = "Cannot resolve active package path for " + pkg_name;
                            return result;
                        }

                        // CMake src/ layout package (modern sibling package flow)
                        // supports hot-reload by building the package target directly.
                        std::filesystem::path src_cpp = std::filesystem::path(pkg_dir) / "src" / (op_name + ".cpp");
                        if (std::filesystem::exists(src_cpp)) {
                            auto quote = [](const std::string& s) { return "'" + s + "'"; };
                            std::string pkg_build = pkg_dir + "/build";

                            // Ensure build directory exists/configured.
                            if (!std::filesystem::exists(std::filesystem::path(pkg_build) / "CMakeCache.txt")) {
                                std::filesystem::create_directories(pkg_build);
                                std::string cfg_cmd = "cmake"
                                    " -B " + quote(pkg_build) +
                                    " -S " + quote(pkg_dir) +
                                    " -DVIVID_SRC_DIR=" + quote(pkg_src_dir) +
                                    " -DVIVID_BUILD_DIR=" + quote(pkg_build_dir) +
                                    " -DVIVID_PLUGIN_SUFFIX=" + std::string(vivid::kPluginSuffix) +
                                    " 2>&1";
                                std::string cfg_out;
                                FILE* cfg_pipe = popen(cfg_cmd.c_str(), "r");
                                if (!cfg_pipe) {
                                    result.success = false;
                                    result.error_output = "Failed to execute cmake configure for package target";
                                    return result;
                                }
                                std::array<char, 256> cfg_buf;
                                while (fgets(cfg_buf.data(), cfg_buf.size(), cfg_pipe) != nullptr)
                                    cfg_out += cfg_buf.data();
                                int cfg_status = pclose(cfg_pipe);
                                if (cfg_status != 0) {
                                    result.success = false;
                                    result.error_output = "cmake configure failed:\n" + cfg_out;
                                    return result;
                                }
                            }

                            std::string build_cmd = "cmake --build " + quote(pkg_build) +
                                                    " --target " + quote(op_name) + " 2>&1";
                            std::string build_out;
                            FILE* pipe = popen(build_cmd.c_str(), "r");
                            if (!pipe) {
                                result.success = false;
                                result.error_output = "Failed to execute cmake build for package target";
                                return result;
                            }
                            std::array<char, 256> buf;
                            while (fgets(buf.data(), buf.size(), pipe) != nullptr)
                                build_out += buf.data();
                            int status = pclose(pipe);
                            if (status != 0) {
                                result.success = false;
                                result.error_output = "cmake build failed:\n" + build_out;
                                return result;
                            }

                            result.success = true;
                            result.staged_dylib_path = pkg_build + "/" + op_name + vivid::kPluginSuffix;
                            return result;
                        }

                        // Legacy operators/<category>/<name>/ layout
                        vivid::PackageCompiler compiler(pkg_src_dir, pkg_build_dir);
                        std::string op_rel;
                        for (const auto& domain : {"audio", "control", "gpu"}) {
                            std::string candidate = pkg_dir + "/operators/" +
                                domain + "/" + op_name + "/" + op_name + ".cpp";
                            if (std::filesystem::exists(candidate)) {
                                op_rel = std::string(domain) + "/" + op_name;
                                break;
                            }
                        }
                        if (op_rel.empty()) {
                            result.success = false;
                            result.error_output = "Cannot find operator source for " + op_name + " in " + pkg_dir;
                            return result;
                        }

                        auto cr = compiler.compile_operator(pkg_dir, op_rel, false);
                        result.success = cr.success;
                        result.staged_dylib_path = cr.dylib_path;
                        result.error_output = cr.error_output;
                        return result;
                    });
            }
        } else {
            std::fprintf(stderr, "[vivid] Hot-reload disabled (operators/ not found; use --src-dir)\n");
        }
    }

    auto enter_fullscreen = [&](GLFWmonitor* preferred_monitor) {
        if (display_state.fullscreen) return;
        glfwGetWindowPos(window, &display_state.windowed_x, &display_state.windowed_y);
        glfwGetWindowSize(window, &display_state.windowed_w, &display_state.windowed_h);
        GLFWmonitor* monitor = preferred_monitor;
        if (!monitor_connected(monitor)) monitor = monitor_for_window(window);
        if (!monitor) monitor = glfwGetPrimaryMonitor();
        if (!monitor) return;
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitor, &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (!mode) return;
        const int mw = mode->width;
        const int mh = mode->height;
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowPos(window, mx, my);
        glfwSetWindowSize(window, mw, mh);
#ifdef __APPLE__
        vivid::macos_set_presentation_fullscreen(true);
#endif
        display_state.fullscreen = true;
        display_state.fullscreen_monitor = monitor;
        display_state.surface_reconfigure_pending = true;
        display_state.surface_settle_frames = 2;
        std::fprintf(stderr, "[vivid] Fullscreen enabled (borderless %dx%d at %d,%d)\n",
                     mw, mh, mx, my);
    };

    auto exit_fullscreen = [&]() {
        if (!display_state.fullscreen) return;
        int x = display_state.windowed_x;
        int y = display_state.windowed_y;
        int w = display_state.windowed_w;
        int h = display_state.windowed_h;
        clamp_window_rect_to_monitor(glfwGetPrimaryMonitor(), &x, &y, &w, &h);
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowPos(window, x, y);
        glfwSetWindowSize(window, w, h);
#ifdef __APPLE__
        vivid::macos_set_presentation_fullscreen(false);
#endif
        display_state.fullscreen = false;
        display_state.fullscreen_monitor = nullptr;
        display_state.surface_reconfigure_pending = true;
        display_state.surface_settle_frames = 2;
        std::fprintf(stderr, "[vivid] Fullscreen disabled (%dx%d at %d,%d)\n", w, h, x, y);
    };

    auto toggle_fullscreen = [&]() {
        if (display_state.fullscreen) {
            exit_fullscreen();
        } else {
            enter_fullscreen(monitor_for_window(window));
        }
    };

    auto load_graph_runtime = [&](const std::string& input_path, const char* label) {
        std::string resolved = resolve_graph_input_path(input_path, graphs_root, discovered_examples);
        if (!graph.load(resolved.c_str())) {
            std::fprintf(stderr, "[vivid] %s: failed to load %s\n", label, resolved.c_str());
            return false;
        }
        run_graph_package_diagnostics(graph);
        registry.load_for_graph(graph);
        auto result = runtime_api.reload(has_gpu_ops, has_audio);
        if (result.ok) {
            graph_loaded = true;
            command_sink.reset_undo_history();
        }
        std::fprintf(stderr, "[vivid] %s: %s\n", label, result.message.c_str());
        return result.ok;
    };

    auto new_graph_runtime = [&]() {
        auto result = runtime_api.new_graph(has_gpu_ops, has_audio);
        if (result.ok) {
            graph_loaded = true;
            command_sink.reset_undo_history();
        }
        std::fprintf(stderr, "[vivid] New Graph: %s\n", result.message.c_str());
        return result.ok;
    };

    vivid::FileDropRegistry file_drop_registry;
    auto refresh_file_drop_registry = [&]() {
        file_drop_registry.refresh(registry);
    };
    refresh_file_drop_registry();

    auto create_file_drop_node = [&](const vivid::FileDropMatch& match,
                                     const std::string& dropped_path,
                                     float graph_x, float graph_y) {
        if (!registry.find(match.type_name)) {
            std::fprintf(stderr, "[vivid] Drop: failed to load operator %s\n",
                         match.type_name.c_str());
            return false;
        }

        std::string id;
        for (int n = 1; ; ++n) {
            id = match.type_name + std::to_string(n);
            if (!graph.find_node(id)) break;
        }

        std::string add_error;
        if (!command_sink.try_add_node(match.type_name, id, &add_error)) {
            std::fprintf(stderr, "[vivid] Drop: failed to add %s: %s\n",
                         match.type_name.c_str(), add_error.c_str());
            return false;
        }
        command_sink.set_node_layout(id, graph_x, graph_y);
        runtime_api.apply_pending(has_gpu_ops, has_audio);
        auto set_result = runtime_api.set_string_param(id, match.file_param, dropped_path);
        if (!set_result.ok) {
            std::fprintf(stderr, "[vivid] Drop: failed to set %s/%s: %s\n",
                         id.c_str(), match.file_param.c_str(), set_result.message.c_str());
            return false;
        }
        return true;
    };

    graph_ui.set_example_open_callback([&](const std::string& rel_path) {
        load_graph_runtime(rel_path, "Open Example");
    });
    graph_ui.set_graph_meta_save_callback([&](const vivid::ui::GraphMetaEditData& data,
                                              std::string& error) {
        if (!save_graph_meta_edit_data(data, error)) return false;
        refresh_discovered_examples();
        return true;
    });

    // Helper: save current graph (capturing viewport + filter shaders)
    auto do_save = [&]() {
        if (graph_ui.visible())
            graph.set_viewport(graph_ui.pan_x(), graph_ui.pan_y(), graph_ui.zoom());
        if (!working_filters_dir.empty()) {
            for (const auto& fd : graph.filters()) {
                std::string wpath = working_filters_dir + "/" + fd.name + ".wgsl";
                std::ifstream ifs(wpath);
                if (ifs) {
                    std::ostringstream ss;
                    ss << ifs.rdbuf();
                    graph.update_filter_shader(fd.name, ss.str());
                }
            }
        }
        annotate_graph_packages(graph);
        auto result = runtime_api.save();
        std::fprintf(stderr, "[vivid] Save: %s\n", result.message.c_str());
        return result.ok;
    };

    // Helper: open save-as dialog and save, returns true if saved
    auto do_save_as_dialog = [&]() -> bool {
        std::string path = vivid::ui::save_file_dialog();
        if (path.empty()) return false;
        if (graph_ui.visible())
            graph.set_viewport(graph_ui.pan_x(), graph_ui.pan_y(), graph_ui.zoom());
        annotate_graph_packages(graph);
        auto result = runtime_api.save_as(path);
        std::fprintf(stderr, "[vivid] Save As: %s\n", result.message.c_str());
        return result.ok;
    };

    // Helper: execute the pending action after save-confirm resolves
    auto execute_pending_action = [&](vivid::ui::NodeGraphUI::SaveConfirmAction action) {
        if (action == vivid::ui::NodeGraphUI::SaveConfirmAction::kNewGraph) {
            new_graph_runtime();
        } else {
            // kNewProject — open directory save dialog then create project
            std::string dir = vivid::ui::save_directory_dialog("MyProject");
            if (dir.empty()) return;
            auto result = runtime_api.new_project(dir, has_gpu_ops, has_audio);
            if (result.ok) {
                graph_loaded = true;
                command_sink.reset_undo_history();
            }
            std::fprintf(stderr, "[vivid] New Project: %s\n", result.message.c_str());
        }
    };

    // Save-confirm dialog callbacks
    graph_ui.on_save_confirm_cancel = [&]() {
        // do nothing — dialog already closed
    };
    graph_ui.on_save_confirm_dont_save = [&]() {
        execute_pending_action(graph_ui.save_confirm_action());
    };
    graph_ui.on_save_confirm_save = [&]() {
        auto action = graph_ui.save_confirm_action();
        bool saved;
        if (graph.source_path().empty())
            saved = do_save_as_dialog();
        else
            saved = do_save();
        if (saved)
            execute_pending_action(action);
    };

    // --- macOS native menu bar ---
#ifdef __APPLE__
    {
        vivid::MenuCallbacks menu_cbs;

        menu_cbs.on_about = [&]() { graph_ui.open_about(); };
        menu_cbs.on_new = [&]() {
            if (runtime_api.graph_dirty()) {
                graph_ui.open_save_confirm_dialog(
                    vivid::ui::NodeGraphUI::SaveConfirmAction::kNewGraph);
            } else {
                new_graph_runtime();
            }
        };
        menu_cbs.on_new_project = [&]() {
            if (runtime_api.graph_dirty()) {
                graph_ui.open_save_confirm_dialog(
                    vivid::ui::NodeGraphUI::SaveConfirmAction::kNewProject);
            } else {
                std::string dir = vivid::ui::save_directory_dialog("MyProject");
                if (dir.empty()) return;
                auto result = runtime_api.new_project(dir, has_gpu_ops, has_audio);
                if (result.ok) {
                    graph_loaded = true;
                    command_sink.reset_undo_history();
                }
                std::fprintf(stderr, "[vivid] New Project: %s\n", result.message.c_str());
            }
        };
        menu_cbs.on_preferences = [&]() {
            graph_ui.toggle_preferences();
        };

        menu_cbs.on_save = [&]() {
            if (graph.source_path().empty())
                do_save_as_dialog();
            else
                do_save();
        };

        menu_cbs.on_save_as = [&]() {
            do_save_as_dialog();
        };

        menu_cbs.on_open = [&]() {
            std::string path = vivid::ui::open_file_dialog();
            if (path.empty()) return;
            load_graph_runtime(path, "Open");
        };

        menu_cbs.on_open_example = [&]() {
            graph_ui.toggle_example_browser();
        };

        menu_cbs.on_open_graph_folder = [&]() {
            auto folder = std::filesystem::path(graph.source_path()).parent_path().string();
            if (!folder.empty())
                vivid::open_url(folder);
        };

        menu_cbs.has_graph_path = [&]() -> bool {
            return !graph.source_path().empty();
        };

        menu_cbs.on_export = [&]() {
            if (graph.source_path().empty()) {
                std::fprintf(stderr, "[vivid] Export: no graph loaded\n");
                return;
            }

            std::string output_path = vivid::ui::save_file_dialog("my_app");
            if (output_path.empty()) return;

            auto out = std::filesystem::path(output_path);
            std::string output_name = out.stem().string();
            std::string output_dir = (out.parent_path() / (output_name + "_export")).string();

            if (build_paths.source_dir.empty()) {
                std::fprintf(stderr, "[vivid] Export: cannot determine source directory\n");
                return;
            }

            vivid::ExportOptions opts;
            opts.graph_path = graph.source_path();
            opts.output_name = output_name;
            opts.output_path = output_path;
            opts.output_dir = output_dir;

            vivid::ExportPipeline pipeline(build_paths.source_dir, build_paths.build_dir);
            if (pipeline.run(opts, registry)) {
                std::fprintf(stderr, "[vivid] Export succeeded: %s\n", output_name.c_str());
            } else {
                std::fprintf(stderr, "[vivid] Export failed\n");
            }
        };

        menu_cbs.on_browse_packages = [&]() {
            graph_ui.toggle_package_browser();
        };

        menu_cbs.on_open_package_catalog_website = [&]() {
            const char* env_url = std::getenv("VIVID_PACKAGE_DISCOVERY_URL");
            const std::string url =
                (env_url && env_url[0] != '\0')
                    ? std::string(env_url)
                    : std::string("https://vivid.seethroughlab.com");
            std::string err;
            if (!vivid::open_url(url, &err)) {
                std::fprintf(stderr, "[vivid] Failed to open package catalog URL '%s': %s\n",
                             url.c_str(), err.c_str());
            } else {
                std::fprintf(stderr, "[vivid] Opened package catalog website: %s\n",
                             url.c_str());
            }
        };

        menu_cbs.on_check_for_updates = [&]() {
#ifdef __APPLE__
            std::string err;
            if (vivid::SparkleBridge::available() &&
                vivid::SparkleBridge::check_for_updates(&err)) {
                settings.core_update_last_checked_at = now_epoch_seconds_str();
                vivid::save_settings(settings);
                return;
            }
#endif
            app_updates.refresh();
            settings.core_update_last_checked_at = now_epoch_seconds_str();
            vivid::save_settings(settings);
            std::fprintf(stderr, "[vivid] Checking for core updates via appcast...\n");
        };

        menu_cbs.on_toggle_auto_check_updates = [&]() {
            settings.core_update_auto_check = !settings.core_update_auto_check;
            vivid::save_settings(settings);
            std::fprintf(stderr, "[vivid] Core auto-update checks: %s\n",
                         settings.core_update_auto_check ? "enabled" : "disabled");
        };

        menu_cbs.on_report_issue = [&]() {
            const auto packages = pkg_manager.list();
            const auto operators = registry.type_names();
            const char* graph_path = graph.source_path().empty() ? "<unsaved>" : graph.source_path().c_str();
#ifdef NDEBUG
            const char* build_mode = "Release";
#else
            const char* build_mode = "Debug";
#endif

            std::ostringstream body;
            body << "## What happened?\n";
            body << "<!-- Describe expected vs actual behavior -->\n\n";
            body << "## Steps to reproduce\n";
            body << "1. \n";
            body << "2. \n";
            body << "3. \n\n";
            body << "## Runtime diagnostics\n";
            body << "- Core version: " << VIVID_CORE_VERSION << "\n";
            body << "- Platform: " << platform_label() << "\n";
            body << "- Build mode: " << build_mode << "\n";
            body << "- Graph: " << graph_path << "\n";
            body << "- Registered operator types: " << operators.size() << "\n";
            body << "- Installed packages: " << packages.size() << "\n";
            body << "- Audio enabled: " << (has_audio ? "yes" : "no") << "\n";
            body << "- GPU operators enabled: " << (has_gpu_ops ? "yes" : "no") << "\n";

            const std::string issue_url =
                "https://github.com/seethroughlab/vivid/issues/new"
                "?title=" + url_encode("[Bug] ") +
                "&body=" + url_encode(body.str());

            std::string err;
            if (!vivid::open_url(issue_url, &err)) {
                std::fprintf(stderr, "[vivid] Failed to open issue URL: %s\n", err.c_str());
            } else {
                std::fprintf(stderr, "[vivid] Opened issue reporter URL\n");
            }
        };

        // Edit menu
        menu_cbs.on_delete_selected = [&]() { graph_ui.delete_selected(); };
        menu_cbs.on_edit_meta = [&]() {
            if (graph.source_path().empty()) return;
            vivid::ui::GraphMetaEditData data;
            std::string error;
            if (!load_graph_meta_edit_data(graph.source_path(), data, error)) {
                std::fprintf(stderr, "[vivid] Edit Meta: %s\n", error.c_str());
                return;
            }
            graph_ui.open_graph_meta_editor(data);
        };

        // View menu
        menu_cbs.on_toggle_ui = [&]() { graph_ui.toggle_visible(); };
        menu_cbs.on_toggle_fullscreen = [&]() { toggle_fullscreen(); };
        menu_cbs.on_toggle_bezier_wires = [&]() { graph_ui.set_bezier_wires(!graph_ui.bezier_wires()); };
        menu_cbs.on_toggle_show_param_wires = [&]() { graph_ui.set_show_param_wires(!graph_ui.show_param_wires()); };
        menu_cbs.on_toggle_session_grid = [&]() { graph_ui.toggle_session_grid(); };
        menu_cbs.on_toggle_midi_map = [&]() { graph_ui.toggle_midi_map_mode(); };

        // Insert menu
        menu_cbs.on_add_node = [&]() { graph_ui.open_chooser(); };

        // State queries for checkmarks / enable states
        menu_cbs.is_ui_visible = [&]() { return graph_ui.visible(); };
        menu_cbs.is_fullscreen = [&]() { return display_state.fullscreen; };
        menu_cbs.is_bezier_wires = [&]() { return graph_ui.bezier_wires(); };
        menu_cbs.is_show_param_wires = [&]() { return graph_ui.show_param_wires(); };
        menu_cbs.is_session_grid_open = [&]() { return graph_ui.session_grid_open(); };
        menu_cbs.is_midi_map_mode = [&]() { return graph_ui.midi_map_mode(); };
        menu_cbs.has_selection = [&]() { return graph_ui.has_selection(); };
        menu_cbs.can_edit_meta = [&]() { return !graph.source_path().empty(); };
        menu_cbs.is_auto_check_updates = [&]() { return settings.core_update_auto_check; };

        vivid::macos_setup_menu(menu_cbs);
    }
#endif

    double prev_time = glfwGetTime();
    uint64_t frame_count = 0;
    bool pkg_update_notice_done = false;
    bool core_update_notice_done = false;
    bool synthetic_drop_injected = false;
    UITestDumpState test_dump_state;
    bool test_dump_write_attempted = false;
#ifdef __APPLE__
    bool window_doc_edited = false;
#endif
    std::string window_title_graph_path;
    reset_file_dialog_test_stats_runtime();

    // --- Main loop ---
    auto tick_frame = [&]() -> bool {
        // Guard against reentrancy from nested macOS run loops (e.g. drag-and-drop
        // spins a nested NSRunLoop that fires our CFRunLoopTimer again).
        struct ReentrancyGuard {
            bool& flag;
            bool was_reentrant;
            ReentrancyGuard(bool& f) : flag(f), was_reentrant(f) { flag = true; }
            ~ReentrancyGuard() { if (!was_reentrant) flag = false; }
        };
        static bool in_tick = false;
        ReentrancyGuard guard(in_tick);
        if (guard.was_reentrant) return true;

        // Close button may fire during macOS tracking (resize/menus).
        if (glfwWindowShouldClose(window)) return false;

        const std::string current_graph_path = graph.source_path();
        if (current_graph_path != window_title_graph_path) {
            refresh_window_title(window, current_graph_path);
            window_title_graph_path = current_graph_path;
        }

        int win_w, win_h;
        glfwGetWindowSize(window, &win_w, &win_h);
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        // Fullscreen state is managed by display_state (borderless fullscreen), not GLFW monitor mode.

        const uint64_t monitor_serial = g_monitor_topology_serial.load(std::memory_order_relaxed);
        if (display_state.seen_monitor_serial != monitor_serial) {
            display_state.seen_monitor_serial = monitor_serial;
            if (display_state.fullscreen) {
                if (!monitor_connected(display_state.fullscreen_monitor)) {
                    GLFWmonitor* fallback = glfwGetPrimaryMonitor();
                    if (!fallback) fallback = monitor_for_window(window);
                    if (fallback) {
                        int mx = 0, my = 0;
                        glfwGetMonitorPos(fallback, &mx, &my);
                        const GLFWvidmode* mode = glfwGetVideoMode(fallback);
                        if (!mode) return true;
                        const int mw = mode->width;
                        const int mh = mode->height;
                        glfwSetWindowPos(window, mx, my);
                        glfwSetWindowSize(window, mw, mh);
                        display_state.fullscreen_monitor = fallback;
                        display_state.surface_reconfigure_pending = true;
                        display_state.surface_settle_frames = 2;
                        std::fprintf(stderr, "[vivid] Rebound fullscreen to active monitor (%dx%d at %d,%d)\n",
                                     mw, mh, mx, my);
                    }
                }
            } else {
                int x = 0, y = 0, w = 0, h = 0;
                glfwGetWindowPos(window, &x, &y);
                glfwGetWindowSize(window, &w, &h);
                bool on_screen = false;
                int mon_count = 0;
                GLFWmonitor** monitors = glfwGetMonitors(&mon_count);
                for (int i = 0; i < mon_count; ++i) {
                    int mx = 0, my = 0, mw = 0, mh = 0;
                    glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
                    if (x + 100 > mx && x < mx + mw && y + 100 > my && y < my + mh) {
                        on_screen = true;
                        break;
                    }
                }
                if (!on_screen) {
                    clamp_window_rect_to_monitor(glfwGetPrimaryMonitor(), &x, &y, &w, &h);
                    glfwSetWindowPos(window, x, y);
                    glfwSetWindowSize(window, w, h);
                    display_state.surface_reconfigure_pending = true;
                    display_state.surface_settle_frames = 2;
                    std::fprintf(stderr, "[vivid] Repositioned window after display change (%dx%d at %d,%d)\n",
                                 w, h, x, y);
                }
            }
        }

        // Skip frame if minimized
        if (fb_w == 0 || fb_h == 0) return true;

        if (!test_drop_path.empty() &&
            !synthetic_drop_injected &&
            static_cast<int>(frame_count) >= test_drop_frame &&
            window_user_data.pending_drop_path.empty()) {
            std::filesystem::path drop_path(test_drop_path);
            if (!drop_path.is_absolute()) drop_path = std::filesystem::absolute(drop_path);
            if (!std::filesystem::exists(drop_path)) {
                std::fprintf(stderr,
                             "[vivid] Test drop fixture missing: %s\n",
                             drop_path.string().c_str());
            } else {
                float drop_x = static_cast<float>(win_w) * 0.5f;
                float drop_y = static_cast<float>(win_h) * 0.5f;
                if (test_drop_screen_pos.size() == 2) {
                    drop_x = static_cast<float>(test_drop_screen_pos[0]);
                    drop_y = static_cast<float>(test_drop_screen_pos[1]);
                }
                window_user_data.raw_mouse_x = drop_x;
                window_user_data.raw_mouse_y = drop_y;
                window_user_data.pending_drop_path = drop_path.string();
                std::fprintf(stderr,
                             "[vivid] Test drop injected: %s at (%.1f, %.1f) on frame %llu\n",
                             window_user_data.pending_drop_path.c_str(),
                             static_cast<double>(drop_x),
                             static_cast<double>(drop_y),
                             static_cast<unsigned long long>(frame_count));
            }
            synthetic_drop_injected = true;
        }

        // Handle drag-and-drop graph loading / file-to-operator creation
        if (!window_user_data.pending_drop_path.empty()) {
            std::string path = std::move(window_user_data.pending_drop_path);
            window_user_data.pending_drop_path.clear();
            std::string ext = std::filesystem::path(path).extension().string();
            for (auto& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (ext == ".json") {
                load_graph_runtime(path, "Drop");
            } else {
                refresh_file_drop_registry();
                auto matches = file_drop_registry.matches_for_path(path);
                if (matches.empty()) {
                    std::fprintf(stderr, "[vivid] Drop: no operator registered for %s\n", path.c_str());
                } else {
                    float graph_x = 0.0f, graph_y = 0.0f;
                    if (!graph_ui.graph_position_for_screen(
                            static_cast<float>(window_user_data.raw_mouse_x),
                            static_cast<float>(window_user_data.raw_mouse_y),
                            graph_x, graph_y)) {
                        graph_ui.graph_center_position(graph_x, graph_y);
                    }

                    if (matches.size() == 1) {
                        create_file_drop_node(matches.front(), path, graph_x, graph_y);
                    } else {
                        std::vector<vivid::ui::FileDropChooserAction> actions;
                        actions.reserve(matches.size());
                        for (const auto& match : matches) {
                            vivid::ui::FileDropChooserAction action;
                            action.label = match.label;
                            action.subtitle = match.package_name.empty()
                                ? match.type_name
                                : (match.type_name + "  [" + match.package_name + "]");
                            action.type_name = match.type_name;
                            action.file_param = match.file_param;
                            action.dropped_path = path;
                            actions.push_back(std::move(action));
                        }
                        graph_ui.open_file_drop_chooser(std::move(actions), graph_x, graph_y);
                    }
                }
            }
            // Suppress surface presentation for a few frames after a drop.
            // The drop callback fires during glfwPollEvents(), and macOS may
            // still be inside NSCoreDragReceiveMessageProc when our CFRunLoop
            // timer fires this tick.  The window surface can be transiently
            // invalid during drag-tracking runloops, so we avoid begin_frame /
            // end_frame (and the fatal wgpuQueueSubmit) until the surface
            // stabilises.  Reuses the same settle mechanism as resize/fullscreen.
            display_state.surface_settle_frames = std::max(display_state.surface_settle_frames, 3);
        }

        // Reconfigure GPU surface if framebuffer size changed.
        if (fb_w != fb_width || fb_h != fb_height) {
            fb_width = fb_w;
            fb_height = fb_h;
            gpu.resize(static_cast<uint32_t>(fb_width), static_cast<uint32_t>(fb_height));
            display_state.surface_settle_frames = std::max(display_state.surface_settle_frames, 2);
            return true;
        }

        // Drain control server requests (may set pending topology changes)
        control_server.process_requests(runtime_api, graph, runtime, registry,
                                        has_gpu_ops, has_audio);
        static uint64_t last_reload_serial = 0;
        if (runtime_api.reload_serial() != last_reload_serial) {
            last_reload_serial = runtime_api.reload_serial();
            if (!runtime_api.consume_preserve_undo_history_reload()) {
                command_sink.reset_undo_history();
            }
        }

        if (runtime_api.has_pending()) {
            runtime_api.apply_pending(has_gpu_ops, has_audio);
            // Re-allocate per-node GPU textures after topology change
            if (has_gpu_ops) {
                runtime.allocate_gpu_textures(gpu.device(), kDefaultTexW, kDefaultTexH, kOffscreenFormat);
            }
            video_out_idx = has_gpu_ops ? runtime.find_effective_gpu_sink() : -1;
            capture_coordinator.set_audio_engine(has_audio ? &audio_engine : nullptr);
            // Evict thumbnail cache entries for removed nodes
            if (auto* cg_evict = runtime.compiled_graph()) {
                std::unordered_set<std::string> active_ids;
                for (const auto& cn : cg_evict->nodes)
                    active_ids.insert(cn.node_id);
                thumb_cache.retain_only(active_ids);
            }
        }
        // Handle GPU realloc after reload command or operator-requested resize
        if (runtime_api.needs_gpu_realloc() || runtime.needs_gpu_realloc()) {
            runtime_api.clear_gpu_realloc();
            runtime.clear_gpu_realloc();
            runtime.allocate_gpu_textures(gpu.device(), kDefaultTexW, kDefaultTexH, kOffscreenFormat);
            video_out_idx = has_gpu_ops ? runtime.find_effective_gpu_sink() : -1;
        }
        if (!graph_loaded && runtime.compiled_graph() && !runtime.compiled_graph()->nodes.empty()) {
            graph_loaded = true;
        }

#ifdef __APPLE__
        bool now_dirty = runtime_api.graph_dirty();
        if (now_dirty != window_doc_edited) {
            vivid::macos_set_document_edited(now_dirty);
            window_doc_edited = now_dirty;
        }
#endif

        // Drive output window from video_out "launch" param.
        if (has_gpu_ops && video_out_idx >= 0 && runtime.compiled_graph() &&
            static_cast<size_t>(video_out_idx) < runtime.compiled_graph()->nodes.size()) {
            const auto& vo_cn = runtime.compiled_graph()->nodes[video_out_idx];
            auto launch_it = vo_cn.param_indices.find("launch");
            if (launch_it != vo_cn.param_indices.end() &&
                launch_it->second < vo_cn.param_values.size()) {
                const bool want_launch = vo_cn.param_values[launch_it->second] >= 0.5f;
                int target = 0; // Current monitor
                auto dt_it = vo_cn.param_indices.find("display_target");
                if (dt_it != vo_cn.param_indices.end() &&
                    dt_it->second < vo_cn.param_values.size()) {
                    target = static_cast<int>(vo_cn.param_values[dt_it->second]);
                    if (target < 0) target = 0;
                    if (target > 2) target = 2;
                }

                if (want_launch) {
                    GLFWmonitor* target_monitor = monitor_for_target(target, window);
                    if (!output_window.is_open()) {
                        output_window.open(gpu.instance(), gpu.adapter(), gpu.device(), gpu.queue(), target_monitor);
                    } else if (target != display_state.sink_target &&
                               monitor_connected(target_monitor)) {
                        output_window.move_to_monitor(target_monitor);
                    }
                } else if (output_window.is_open()) {
                    output_window.close();
                }

                // Handle ESC / window close on output window
                if (output_window.should_close()) {
                    output_window.close();
                    if (auto* cg = runtime.compiled_graph()) {
                        auto& cn = cg->nodes[video_out_idx];
                        cn.param_values[launch_it->second] = 0.0f;
                        cn.dirty = true;
                    }
                }

                display_state.sink_target = target;
            }
        }

        if (display_state.surface_reconfigure_pending) {
            glfwGetWindowSize(window, &win_w, &win_h);
            glfwGetFramebufferSize(window, &fb_w, &fb_h);
            if (fb_w > 0 && fb_h > 0) {
                fb_width = fb_w;
                fb_height = fb_h;
                gpu.resize(static_cast<uint32_t>(fb_width), static_cast<uint32_t>(fb_height));
            }
            display_state.surface_reconfigure_pending = false;
        }
        bool suppress_surface_frame = false;
        if (display_state.surface_settle_frames > 0) {
            suppress_surface_frame = true;
            display_state.surface_settle_frames--;
        }

        // --- Compute dt unconditionally (before GPU work) ---
        double now = glfwGetTime();
        double dt = now - prev_time;
        prev_time = now;
        graph_ui.set_dt(static_cast<float>(dt));

        // Non-intrusive startup update alert (logs once, never blocks startup).
        if (!pkg_update_notice_done) {
            auto state = pkg_catalog.fetch_state();
            if (state == vivid::CatalogFetchState::Ready) {
                auto summary = pkg_catalog.summarize_updates(VIVID_CORE_VERSION);
                if (summary.updates_available > 0) {
                    std::fprintf(stderr,
                        "[vivid] Package updates available: %d (%d incompatible). "
                        "Run `vivid package-check-updates` for details.\n",
                        summary.updates_available, summary.incompatible_updates);
                }
                pkg_update_notice_done = true;
            } else if (state == vivid::CatalogFetchState::Error) {
                std::fprintf(stderr, "[vivid] Package update check unavailable (non-fatal): %s\n",
                             pkg_catalog.fetch_error().c_str());
                pkg_update_notice_done = true;
            }
        }

        // Non-intrusive startup core update alert.
        if (!core_update_notice_done && settings.core_update_auto_check) {
            auto st = app_updates.fetch_state();
            if (st == vivid::AppUpdateFetchState::Ready) {
                auto info = app_updates.latest();
                settings.core_update_last_checked_at = now_epoch_seconds_str();
                if (info.update_available && !app_updates.is_skipped(info.latest_version)) {
                    std::fprintf(stderr,
                        "[vivid] Core update available: %s -> %s. "
                        "Use File -> Check for Updates... for installer flow.\n",
                        info.current_version.c_str(), info.latest_version.c_str());
                    graph_ui.show_core_update_notice(info.latest_version, info.title);
                }
                core_update_notice_done = true;
            } else if (st == vivid::AppUpdateFetchState::Error) {
                settings.core_update_last_checked_at = now_epoch_seconds_str();
                std::fprintf(stderr, "[vivid] Core update check unavailable (non-fatal): %s\n",
                             app_updates.fetch_error().c_str());
                core_update_notice_done = true;
            }
        }

        // --- Apply MIDI mappings (before tick so wire wins on conflict) ---
        runtime_api.apply_midi_mappings();

        // --- Tick quantized variation switching ---
        runtime_api.tick_quantized_switch();

        // --- Try to acquire surface texture for presentation ---
        vivid::FrameState frame;
        bool have_surface = !suppress_surface_frame && gpu.begin_frame(frame);

        // If no surface (e.g. during resize), create a standalone encoder
        // so offscreen GPU work (runtime tick, thumbnails) still runs.
        WGPUCommandEncoder tick_encoder = nullptr;
        if (have_surface) {
            tick_encoder = frame.encoder;
        } else {
            WGPUCommandEncoderDescriptor enc_desc{};
            enc_desc.label = to_sv("Offscreen Tick Encoder");
            tick_encoder = wgpuDeviceCreateCommandEncoder(gpu.device(), &enc_desc);
        }

        // --- Tick graph (always runs, even without a surface) ---
        if (graph_loaded) {

            // Base GPU state (per-node textures are set by runtime)
            VividGpuContext gpu_state{};
            gpu_state.device          = gpu.device();
            gpu_state.queue           = gpu.queue();
            gpu_state.command_encoder = tick_encoder;
            gpu_state.output_format   = kOffscreenFormat;

            // --- Hot-reload polling ---
            if (hot_reload_enabled) {
                auto now_scan = std::chrono::steady_clock::now();
                if (now_scan >= next_package_watch_rescan_at) {
                    refresh_package_watches();
                    next_package_watch_rescan_at = now_scan + std::chrono::seconds(1);
                }
                poll_hot_reload(file_watcher, hot_reloader, runtime, registry,
                                audio_engine, has_audio, &op_info_cache,
                                runtime.operators_src_dir());
            }

            if (has_audio)
                runtime.pre_tick_audio_sync(now);

            // --- Build input state for operators (when UI hidden) ---
            const VividInputState* input_ptr = nullptr;
            VividInputState input_state{};
            if (!window_user_data.pending_events.empty() ||
                (window_user_data.buttons_held && !(graph_ui.visible()))) {
                // Compute inverse blit_fit transform: window coords → [0,1] texture UV
                float scale_x = 1.0f, scale_y = 1.0f;
                float offset_x = 0.0f, offset_y = 0.0f;
                if (has_gpu_ops && video_out_idx >= 0 && fb_width > 0 && fb_height > 0) {
                    uint32_t src_w = 0, src_h = 0;
                    runtime.gpu_sink_source_size(video_out_idx, src_w, src_h);
                    if (src_w > 0 && src_h > 0) {
                        const auto& vo_cn = runtime.compiled_graph()->nodes[video_out_idx];
                        auto fit_mode = vivid::FitMode::Fit;
                        auto fm_it = vo_cn.param_indices.find("fit_mode");
                        if (fm_it != vo_cn.param_indices.end() && fm_it->second < vo_cn.param_values.size())
                            fit_mode = static_cast<vivid::FitMode>(
                                static_cast<int>(vo_cn.param_values[fm_it->second]));

                        float src_aspect = static_cast<float>(src_w) / static_cast<float>(src_h);
                        float dst_aspect = static_cast<float>(fb_width) / static_cast<float>(fb_height);

                        if (fit_mode == vivid::FitMode::Stretch) {
                            scale_x = 1.0f; scale_y = 1.0f;
                        } else if (fit_mode == vivid::FitMode::Fit) {
                            if (src_aspect > dst_aspect) {
                                scale_x = 1.0f; scale_y = dst_aspect / src_aspect;
                            } else {
                                scale_x = src_aspect / dst_aspect; scale_y = 1.0f;
                            }
                        } else { // Fill
                            if (src_aspect > dst_aspect) {
                                scale_x = src_aspect / dst_aspect; scale_y = 1.0f;
                            } else {
                                scale_x = 1.0f; scale_y = dst_aspect / src_aspect;
                            }
                        }
                        offset_x = (1.0f - scale_x) * 0.5f;
                        offset_y = (1.0f - scale_y) * 0.5f;
                    }
                }

                // Normalize mouse coords in all pending events: window px → [0,1] texture UV
                // ndc = cursor_pos / win_size;  tex_uv = (ndc - offset) / scale
                float inv_w = (win_w > 0) ? 1.0f / static_cast<float>(win_w) : 0.0f;
                float inv_h = (win_h > 0) ? 1.0f / static_cast<float>(win_h) : 0.0f;
                float inv_sx = (scale_x > 0.0f) ? 1.0f / scale_x : 0.0f;
                float inv_sy = (scale_y > 0.0f) ? 1.0f / scale_y : 0.0f;

                for (auto& ev : window_user_data.pending_events) {
                    float ndc_x = ev.mouse_x * inv_w;
                    float ndc_y = ev.mouse_y * inv_h;
                    ev.mouse_x = (ndc_x - offset_x) * inv_sx;
                    ev.mouse_y = (ndc_y - offset_y) * inv_sy;
                }

                float cur_ndc_x = static_cast<float>(window_user_data.raw_mouse_x) * inv_w;
                float cur_ndc_y = static_cast<float>(window_user_data.raw_mouse_y) * inv_h;

                input_state.events = window_user_data.pending_events.data();
                input_state.event_count = static_cast<uint32_t>(window_user_data.pending_events.size());
                input_state.mouse_x = (cur_ndc_x - offset_x) * inv_sx;
                input_state.mouse_y = (cur_ndc_y - offset_y) * inv_sy;
                input_state.buttons_held = window_user_data.buttons_held;
                input_state.modifiers = window_user_data.current_mods;
                input_ptr = &input_state;
            }

            // Tick with thumbnail capture callback for GPU nodes
            runtime.tick(now, dt, frame_count, &gpu_state,
                [&](uint32_t, const std::string& node_id, WGPUTextureView node_tex_view) {
                    // Blit per-node texture → thumbnail (uses RGBA16Float pipeline)
                    if (!node_tex_view) return;
                    auto* thumb_view = thumb_cache.get_or_create(node_id);
                    if (thumb_view) {
                        thumb_blit.blit(tick_encoder, node_tex_view, thumb_view);
                    }
                },
                input_ptr);

            // Clear consumed input events
            window_user_data.pending_events.clear();

            draw_custom_thumbnails(runtime, thumb_cache, graph_ui,
                                   gpu.device(), gpu.queue(), tick_encoder,
                                   now, dt, frame_count, kThumbW, kThumbH, kOffscreenFormat);

            // --- Tick state-preset mappings (after runtime tick, state outputs are fresh) ---
            runtime_api.tick_state_presets();

            if (has_audio)
                runtime.post_tick_audio_sync();

            // Process capture/recording/analysis requests (after tick, textures are fresh)
            if (capture_coordinator.has_pending() || capture_coordinator.is_recording() ||
                capture_coordinator.has_pending_analyses()) {
                WGPUTexture cap_tex = nullptr;
                uint32_t cap_w = 0, cap_h = 0;
                if (has_gpu_ops && video_out_idx >= 0) {
                    // Find the source node's texture (upstream of video_out)
                    cap_tex = runtime.gpu_sink_source_texture(video_out_idx);
                    runtime.gpu_sink_source_size(video_out_idx, cap_w, cap_h);
                }
                if (capture_coordinator.has_pending())
                    capture_coordinator.process_pending(
                        gpu.device(), gpu.queue(), cap_tex, cap_w, cap_h);
                if (capture_coordinator.is_recording())
                    capture_coordinator.tick_recording(
                        gpu.device(), gpu.queue(), cap_tex, cap_w, cap_h);
                capture_coordinator.tick_analysis(
                    gpu.device(), gpu.queue(), cap_tex, cap_w, cap_h);
            }

            if (frame_count % 60 == 0) {
                std::fprintf(stderr, "[vivid] frame=%llu",
                    static_cast<unsigned long long>(frame_count));
                if (auto* cg_dbg = runtime.compiled_graph()) {
                for (const auto& cn : cg_dbg->nodes) {
                    for (const auto& [port_name, port_idx] : cn.output_port_indices) {
                        std::fprintf(stderr, " | %s/%s=%.4f",
                            cn.node_id.c_str(), port_name.c_str(),
                            cn.output_values[port_idx]);
                    }
                }
                }
                std::fprintf(stderr, "\n");
            }
            frame_count++;
        }

        if (have_surface) {
            // Poll async package action completion (main thread only).
            {
                bool done = false, needs_refresh = false;
                std::string err;
                {
                    std::lock_guard<std::mutex> lk(pkg_action_mutex);
                    if (pkg_action_state == PkgActionState::Done ||
                        pkg_action_state == PkgActionState::Error) {
                        done = true;
                        needs_refresh = pkg_action_needs_refresh;
                        err = pkg_action_error_msg;
                        pkg_action_state = PkgActionState::Idle;
                        pkg_action_needs_refresh = false;
                    }
                }
                if (done) {
                    if (needs_refresh) {
                        refresh_discovered_examples();
                        registry.load_for_graph(graph);
                        if (rebuild_live_runtime_from_graph()) {
                            if (auto* cg_pkg = runtime.compiled_graph()) {
                                std::unordered_set<std::string> active_ids;
                                for (const auto& cn : cg_pkg->nodes) {
                                    active_ids.insert(cn.node_id);
                                }
                                thumb_cache.retain_only(active_ids);
                            }
                        }
                    }
                    refresh_package_browser_entries_cache();
                    graph_ui.notify_pkg_action_complete(needs_refresh, err);
                }
            }

            // --- Surface presentation path ---
            if (has_gpu_ops && video_out_idx >= 0) {
                // Find video_out's input texture from its resolved_tex_inputs
                const auto& vo_cn = runtime.compiled_graph()->nodes[video_out_idx];
                WGPUTextureView display_tex = nullptr;
                uint32_t src_w = 0, src_h = 0;
                if (vo_cn.gpu && !vo_cn.gpu->resolved_tex_inputs.empty()) {
                    display_tex = vo_cn.gpu->resolved_tex_inputs[0];
                    runtime.gpu_sink_source_size(video_out_idx, src_w, src_h);
                }

                if (display_tex && src_w > 0 && src_h > 0) {
                    auto fit_mode = vivid::FitMode::Fit;
                    auto fm_it = vo_cn.param_indices.find("fit_mode");
                    if (fm_it != vo_cn.param_indices.end() && fm_it->second < vo_cn.param_values.size())
                        fit_mode = static_cast<vivid::FitMode>(static_cast<int>(vo_cn.param_values[fm_it->second]));
                    bool ui_vis = graph_ui.visible();
                    blit.blit_fit(frame.encoder, display_tex, frame.view,
                                  src_w, src_h,
                                  static_cast<uint32_t>(fb_width),
                                  static_cast<uint32_t>(fb_height),
                                  fit_mode, ui_vis);

                    // Present to output window (separate surface, own encoder)
                    if (output_window.is_open()) {
                        output_window.present(display_tex, src_w, src_h, fit_mode);
                    }
                } else {
                    emit_clear_pass(frame.encoder, frame.view, clear);
                }

            } else {
                emit_clear_pass(frame.encoder, frame.view, clear);
            }

            const bool interface_capture_requested =
                capture_coordinator.has_pending_interface_capture() ||
                capture_coordinator.has_active_interface_capture();
            if (interface_capture_requested && !gpu.surface_supports_copy_src()) {
                capture_coordinator.fail_pending_interface_captures(
                    "surface does not support interface capture");
            }
            if (interface_capture_requested && !text_renderer_ok) {
                capture_coordinator.fail_pending_interface_captures(
                    "UI text renderer is unavailable");
            }

            // --- Node graph UI overlay (2-pass rendering) ---
            if (text_renderer_ok && (graph_ui.visible() || interface_capture_requested)) {
                auto snapshot = build_graph_snapshot(
                    graph, runtime, has_audio ? &audio_engine : nullptr,
                    registry, op_info_cache, &system_midi, &runtime_api,
                    &capture_coordinator, &control_server);

                if (!test_ui_script.actions.empty()) {
                    run_ui_test_script_frame(test_ui_script, graph_ui, window_user_data,
                                             screenshot_path, screenshot_delay, frame_count);
                }
                graph_ui.update(snapshot);
                if (!test_dump_ui_state_path.empty()) {
                    test_dump_state.final_state =
                        capture_ui_test_observed_state(snapshot, graph_ui);
                    test_dump_state.has_final_state = true;
                    for (const auto& label : test_ui_script.pending_checkpoint_labels) {
                        test_dump_state.checkpoints.push_back(
                            UITestCheckpointState{
                                label,
                                capture_ui_test_observed_state(snapshot, graph_ui),
                            });
                    }
                    test_ui_script.pending_checkpoint_labels.clear();
                }
                if (interface_capture_requested) {
                    capture_coordinator.prepare_pending_interface_capture(graph_ui);
                }
                if (!screenshot_select_applied && !screenshot_select_node.empty()) {
                    if (graph_ui.select_single_node_for_review(screenshot_select_node)) {
                        screenshot_select_applied = true;
                    } else if (!screenshot_select_warned) {
                        std::fprintf(stderr,
                                     "[vivid] --select-node could not find node id '%s' in the current graph yet\n",
                                     screenshot_select_node.c_str());
                        screenshot_select_warned = true;
                    }
                }
                if (graph_ui.visible()) {
                    graph_ui.draw(text_renderer, static_cast<uint32_t>(win_w), static_cast<uint32_t>(win_h));
                    // Pass 1: text/rects
                    text_renderer.flush(frame.encoder, frame.view, static_cast<uint32_t>(win_w), static_cast<uint32_t>(win_h));
                    // Pass 2: thumbnails (GPU auto-captured + CPU custom, composited over text)
                    if (thumb_renderer_ok) {
                        graph_ui.draw_thumbnails(thumb_renderer, thumb_cache,
                                                 frame.encoder, frame.view,
                                                 static_cast<uint32_t>(fb_width),
                                                 static_cast<uint32_t>(fb_height));
                    }
                    // Pass 3: overlays (context menu, dropdown) on top of thumbnails
                    graph_ui.draw_overlays(text_renderer);
                    text_renderer.flush(frame.encoder, frame.view, static_cast<uint32_t>(win_w), static_cast<uint32_t>(win_h));
                }
            }

            bool frame_already_submitted = false;
            if (capture_coordinator.has_active_interface_capture()) {
                SurfaceCaptureResult interface_capture;
                std::string capture_error;
                if (capture_surface_png(gpu, frame, fb_width, fb_height,
                                        interface_capture, capture_error)) {
                    capture_coordinator.complete_active_interface_capture(
                        interface_capture.width, interface_capture.height, interface_capture.png_data);
                } else {
                    capture_coordinator.fail_active_interface_capture(capture_error);
                }
                frame_already_submitted = true;
            }

            // --- Screenshot capture ---
            if (!frame_already_submitted &&
                try_capture_screenshot(screenshot_path, gpu, frame, fb_width, fb_height,
                                       frame_count, screenshot_delay, window)) {
                return true; // frame already submitted inside try_capture_screenshot
            }

            int fb_now_w = 0, fb_now_h = 0;
            glfwGetFramebufferSize(window, &fb_now_w, &fb_now_h);
            if (fb_now_w != fb_width || fb_now_h != fb_height || fb_now_w == 0 || fb_now_h == 0) {
                if (fb_now_w > 0 && fb_now_h > 0) {
                    fb_width = fb_now_w;
                    fb_height = fb_now_h;
                    gpu.resize(static_cast<uint32_t>(fb_width), static_cast<uint32_t>(fb_height));
                }
                display_state.surface_settle_frames = std::max(display_state.surface_settle_frames, 2);
                if (!frame_already_submitted)
                    gpu.discard_frame(frame);
                return true;
            }

            if (!frame_already_submitted)
                gpu.end_frame(frame);
        } else {
            if (capture_coordinator.has_pending_interface_capture() ||
                capture_coordinator.has_active_interface_capture()) {
                capture_coordinator.fail_pending_interface_captures(
                    "no capturable surface available");
            }
            // No surface — submit offscreen GPU work (runtime tick, thumbnails)
            // and poll the device so audio/compute operators still advance.
            vivid::gpu_submit(gpu.device(), gpu.queue(), tick_encoder, "Offscreen Commands");
        }

        // wgpu-native: poll the device to process async operations
        wgpuDevicePoll(gpu.device(), false, nullptr);
        return true;
    };

#ifdef __APPLE__
    auto poll_events = [&]() -> bool {
        glfwPollEvents();
        return !glfwWindowShouldClose(window);
    };
    vivid::macos_run_frame_loop(poll_events, tick_frame);
#else
    while (true) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) break;
        if (!tick_frame()) break;
    }
#endif

    if (!test_dump_ui_state_path.empty() && !test_dump_write_attempted) {
        std::filesystem::create_directories(
            std::filesystem::path(test_dump_ui_state_path).parent_path());
        std::string dump_error;
        if (write_ui_test_dump_file(test_dump_ui_state_path, test_dump_state, dump_error)) {
            std::fprintf(stderr, "[vivid] UI test dump saved: %s\n",
                         test_dump_ui_state_path.c_str());
        } else {
            std::fprintf(stderr, "[vivid] UI test dump FAILED: %s (%s)\n",
                         test_dump_ui_state_path.c_str(), dump_error.c_str());
        }
        test_dump_write_attempted = true;
    }

    // --- Shutdown ---
    if (pkg_action_thread.joinable()) pkg_action_thread.join();
    system_midi.close();
    control_server.stop();
    if (hot_reload_enabled) {
        file_watcher.stop();
        hot_reloader.stop();
    }
    if (has_audio) {
        audio_engine.shutdown();
    }
    if (graph_loaded) {
        runtime.shutdown();
    }

    if (text_renderer_ok) {
        text_renderer.shutdown();
    }
    thumb_renderer.shutdown();
    thumb_cache.shutdown();
    thumb_blit.shutdown();
    blit.shutdown();
    output_window.close();
    gpu.shutdown();

    // Save window geometry for next launch
    {
        vivid::Settings s = settings;  // preserve editor/style prefs
        glfwGetWindowPos(window, &s.window_x, &s.window_y);
        glfwGetWindowSize(window, &s.window_width, &s.window_height);
        s.bezier_wires = graph_ui.bezier_wires();
        s.show_param_wires = graph_ui.show_param_wires();
        vivid::save_settings(s);
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    std::fprintf(stderr, "[vivid] Clean shutdown\n");
    return 0;
}
