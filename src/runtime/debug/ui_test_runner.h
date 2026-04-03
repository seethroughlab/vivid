#pragma once

#include "ui/dialogs/file_dialog.h"
#include "ui/graph/graph_snapshot.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid::ui {
class NodeGraphUI;
}

namespace vivid {

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
    int button = 0;
    int mouse_action = 0;
    int key = 0;
    int key_action = 0;
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

bool load_ui_test_script(const std::string& script_path,
                         UITestScript& script,
                         std::string& error);

void reset_file_dialog_test_stats_runtime();

UITestObservedState capture_ui_test_observed_state(
    const vivid::ui::GraphSnapshot& snapshot,
    const vivid::ui::NodeGraphUI& graph_ui);

bool write_ui_test_dump_file(const std::string& path,
                             const UITestDumpState& dump,
                             std::string& error);

} // namespace vivid
