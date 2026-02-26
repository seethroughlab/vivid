#ifndef VIVID_UI_COMMAND_SINK_H
#define VIVID_UI_COMMAND_SINK_H

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
    virtual void set_node_layout(const std::string& node_id, float x, float y) = 0;
    virtual void set_resolution(const std::string& node_id, uint32_t w, uint32_t h) = 0;
    virtual void add_midi_mapping(const std::string& node_id, const std::string& param,
                                  int cc, int channel, float range_min, float range_max) = 0;
    virtual void remove_midi_mapping(const std::string& node_id, const std::string& param) = 0;
    virtual void update_midi_mapping(const std::string& node_id, const std::string& param,
                                     float range_min, float range_max) = 0;
};

} // namespace vivid::ui

#endif // VIVID_UI_COMMAND_SINK_H
