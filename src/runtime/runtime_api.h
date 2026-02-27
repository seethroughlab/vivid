#ifndef VIVID_RUNTIME_RUNTIME_API_H
#define VIVID_RUNTIME_RUNTIME_API_H

#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

class Graph;
class Scheduler;
class AudioEngine;
class OperatorRegistry;
class SystemMidiListener;

struct CommandResult {
    bool ok;
    std::string message;
};

class RuntimeAPI {
public:
    RuntimeAPI(Graph& graph, Scheduler& scheduler, AudioEngine& audio_engine,
               OperatorRegistry& registry, SystemMidiListener* system_midi = nullptr);

    // Immediate: modify param on live scheduler node
    CommandResult set_param(const std::string& node_id, const std::string& param, float value);
    CommandResult set_string_param(const std::string& node_id, const std::string& param,
                                   const std::string& value);

    // Layout: persist node position for the graph UI
    CommandResult set_node_layout(const std::string& node_id, float x, float y);

    // Set GPU texture resolution for a node (0 = inherit/default)
    CommandResult set_resolution(const std::string& node_id, uint32_t width, uint32_t height);
    CommandResult get_param(const std::string& node_id, const std::string& param);

    // Buffered topology changes (require apply_pending)
    CommandResult add_node(const std::string& type, const std::string& id);
    CommandResult remove_node(const std::string& id);
    CommandResult connect(const std::string& from_addr, const std::string& to_addr);
    CommandResult disconnect(const std::string& from_addr, const std::string& to_addr);

    // Apply buffered topology changes — call between frames
    // Updates has_gpu_ops and has_audio output flags after rebuild.
    bool apply_pending(bool& has_gpu_ops, bool& has_audio);

    // Inspection
    CommandResult inspect(const std::string& node_id);
    CommandResult list_nodes();
    CommandResult list_types();

    // MIDI mapping
    CommandResult add_midi_mapping(const std::string& node_id, const std::string& param,
                                   int cc, int channel, float range_min, float range_max);
    CommandResult remove_midi_mapping(const std::string& node_id, const std::string& param);
    CommandResult update_midi_mapping(const std::string& node_id, const std::string& param,
                                      float range_min, float range_max);

    // Per-frame: apply MIDI CC values to mapped params
    void apply_midi_mappings();

    // Persistence
    CommandResult save();
    CommandResult save_as(const std::string& path);
    CommandResult reload(bool& has_gpu_ops, bool& has_audio);

    bool has_pending() const { return pending_topology_change_; }
    bool needs_gpu_realloc() const { return needs_gpu_realloc_; }
    void clear_gpu_realloc() { needs_gpu_realloc_ = false; }

private:
    static bool split_addr(const std::string& addr, std::string& node, std::string& port);

    Graph& graph_;
    Scheduler& scheduler_;
    AudioEngine& audio_engine_;
    OperatorRegistry& registry_;
    SystemMidiListener* system_midi_ = nullptr;
    bool pending_topology_change_ = false;
    bool needs_gpu_realloc_ = false;
};

} // namespace vivid

#endif // VIVID_RUNTIME_RUNTIME_API_H
