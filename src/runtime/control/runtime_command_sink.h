#pragma once

#include "ui/ui_command_sink.h"
#include "runtime/control/runtime_api.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_creator.h"
#include "runtime/core/hot_reload.h"
#include "runtime/gpu/wgsl_header_parser.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_info_cache.h"
#include "runtime/debug/capture_coordinator.h"
#include "runtime/core/settings.h"
#include "runtime/core/editor_detect.h"
#include "runtime/core/undo_manager.h"
#include "runtime/packages/package_manager.h"
#include "runtime/core/build_console.h"
#include "runtime/operators/operator_destination_policy.h"
#include "operator_api/data_driven_filter.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <chrono>
#include <array>

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
                              float to_min, float to_max, bool clamp) override {
        auto r = api_.set_connection_remap(from, to, from_min, from_max, to_min, to_max, clamp);
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

    void save_variation(const std::string& name) override {
        auto r = api_.save_variation(name);
        if (r.ok) capture_undo_snapshot();
    }
    void recall_variation(const std::string& name) override {
        auto r = api_.recall_variation(name);
        if (r.ok) capture_undo_snapshot();
    }
    void recall_variation_idx(int idx) override {
        auto r = api_.recall_variation_idx(idx);
        if (r.ok) capture_undo_snapshot();
    }
    void remove_variation(const std::string& name) override {
        auto r = api_.remove_variation(name);
        if (r.ok) capture_undo_snapshot();
    }
    void rename_variation(const std::string& old_name, const std::string& new_name) override {
        auto r = api_.rename_variation(old_name, new_name);
        if (r.ok) capture_undo_snapshot();
    }
    void update_variation(const std::string& name) override {
        auto r = api_.update_variation(name);
        if (r.ok) capture_undo_snapshot();
    }
    void duplicate_variation(const std::string& name, const std::string& new_name) override {
        auto r = api_.duplicate_variation(name, new_name);
        if (r.ok) capture_undo_snapshot();
    }
    void move_variation(const std::string& name, int to_index) override {
        auto r = api_.move_variation(name, to_index);
        if (r.ok) capture_undo_snapshot();
    }
    void queue_variation(const std::string& name, const std::string& quantize) override {
        auto r = api_.queue_variation(name, quantize);
        if (r.ok) capture_undo_snapshot();
    }
    void set_quantize_clock(const std::string& node_id) override {
        auto r = api_.set_quantize_clock(node_id);
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

    void open_shader(const std::string& type_name) override;

    void duplicate_as_user_filter(const std::string& type_name) override;

    void clone_and_edit(const std::string& type_name) override {
        clone_and_edit(type_name, "auto");
    }

    void clone_and_edit(const std::string& type_name, const std::string& destination) override;

    bool has_project_clone_destination() override;

    void set_editor_preference(const std::string& editor_id,
                               const std::string& custom_command) override {
        if (!settings_) return;
        settings_->editor = editor_id;
        settings_->editor_command = custom_command;
        vivid::save_settings(*settings_);
    }

    void set_style_preference(const std::string& style_id) override {
        if (!settings_) return;
        settings_->style_id = style_id;
        vivid::save_settings(*settings_);
    }

    void set_pan_gesture_preference(const std::string& gesture) override {
        if (!settings_) return;
        settings_->pan_gesture = gesture;
        vivid::save_settings(*settings_);
    }

    bool can_create_operator() const override {
        return !operators_dir_.empty() && !build_dir_.empty();
    }

    std::string validate_operator_name(const std::string& name) override {
        if (!registry_) return "registry not available";
        return vivid::OperatorCreator::validate_name(name, *registry_);
    }

    bool create_operator(const VividCreateOperatorRequest& request,
                         std::string* error = nullptr) override;

    void set_solo(const std::string& node_id) override {
        api_.set_solo(node_id);
    }

    void add_sticky_note(const std::string& id, const std::string& text,
                         float x, float y, float w, float h, int color) override {
        if (!graph_) return;
        vivid::StickyNoteDef note;
        note.id = id; note.text = text;
        note.x = x; note.y = y; note.width = w; note.height = h; note.color = color;
        graph_->add_sticky_note(std::move(note));
        capture_undo_snapshot();
    }
    void remove_sticky_note(const std::string& id) override {
        if (!graph_) return;
        if (graph_->remove_sticky_note(id))
            capture_undo_snapshot();
    }
    void update_sticky_note(const std::string& id, const std::string& text,
                            float x, float y, float w, float h, int color) override {
        if (!graph_) return;
        auto* sn = graph_->find_sticky_note(id);
        if (!sn) return;
        sn->text = text; sn->x = x; sn->y = y;
        sn->width = w; sn->height = h; sn->color = color;
        capture_undo_snapshot("sticky:" + id);
    }

    void capture_snapshot() override {
        if (!capture_coordinator_) return;
        // Fire-and-forget — PNG is saved to disk
        capture_coordinator_->request_snapshot_to_file("");
    }

    void start_recording(const std::string& path, const std::string& codec, double fps) override;

    void stop_recording() override {
        if (!capture_coordinator_) return;
        capture_coordinator_->request_stop_recording();
    }

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
    void set_filters_dir(const std::string& dir) { filters_dir_ = dir; }
    void set_registry(vivid::OperatorRegistry* r) { registry_ = r; }
    void set_graph(vivid::Graph* g) {
        graph_ = g;
        reset_undo_history();
    }
    void set_op_cache(OperatorInfoCache* c) { op_cache_ = c; }
    void set_working_filters_dir(const std::string& dir) { working_filters_dir_ = dir; }
    void set_build_dir(const std::string& dir) { build_dir_ = dir; }
    void set_settings(vivid::Settings* s) { settings_ = s; }
    void set_hot_reloader(vivid::HotReloader* hr) { hot_reloader_ = hr; }
    void set_package_manager(vivid::PackageManager* pm) { package_manager_ = pm; }
    void set_build_console(vivid::BuildConsole* bc) { build_console_ = bc; }

private:
    void capture_undo_snapshot(const std::string& coalesce_key = "");

    // Find the .wgsl preset file for a given type name in the filters/ directory
    std::string find_preset_wgsl(const std::string& type_name);

    bool patch_package_cmake_ops(const std::string& pkg_dir, const std::string& op_name);

    void clone_cpp_operator(const std::string& type_name, const std::string& destination);

    vivid::RuntimeAPI& api_;
    vivid::UndoManager undo_manager_;
    std::string last_coalesce_key_;
    std::chrono::steady_clock::time_point last_coalesce_time_{};
    vivid::CaptureCoordinator* capture_coordinator_ = nullptr;
    bool* has_gpu_ops_ = nullptr;
    bool* has_audio_ = nullptr;
    std::string operators_dir_;
    std::string filters_dir_;
    std::string working_filters_dir_;
    std::string build_dir_;
    vivid::OperatorRegistry* registry_ = nullptr;
    vivid::Graph* graph_ = nullptr;
    OperatorInfoCache* op_cache_ = nullptr;
    vivid::Settings* settings_ = nullptr;
    vivid::HotReloader* hot_reloader_ = nullptr;
    vivid::PackageManager* package_manager_ = nullptr;
    vivid::BuildConsole* build_console_ = nullptr;
};
