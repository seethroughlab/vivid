#include "runtime/debug/ui_test_runner.h"

#include "ui/dialogs/file_dialog.h"
#include "ui/graph/node_graph.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace vivid {

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
        error = "modifiers must be an array";
        return false;
    }
    for (const auto& item : arr) {
        if (!item.is_string()) {
            error = "modifier entries must be strings";
            return false;
        }
        const std::string mod = lower_copy(item.get<std::string>());
        if (mod == "shift") mods |= GLFW_MOD_SHIFT;
        else if (mod == "ctrl" || mod == "control") mods |= GLFW_MOD_CONTROL;
        else if (mod == "alt" || mod == "option") mods |= GLFW_MOD_ALT;
        else if (mod == "super" || mod == "cmd" || mod == "command") mods |= GLFW_MOD_SUPER;
        else {
            error = "unknown modifier: " + mod;
            return false;
        }
    }
    return true;
}

static bool parse_ui_test_mouse_button(const std::string& button_name, int& button) {
    const std::string name = lower_copy(button_name);
    if (name == "left") { button = GLFW_MOUSE_BUTTON_LEFT; return true; }
    if (name == "right") { button = GLFW_MOUSE_BUTTON_RIGHT; return true; }
    if (name == "middle") { button = GLFW_MOUSE_BUTTON_MIDDLE; return true; }
    return false;
}

static bool parse_ui_test_action_name(const std::string& action_name, int& action) {
    const std::string name = lower_copy(action_name);
    if (name == "press" || name == "down") { action = GLFW_PRESS; return true; }
    if (name == "release" || name == "up") { action = GLFW_RELEASE; return true; }
    if (name == "repeat") { action = GLFW_REPEAT; return true; }
    return false;
}

static bool parse_ui_test_key_name(const std::string& key_name, int& key) {
    const std::string name = lower_copy(key_name);
    if (name.size() == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') {
            key = GLFW_KEY_A + static_cast<int>(c - 'a');
            return true;
        }
        if (c >= '0' && c <= '9') {
            key = GLFW_KEY_0 + static_cast<int>(c - '0');
            return true;
        }
    }

    static const std::pair<const char*, int> kNamedKeys[] = {
        {"enter", GLFW_KEY_ENTER}, {"return", GLFW_KEY_ENTER},
        {"escape", GLFW_KEY_ESCAPE}, {"esc", GLFW_KEY_ESCAPE},
        {"tab", GLFW_KEY_TAB}, {"space", GLFW_KEY_SPACE},
        {"backspace", GLFW_KEY_BACKSPACE}, {"delete", GLFW_KEY_DELETE},
        {"up", GLFW_KEY_UP}, {"down", GLFW_KEY_DOWN},
        {"left", GLFW_KEY_LEFT}, {"right", GLFW_KEY_RIGHT},
        {"home", GLFW_KEY_HOME}, {"end", GLFW_KEY_END},
        {"pageup", GLFW_KEY_PAGE_UP}, {"pagedown", GLFW_KEY_PAGE_DOWN},
        {"comma", GLFW_KEY_COMMA}, {"period", GLFW_KEY_PERIOD},
        {"minus", GLFW_KEY_MINUS}, {"equal", GLFW_KEY_EQUAL},
        {"grave", GLFW_KEY_GRAVE_ACCENT},
    };
    for (const auto& [label, code] : kNamedKeys) {
        if (name == label) {
            key = code;
            return true;
        }
    }
    return false;
}

bool load_ui_test_script(const std::string& script_path,
                         UITestScript& script,
                         std::string& error) {
    script = {};
    script.source_dir = std::filesystem::absolute(std::filesystem::path(script_path)).parent_path();

    nlohmann::json root;
    try {
        std::ifstream in(script_path);
        if (!in) {
            error = "failed to read UI test script";
            return false;
        }
        in >> root;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    if (!root.is_object() || !root.contains("actions") || !root["actions"].is_array()) {
        error = "UI test script must contain an 'actions' array";
        return false;
    }

    for (const auto& item : root["actions"]) {
        if (!item.is_object()) {
            error = "action must be an object";
            return false;
        }
        auto type_it = item.find("type");
        if (type_it == item.end() || !type_it->is_string()) {
            error = "action missing string type";
            return false;
        }

        const std::string type = lower_copy(type_it->get<std::string>());
        UITestAction action;
        if (type == "wait") {
            action.type = UITestActionType::Wait;
            auto frames_it = item.find("frames");
            action.frames = (frames_it != item.end() && frames_it->is_number_integer())
                ? frames_it->get<int>() : 1;
        } else if (type == "mouse_move") {
            auto x_it = item.find("x");
            auto y_it = item.find("y");
            if (x_it == item.end() || y_it == item.end() ||
                !x_it->is_number() || !y_it->is_number()) {
                error = "mouse_move requires numeric x/y";
                return false;
            }
            action.type = UITestActionType::MouseMove;
            action.x = x_it->get<float>();
            action.y = y_it->get<float>();
        } else if (type == "mouse_button") {
            auto button_it = item.find("button");
            auto action_it = item.find("action");
            auto mods_it = item.find("mods");
            if (button_it == item.end() || action_it == item.end() ||
                !button_it->is_string() || !action_it->is_string()) {
                error = "mouse_button requires string button/action";
                return false;
            }
            if (!parse_ui_test_mouse_button(button_it->get<std::string>(), action.button) ||
                !parse_ui_test_action_name(action_it->get<std::string>(), action.mouse_action) ||
                !parse_ui_test_modifiers(mods_it != item.end() ? *mods_it : nlohmann::json(), action.mods, error)) {
                if (error.empty()) error = "invalid mouse_button action";
                return false;
            }
            action.type = UITestActionType::MouseButton;
        } else if (type == "key") {
            auto key_it = item.find("key");
            auto action_it = item.find("action");
            auto mods_it = item.find("mods");
            if (key_it == item.end() || action_it == item.end() ||
                !key_it->is_string() || !action_it->is_string()) {
                error = "key action requires string key/action";
                return false;
            }
            if (!parse_ui_test_key_name(key_it->get<std::string>(), action.key) ||
                !parse_ui_test_action_name(action_it->get<std::string>(), action.key_action) ||
                !parse_ui_test_modifiers(mods_it != item.end() ? *mods_it : nlohmann::json(), action.mods, error)) {
                if (error.empty()) error = "invalid key action";
                return false;
            }
            action.type = UITestActionType::Key;
        } else if (type == "char") {
            auto value_it = item.find("value");
            if (value_it == item.end() || !value_it->is_string() || value_it->get<std::string>().empty()) {
                error = "char requires non-empty string value";
                return false;
            }
            action.type = UITestActionType::CharInput;
            action.codepoint = static_cast<unsigned int>(value_it->get<std::string>()[0]);
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

void reset_file_dialog_test_stats_runtime() {
#ifdef __APPLE__
    vivid::ui::reset_file_dialog_test_stats();
#endif
}

UITestObservedState capture_ui_test_observed_state(
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
    for (const auto& node_id : state.selected_node_ids)
        selected.push_back(sanitize_json_string(node_id));
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

bool write_ui_test_dump_file(const std::string& path,
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

} // namespace vivid
