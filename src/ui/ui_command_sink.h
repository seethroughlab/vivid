#pragma once

#include <string>
#include <cstdint>

namespace vivid::ui {

class UICommandSink {
public:
    virtual ~UICommandSink() = default;
    virtual void set_param(const std::string& node_id, const std::string& param, float value) = 0;
    virtual void add_node(const std::string& type, const std::string& id) = 0;
    virtual void remove_node(const std::string& id) = 0;
    virtual void connect(const std::string& from, const std::string& to) = 0;
    virtual void disconnect(const std::string& from, const std::string& to) = 0;
    virtual void set_connection_scale(const std::string& from, const std::string& to, float scale) = 0;
    virtual void set_node_layout(const std::string& node_id, float x, float y) = 0;
    virtual void set_resolution(const std::string& node_id, uint32_t w, uint32_t h) = 0;
    virtual void add_midi_mapping(const std::string& node_id, const std::string& param,
                                  int cc, int channel, float range_min, float range_max) = 0;
    virtual void remove_midi_mapping(const std::string& node_id, const std::string& param) = 0;
    virtual void update_midi_mapping(const std::string& node_id, const std::string& param,
                                     float range_min, float range_max) = 0;
    virtual void open_shader(const std::string& type_name) {}
    virtual void duplicate_as_user_filter(const std::string& type_name) {}
    virtual void clone_and_edit(const std::string& type_name) {}
    virtual void set_editor_preference(const std::string& editor_id,
                                       const std::string& custom_command) {}
    virtual void set_style_preference(const std::string& style_id) {}
    virtual bool can_create_operator() const { return false; }
    virtual std::string validate_operator_name(const std::string& name) { return "not available"; }
    virtual bool create_operator(const std::string& name, int domain) { return false; }
    virtual void set_string_param(const std::string& node_id, const std::string& param,
                                  const std::string& value) = 0;

    // Variation operations (defaults are no-ops for headless/test sinks)
    virtual void save_variation(const std::string& name) {}
    virtual void recall_variation(const std::string& name) {}
    virtual void recall_variation_idx(int idx) {}
    virtual void remove_variation(const std::string& name) {}
    virtual void rename_variation(const std::string& old_name, const std::string& new_name) {}
    virtual void update_variation(const std::string& name) {}
    virtual void queue_variation(const std::string& name, const std::string& quantize) {}
    virtual void set_quantize_clock(const std::string& node_id) {}

    // Per-parameter lock flags
    virtual void set_param_lock(const std::string& node_id, const std::string& param, uint8_t flags) {}

    // Per-operator preset operations (defaults are no-ops for headless/test sinks)
    virtual void recall_preset(const std::string& node_id, const std::string& name) {}
    virtual void save_preset(const std::string& node_id, const std::string& name) {}

    // State-preset mapping operations
    virtual void set_state_preset(const std::string& sm_node, int state_idx,
                                  const std::string& target_node,
                                  const std::string& preset_name) {}
    virtual void remove_state_preset(const std::string& sm_node, int state_idx,
                                     const std::string& target_node) {}

    // Capture/recording operations (defaults are no-ops for headless/test sinks)
    virtual void capture_snapshot() {}
    virtual void start_recording(const std::string& path, const std::string& codec, double fps) {}
    virtual void stop_recording() {}
};

} // namespace vivid::ui
