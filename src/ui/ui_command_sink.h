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
    virtual void set_string_param(const std::string& node_id, const std::string& param,
                                  const std::string& value) = 0;
};

} // namespace vivid::ui
