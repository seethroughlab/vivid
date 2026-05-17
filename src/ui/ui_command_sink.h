#pragma once

#include "operator_api/create_request.h"
#include <string>
#include <cstdint>

namespace vivid::ui {

class UICommandSink {
public:
    virtual ~UICommandSink() = default;
    virtual void set_param(const std::string& node_id, const std::string& param, float value) = 0;
    virtual void add_node(const std::string& type, const std::string& id) = 0;
    virtual bool try_add_node(const std::string& type, const std::string& id,
                              std::string* error = nullptr) {
        add_node(type, id);
        if (error) error->clear();
        return true;
    }
    virtual void remove_node(const std::string& id) = 0;
    virtual void connect(const std::string& from, const std::string& to) = 0;
    virtual bool try_connect(const std::string& from, const std::string& to,
                             std::string* error = nullptr) {
        connect(from, to);
        if (error) error->clear();
        return true;
    }
    virtual void disconnect(const std::string& from, const std::string& to) = 0;
    virtual bool try_disconnect(const std::string& from, const std::string& to,
                                std::string* error = nullptr) {
        disconnect(from, to);
        if (error) error->clear();
        return true;
    }
    virtual void set_connection_remap(const std::string& from, const std::string& to,
                                      float from_min, float from_max,
                                      float to_min, float to_max,
                                      bool clamp, uint8_t curve = 0) = 0;
    virtual void set_node_layout(const std::string& node_id, float x, float y) = 0;
    virtual void set_resolution(const std::string& node_id, uint32_t w, uint32_t h) = 0;
    virtual void add_midi_mapping(const std::string& node_id, const std::string& param,
                                  int cc, int channel, float range_min, float range_max) = 0;
    virtual void remove_midi_mapping(const std::string& node_id, const std::string& param) = 0;
    virtual void update_midi_mapping(const std::string& node_id, const std::string& param,
                                     float range_min, float range_max) = 0;
    virtual void open_shader(const std::string& type_name) {}
    virtual void open_module_source(const std::string& type_name) {}
    virtual void open_editor(const std::string& node_id) {}
    virtual bool is_editor_open(const std::string& node_id) const { return false; }
    virtual void clone_and_edit(const std::string& type_name) {
        clone_and_edit(type_name, "auto");
    }
    virtual void clone_and_edit(const std::string& type_name,
                                const std::string& custom_name = {},
                                const std::string& node_id = {}) {}
    virtual bool has_project_clone_destination() { return false; }
    virtual void set_editor_preference(const std::string& editor_id,
                                       const std::string& custom_command) {}
    virtual void set_style_preference(const std::string& style_id) {}
    virtual void set_pan_gesture_preference(const std::string& gesture) {}
    virtual bool try_set_audio_buffer_preference(uint32_t buffer_size,
                                                 std::string* error = nullptr) {
        (void)buffer_size;
        if (error) error->clear();
        return true;
    }
    virtual bool can_create_operator() const { return false; }
    virtual std::string validate_operator_name(const std::string& name) { return "not available"; }
    virtual bool create_operator(const VividCreateOperatorRequest& request,
                                 std::string* error = nullptr) {
        return false;
    }
    virtual void set_string_param(const std::string& node_id, const std::string& param,
                                  const std::string& value) = 0;
    virtual bool get_string_param_for_copy(const std::string& node_id,
                                           const std::string& param,
                                           std::string& value) {
        (void)node_id;
        (void)param;
        (void)value;
        return false;
    }

    virtual void queue_state_transition(const std::string& sm_node_id, int state_idx,
                                        const std::string& quantize) {}
    virtual void set_quantize_clock(const std::string& node_id) {}
    virtual void set_graph_metronome(float bpm, int beats_per_bar) {}

    // Per-parameter lock flags
    virtual void set_param_lock(const std::string& node_id, const std::string& param, uint8_t flags) {}

    // Per-node bypass toggle. Default no-op for headless/test sinks.
    virtual void set_node_bypassed(const std::string& node_id, bool bypassed) {}
    virtual bool try_set_node_bypassed(const std::string& node_id, bool bypassed,
                                       std::string* error = nullptr) {
        set_node_bypassed(node_id, bypassed);
        if (error) error->clear();
        return true;
    }

    // Per-operator preset operations (defaults are no-ops for headless/test sinks)
    virtual void recall_preset(const std::string& node_id, const std::string& name) {}
    virtual void save_preset(const std::string& node_id, const std::string& name) {}

    // State-preset mapping operations
    virtual void set_state_preset(const std::string& sm_node, int state_idx,
                                  const std::string& target_node,
                                  const std::string& preset_name) {}
    virtual void remove_state_preset(const std::string& sm_node, int state_idx,
                                     const std::string& target_node) {}
    virtual void ensure_state_mapping(const std::string& sm_node) {}

    // Sticky note operations (defaults are no-ops for headless/test sinks)
    virtual void add_sticky_note(const std::string& id, const std::string& text,
                                 float x, float y, float w, float h, int color) {}
    virtual void remove_sticky_note(const std::string& id) {}
    virtual void update_sticky_note(const std::string& id, const std::string& text,
                                    float x, float y, float w, float h, int color) {}

    // Modulation assignment operations (defaults are no-ops for headless/test sinks)
    virtual void add_mod_assignment(const std::string& node_id, const std::string& source,
                                    const std::string& destination, float amount,
                                    const std::string& polarity, const std::string& curve) {}
    virtual bool try_add_mod_assignment(const std::string& node_id, const std::string& source,
                                        const std::string& destination, float amount,
                                        const std::string& polarity, const std::string& curve,
                                        std::string* error = nullptr) {
        add_mod_assignment(node_id, source, destination, amount, polarity, curve);
        if (error) error->clear();
        return true;
    }
    virtual void remove_mod_assignment(const std::string& node_id,
                                       const std::string& source, const std::string& destination) {}
    virtual bool try_remove_mod_assignment(const std::string& node_id,
                                           const std::string& source, const std::string& destination,
                                           std::string* error = nullptr) {
        remove_mod_assignment(node_id, source, destination);
        if (error) error->clear();
        return true;
    }
    virtual void update_mod_assignment(const std::string& node_id,
                                       const std::string& source, const std::string& destination,
                                       float amount, const std::string& polarity, const std::string& curve) {}
    virtual bool try_update_mod_assignment(const std::string& node_id,
                                           const std::string& source, const std::string& destination,
                                           float amount, const std::string& polarity, const std::string& curve,
                                           std::string* error = nullptr) {
        update_mod_assignment(node_id, source, destination, amount, polarity, curve);
        if (error) error->clear();
        return true;
    }

    // Solo mode (session-only UI affordance)
    virtual void set_solo(const std::string& node_id) {}

    // Capture/recording operations (defaults are no-ops for headless/test sinks)
    virtual void capture_snapshot() {}
    virtual void start_recording(const std::string& path, const std::string& codec, double fps) {}
    virtual void stop_recording() {}

    // Undo/redo (default no-op for sinks that do not support history)
    virtual bool undo() { return false; }
    virtual bool redo() { return false; }
    virtual bool can_undo() const { return false; }
    virtual bool can_redo() const { return false; }

    // Session Track/Clip/Scene operations (defaults are no-ops for headless/test sinks)
    virtual void session_create_track(const std::string& name) {}
    virtual void session_rename_track(const std::string& track_id, const std::string& name) {}
    virtual void session_remove_track(const std::string& track_id) {}
    virtual void session_save_clip(const std::string& track_id, const std::string& name) {}
    virtual void session_rename_clip(const std::string& track_id, const std::string& clip_id,
                                     const std::string& name) {}
    virtual void session_remove_clip(const std::string& track_id, const std::string& clip_id) {}
    virtual void session_save_scene(const std::string& name) {}
    virtual void session_rename_scene(const std::string& scene_id, const std::string& name) {}
    virtual void session_remove_scene(const std::string& scene_id) {}
    virtual void session_update_scene(const std::string& scene_id) {}
    virtual void session_assign_nodes(const std::string& track_id,
                                      const std::vector<std::string>& node_ids) {}
    // Launch (NOT undo-tracked — real-time performance actions)
    virtual void session_queue_clip(const std::string& track_id, const std::string& clip_id,
                                    const std::string& quantize) {}
    virtual void session_queue_scene(const std::string& scene_id,
                                     const std::string& quantize) {}
};

} // namespace vivid::ui
