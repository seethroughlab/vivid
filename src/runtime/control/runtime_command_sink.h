#pragma once

#include "ui/ui_command_sink.h"
#include "runtime/control/runtime_api.h"
#include "runtime/core/undo_manager.h"
#include <string>
#include <chrono>
#include <functional>

// Forward declarations — these types are used only as pointers in this header
namespace vivid {
class OperatorRegistry;
class SubgraphModuleRegistry;
class HotReloader;
class Graph;
class CaptureCoordinator;
struct Settings;
class PackageManager;
class BuildConsole;
class EditorWindowManager;
} // namespace vivid
class OperatorInfoCache;

class RuntimeCommandSink : public vivid::ui::UICommandSink {
public:
    explicit RuntimeCommandSink(vivid::RuntimeAPI& api) : api_(api) {}
    void set_param(const std::string& node_id, const std::string& param, float value) override {
        auto r = api_.set_param(node_id, param, value);
        if (r.ok) capture_undo_snapshot("param:" + node_id + "/" + param);
    }
    void add_node(const std::string& type, const std::string& id) override {
        auto r = api_.add_node(type, id);
        if (r.ok) capture_undo_snapshot();
    }
    bool try_add_node(const std::string& type, const std::string& id,
                      std::string* error = nullptr) override {
        auto r = api_.add_node(type, id);
        if (!r.ok) {
            if (error) *error = r.message;
            return false;
        }
        capture_undo_snapshot();
        if (error) error->clear();
        return true;
    }
    void remove_node(const std::string& id) override {
        auto r = api_.remove_node(id);
        if (r.ok) capture_undo_snapshot();
    }
    void connect(const std::string& from, const std::string& to) override {
        auto r = api_.connect(from, to);
        if (r.ok) capture_undo_snapshot();
    }
    bool try_connect(const std::string& from, const std::string& to,
                     std::string* error = nullptr) override {
        auto r = api_.connect(from, to);
        if (!r.ok) {
            if (error) *error = r.message;
            return false;
        }
        capture_undo_snapshot();
        if (error) error->clear();
        return true;
    }
    void disconnect(const std::string& from, const std::string& to) override {
        auto r = api_.disconnect(from, to);
        if (r.ok) capture_undo_snapshot();
    }
    bool try_disconnect(const std::string& from, const std::string& to,
                        std::string* error = nullptr) override {
        auto r = api_.disconnect(from, to);
        if (!r.ok) {
            if (error) *error = r.message;
            return false;
        }
        capture_undo_snapshot();
        if (error) error->clear();
        return true;
    }
    void set_connection_remap(const std::string& from, const std::string& to,
                              float from_min, float from_max,
                              float to_min, float to_max,
                              bool clamp, uint8_t curve = 0) override {
        auto r = api_.set_connection_remap(from, to, from_min, from_max, to_min, to_max, clamp, curve);
        if (r.ok) capture_undo_snapshot();
    }
    void set_node_layout(const std::string& node_id, float x, float y) override {
        auto r = api_.set_node_layout(node_id, x, y);
        if (r.ok) capture_undo_snapshot("layout:" + node_id);
    }
    void set_resolution(const std::string& node_id, uint32_t w, uint32_t h) override {
        auto r = api_.set_resolution(node_id, w, h);
        if (r.ok) capture_undo_snapshot();
    }
    void set_node_bypassed(const std::string& node_id, bool bypassed) override {
        auto r = api_.set_node_bypassed(node_id, bypassed);
        if (r.ok) capture_undo_snapshot("bypass:" + node_id);
    }
    bool try_set_node_bypassed(const std::string& node_id, bool bypassed,
                               std::string* error = nullptr) override {
        auto r = api_.set_node_bypassed(node_id, bypassed);
        if (r.ok) {
            capture_undo_snapshot("bypass:" + node_id);
            if (error) error->clear();
            return true;
        }
        if (error) *error = r.message;
        return false;
    }
    void add_midi_mapping(const std::string& node_id, const std::string& param,
                          int cc, int channel, float range_min, float range_max) override {
        auto r = api_.add_midi_mapping(node_id, param, cc, channel, range_min, range_max);
        if (r.ok) capture_undo_snapshot();
    }
    void remove_midi_mapping(const std::string& node_id, const std::string& param) override {
        auto r = api_.remove_midi_mapping(node_id, param);
        if (r.ok) capture_undo_snapshot();
    }
    void update_midi_mapping(const std::string& node_id, const std::string& param,
                             float range_min, float range_max) override {
        auto r = api_.update_midi_mapping(node_id, param, range_min, range_max);
        if (r.ok) capture_undo_snapshot();
    }

    void set_string_param(const std::string& node_id, const std::string& param,
                          const std::string& value) override {
        auto r = api_.set_string_param(node_id, param, value);
        if (r.ok) capture_undo_snapshot();
    }
    bool get_string_param_for_copy(const std::string& node_id,
                                   const std::string& param,
                                   std::string& value) override {
        auto r = api_.get_string_param(node_id, param);
        if (!r.ok) return false;
        value = r.message;
        return true;
    }

    void queue_state_transition(const std::string& sm_node_id, int state_idx,
                                const std::string& quantize) override {
        api_.queue_state_transition(sm_node_id, state_idx, quantize);
        // No undo snapshot: state transitions are live performance actions
    }
    void set_quantize_clock(const std::string& node_id) override {
        auto r = api_.set_quantize_clock(node_id);
        if (r.ok) capture_undo_snapshot();
    }
    void set_graph_metronome(float bpm, int beats_per_bar) override {
        auto r = api_.set_graph_metronome(bpm, beats_per_bar);
        if (r.ok) capture_undo_snapshot();
    }

    void set_param_lock(const std::string& node_id, const std::string& param, uint8_t flags) override {
        auto r = api_.set_param_lock(node_id, param, flags);
        if (r.ok) capture_undo_snapshot();
    }

    void recall_preset(const std::string& node_id, const std::string& name) override {
        auto r = api_.recall_preset(node_id, name);
        if (r.ok) capture_undo_snapshot();
    }
    void save_preset(const std::string& node_id, const std::string& name) override {
        auto r = api_.save_preset(node_id, name);
        if (r.ok) capture_undo_snapshot();
    }

    void set_state_preset(const std::string& sm_node, int state_idx,
                          const std::string& target_node,
                          const std::string& preset_name) override {
        auto r = api_.set_state_preset(sm_node, state_idx, target_node, preset_name);
        if (r.ok) capture_undo_snapshot();
    }
    void remove_state_preset(const std::string& sm_node, int state_idx,
                             const std::string& target_node) override {
        auto r = api_.remove_state_preset(sm_node, state_idx, target_node);
        if (r.ok) capture_undo_snapshot();
    }
    void ensure_state_mapping(const std::string& sm_node) override {
        auto r = api_.ensure_state_mapping(sm_node);
        if (r.ok) capture_undo_snapshot();
    }

    void session_create_track(const std::string& name) override {
        auto r = api_.create_track(name);
        if (r.ok) capture_undo_snapshot();
    }
    void session_rename_track(const std::string& track_id, const std::string& name) override {
        auto r = api_.rename_track(track_id, name);
        if (r.ok) capture_undo_snapshot();
    }
    void session_remove_track(const std::string& track_id) override {
        auto r = api_.remove_track(track_id);
        if (r.ok) capture_undo_snapshot();
    }
    void session_save_clip(const std::string& track_id, const std::string& name) override {
        auto r = api_.save_clip(track_id, name);
        if (r.ok) capture_undo_snapshot();
    }
    void session_rename_clip(const std::string& track_id, const std::string& clip_id,
                             const std::string& name) override {
        auto r = api_.rename_clip(track_id, clip_id, name);
        if (r.ok) capture_undo_snapshot();
    }
    void session_remove_clip(const std::string& track_id, const std::string& clip_id) override {
        auto r = api_.remove_clip(track_id, clip_id);
        if (r.ok) capture_undo_snapshot();
    }
    void session_update_clip(const std::string& track_id, const std::string& clip_id) override {
        auto r = api_.update_clip(track_id, clip_id);
        if (r.ok) capture_undo_snapshot();
    }
    void session_set_scene_assignment(const std::string& scene_id,
                                       const std::string& track_id,
                                       const std::string& clip_id) override {
        auto r = api_.set_scene_assignment(scene_id, track_id, clip_id);
        if (r.ok) capture_undo_snapshot();
    }
    void session_clear_scene_assignment(const std::string& scene_id,
                                         const std::string& track_id) override {
        auto r = api_.clear_scene_assignment(scene_id, track_id);
        if (r.ok) capture_undo_snapshot();
    }
    void session_save_scene(const std::string& name) override {
        auto r = api_.save_scene(name);
        if (r.ok) capture_undo_snapshot();
    }
    void session_rename_scene(const std::string& scene_id, const std::string& name) override {
        auto r = api_.rename_scene(scene_id, name);
        if (r.ok) capture_undo_snapshot();
    }
    void session_remove_scene(const std::string& scene_id) override {
        auto r = api_.remove_scene(scene_id);
        if (r.ok) capture_undo_snapshot();
    }
    void session_update_scene(const std::string& scene_id) override {
        auto r = api_.update_scene(scene_id);
        if (r.ok) capture_undo_snapshot();
    }
    void session_assign_nodes(const std::string& track_id,
                              const std::vector<std::string>& node_ids) override {
        auto r = api_.assign_nodes_to_track(track_id, node_ids);
        if (r.ok) capture_undo_snapshot();
    }
    void session_queue_clip(const std::string& track_id, const std::string& clip_id,
                            const std::string& quantize) override {
        api_.queue_clip(track_id, clip_id, quantize);
        // No undo snapshot: launch is a live performance action
    }
    void session_queue_scene(const std::string& scene_id,
                             const std::string& quantize) override {
        api_.queue_scene(scene_id, quantize);
        // No undo snapshot: launch is a live performance action
    }
    void session_create_cue_path(const std::string& name) override {
        auto r = api_.create_cue_path(name);
        if (r.ok) capture_undo_snapshot();
    }
    void session_add_cue_step(const std::string& path_id, const std::string& scene_id,
                              int index = -1) override {
        auto r = api_.add_cue_step(path_id, scene_id, index);
        if (r.ok) capture_undo_snapshot();
    }
    void session_launch_cue_step(const std::string& path_id, const std::string& step_id,
                                 const std::string& quantize) override {
        api_.launch_cue_step(path_id, step_id, quantize);
    }
    void session_advance_cue_path(const std::string& path_id,
                                  const std::string& quantize) override {
        api_.advance_cue_path(path_id, quantize);
    }
    void session_stop_cue_path(const std::string& path_id) override {
        api_.stop_cue_path(path_id);
    }

    void open_shader(const std::string& type_name) override;
    void open_module_source(const std::string& type_name) override;
    void open_editor(const std::string& node_id) override;
    bool is_editor_open(const std::string& node_id) const override;

    void clone_and_edit(const std::string& type_name,
                        const std::string& custom_name = {},
                        const std::string& node_id = {}) override;

    bool has_project_clone_destination() override;

    void set_editor_preference(const std::string& editor_id,
                               const std::string& custom_command) override;
    void set_style_preference(const std::string& style_id) override;
    void set_pan_gesture_preference(const std::string& gesture) override;
    bool try_set_audio_buffer_preference(uint32_t buffer_size,
                                         std::string* error = nullptr) override;

    bool can_create_operator() const override {
        return !operators_dir_.empty() && !build_dir_.empty();
    }

    std::string validate_operator_name(const std::string& name) override;

    bool create_operator(const VividCreateOperatorRequest& request,
                         std::string* error = nullptr) override;

    void add_mod_assignment(const std::string& node_id, const std::string& source,
                            const std::string& destination, float amount,
                            const std::string& polarity, const std::string& curve) override {
        std::string error;
        try_add_mod_assignment(node_id, source, destination, amount, polarity, curve, &error);
    }
    bool try_add_mod_assignment(const std::string& node_id, const std::string& source,
                                const std::string& destination, float amount,
                                const std::string& polarity, const std::string& curve,
                                std::string* error = nullptr) override {
        auto r = api_.add_mod_assignment(node_id, source, destination, amount, polarity, curve);
        if (r.ok) capture_undo_snapshot();
        if (error) *error = r.ok ? std::string() : r.message;
        return r.ok;
    }
    void remove_mod_assignment(const std::string& node_id,
                               const std::string& source, const std::string& destination) override {
        std::string error;
        try_remove_mod_assignment(node_id, source, destination, &error);
    }
    bool try_remove_mod_assignment(const std::string& node_id,
                                   const std::string& source, const std::string& destination,
                                   std::string* error = nullptr) override {
        auto r = api_.remove_mod_assignment(node_id, source, destination);
        if (r.ok) capture_undo_snapshot();
        if (error) *error = r.ok ? std::string() : r.message;
        return r.ok;
    }
    void update_mod_assignment(const std::string& node_id,
                               const std::string& source, const std::string& destination,
                               float amount, const std::string& polarity, const std::string& curve) override {
        std::string error;
        try_update_mod_assignment(node_id, source, destination, amount, polarity, curve, &error);
    }
    bool try_update_mod_assignment(const std::string& node_id,
                                   const std::string& source, const std::string& destination,
                                   float amount, const std::string& polarity, const std::string& curve,
                                   std::string* error = nullptr) override {
        auto r = api_.update_mod_assignment(node_id, source, destination, amount, polarity, curve);
        if (r.ok) capture_undo_snapshot("mod_amount:" + node_id + "/" + source + "/" + destination);
        if (error) *error = r.ok ? std::string() : r.message;
        return r.ok;
    }

    void set_solo(const std::string& node_id) override {
        api_.set_solo(node_id);
    }

    void add_sticky_note(const std::string& id, const std::string& text,
                         float x, float y, float w, float h, int color) override;
    void remove_sticky_note(const std::string& id) override;
    void update_sticky_note(const std::string& id, const std::string& text,
                            float x, float y, float w, float h, int color) override;

    void capture_snapshot() override;
    void start_recording(const std::string& path, const std::string& codec, double fps) override;
    void stop_recording() override;

    bool undo() override;

    bool redo() override;

    bool can_undo() const override { return undo_manager_.canUndo(); }
    bool can_redo() const override { return undo_manager_.canRedo(); }

    void set_capture_coordinator(vivid::CaptureCoordinator* cc) { capture_coordinator_ = cc; }
    void set_runtime_flags(bool* has_gpu_ops, bool* has_audio) {
        has_gpu_ops_ = has_gpu_ops;
        has_audio_ = has_audio;
    }
    void reset_undo_history() {
        undo_manager_.clear();
        capture_undo_snapshot();
    }
    void set_operators_dir(const std::string& dir) { operators_dir_ = dir; }
    void set_registry(vivid::OperatorRegistry* r) { registry_ = r; }
    void set_graph(vivid::Graph* g) {
        graph_ = g;
        reset_undo_history();
    }
    void set_op_cache(OperatorInfoCache* c) { op_cache_ = c; }
    void set_build_dir(const std::string& dir) { build_dir_ = dir; }
    void set_settings(vivid::Settings* s) { settings_ = s; }
    void set_hot_reloader(vivid::HotReloader* hr) { hot_reloader_ = hr; }
    void set_editor_window_manager(vivid::EditorWindowManager* m) { editor_window_manager_ = m; }
    void set_package_manager(vivid::PackageManager* pm) { package_manager_ = pm; }
    void set_subgraph_modules(const vivid::SubgraphModuleRegistry* m) { subgraph_modules_ = m; }
    void set_build_console(vivid::BuildConsole* bc) { build_console_ = bc; }
    void set_shader_watch_callback(std::function<void(const std::string&)> cb) {
        shader_watch_callback_ = std::move(cb);
    }
    void set_audio_buffer_preference_callback(
        std::function<bool(uint32_t, uint32_t, std::string&)> cb) {
        audio_buffer_preference_callback_ = std::move(cb);
    }
    void capture_external_undo_snapshot() {
        last_coalesce_key_.clear();
        capture_undo_snapshot();
    }

private:
    void capture_undo_snapshot(const std::string& coalesce_key = "");

    std::string project_shader_dir() const;
    std::string make_unique_shader_operator_name(const std::string& base_name) const;
    bool clone_shader_operator(const std::string& type_name,
                               const std::string& node_id = {},
                               std::string* error = nullptr);

    bool patch_package_cmake_ops(const std::string& pkg_dir, const std::string& op_name);

    void clone_cpp_operator(const std::string& type_name, const std::string& custom_name,
                            const std::string& node_id);

    // Ensure a project-local package exists beside the graph. Scaffolds and links if needed.
    // Returns {root_path, name} or empty strings on failure.
    std::pair<std::string, std::string> ensure_project_package();

    // Replace a node in the graph with a new type, preserving connections and layout.
    void swap_node_type(const std::string& old_id, const std::string& new_id,
                        const std::string& new_type);

    vivid::RuntimeAPI& api_;
    vivid::UndoManager undo_manager_;
    std::string last_coalesce_key_;
    std::chrono::steady_clock::time_point last_coalesce_time_{};
    vivid::CaptureCoordinator* capture_coordinator_ = nullptr;
    bool* has_gpu_ops_ = nullptr;
    bool* has_audio_ = nullptr;
    std::string operators_dir_;
    std::string build_dir_;
    vivid::OperatorRegistry* registry_ = nullptr;
    vivid::Graph* graph_ = nullptr;
    OperatorInfoCache* op_cache_ = nullptr;
    vivid::Settings* settings_ = nullptr;
    vivid::HotReloader* hot_reloader_ = nullptr;
    vivid::EditorWindowManager* editor_window_manager_ = nullptr;
    vivid::PackageManager* package_manager_ = nullptr;
    const vivid::SubgraphModuleRegistry* subgraph_modules_ = nullptr;
    vivid::BuildConsole* build_console_ = nullptr;
    std::function<void(const std::string&)> shader_watch_callback_;
    std::function<bool(uint32_t, uint32_t, std::string&)> audio_buffer_preference_callback_;
};
