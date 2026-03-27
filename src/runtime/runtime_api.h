#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

class Graph;
class RuntimeCore;
class AudioEngine;
class OperatorRegistry;
class SystemMidiListener;
struct CompiledNode;

struct CommandResult {
    bool ok;
    std::string message;
};

struct CrossfadeState {
    std::unordered_map<std::string, float> start_params;   // snapshot at fade start
    std::unordered_map<std::string, float> target_params;  // from target preset
    std::string target_preset_name;
};

struct ActiveCrossfade {
    std::string sm_node_id;
    std::unordered_map<std::string, CrossfadeState> targets;  // target_node -> fade state
};

class RuntimeAPI {
public:
    RuntimeAPI(Graph& graph, RuntimeCore& core, AudioEngine& audio_engine,
               OperatorRegistry& registry, SystemMidiListener* system_midi = nullptr);

    // Immediate: modify param on live scheduler node
    CommandResult set_param(const std::string& node_id, const std::string& param, float value);
    CommandResult set_string_param(const std::string& node_id, const std::string& param,
                                   const std::string& value);

    // Layout: persist node position for the graph UI
    CommandResult set_node_layout(const std::string& node_id, float x, float y);

    // Set GPU texture resolution for a node (0 = inherit/default)
    CommandResult set_resolution(const std::string& node_id, uint32_t width, uint32_t height);

    // Set per-node cadence override (0=auto, 1=frame, 2=audio)
    CommandResult set_cadence_override(const std::string& node_id, uint8_t cadence);
    CommandResult get_param(const std::string& node_id, const std::string& param);

    // Per-parameter lock flags
    CommandResult set_param_lock(const std::string& node_id, const std::string& param, uint8_t flags);
    CommandResult get_param_lock(const std::string& node_id, const std::string& param);

    // Buffered topology changes (require apply_pending)
    CommandResult add_node(const std::string& type, const std::string& id);
    CommandResult remove_node(const std::string& id);
    CommandResult connect(const std::string& from_addr, const std::string& to_addr,
                          bool semantic_defaults = false);
    CommandResult disconnect(const std::string& from_addr, const std::string& to_addr);
    CommandResult set_connection_remap(const std::string& from_addr, const std::string& to_addr,
                                       float from_min, float from_max, float to_min, float to_max, bool clamp);

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

    // --- Variations ---
    CommandResult save_variation(const std::string& name);
    CommandResult recall_variation(const std::string& name);
    CommandResult recall_variation_idx(int idx);
    CommandResult remove_variation(const std::string& name);
    CommandResult rename_variation(const std::string& old_name, const std::string& new_name);
    CommandResult duplicate_variation(const std::string& name, const std::string& new_name);
    CommandResult move_variation(const std::string& name, int to_index);
    CommandResult update_variation(const std::string& name);
    CommandResult list_variations();
    CommandResult queue_variation(const std::string& name, const std::string& quantize);
    CommandResult set_quantize_clock(const std::string& node_id);

    // Per-frame: check pending quantized variation switch
    void tick_quantized_switch();

    // --- Per-Operator Presets ---
    CommandResult save_preset(const std::string& node_id, const std::string& name);
    CommandResult recall_preset(const std::string& node_id, const std::string& name);
    CommandResult update_preset(const std::string& node_id, const std::string& name);
    CommandResult remove_preset(const std::string& node_id, const std::string& name);
    CommandResult rename_preset(const std::string& node_id, const std::string& old_name,
                                const std::string& new_name);
    CommandResult list_presets(const std::string& node_id);
    CommandResult list_factory_presets(const std::string& node_id);

    // --- State-Preset Mapping ---
    CommandResult set_state_preset(const std::string& sm_node, int state_idx,
                                   const std::string& target_node, const std::string& preset_name);
    CommandResult remove_state_preset(const std::string& sm_node, int state_idx,
                                      const std::string& target_node);
    CommandResult clear_state_presets(const std::string& sm_node);
    CommandResult inspect_state_presets(const std::string& sm_node);

    // Per-frame: check for state machine transitions and recall mapped presets
    void tick_state_presets();

    // Per-operator preset state accessor for snapshot
    const std::string& active_preset(const std::string& node_id) const;

    // Variation state accessors for snapshot
    int pending_variation_idx() const { return pending_variation_.armed ? pending_variation_.variation_idx : -1; }
    bool variation_dirty() const { return variation_dirty_; }
    bool graph_dirty() const { return graph_dirty_; }

    // Persistence
    CommandResult save();
    CommandResult save_as(const std::string& path);
    CommandResult load_graph(const std::string& path, bool& has_gpu_ops, bool& has_audio);
    CommandResult reload(bool& has_gpu_ops, bool& has_audio);
    CommandResult new_graph(bool& has_gpu_ops, bool& has_audio);
    CommandResult new_project(const std::string& dir_path, bool& has_gpu_ops, bool& has_audio);
    CommandResult apply_snapshot_json(const std::string& graph_json,
                                      bool& has_gpu_ops, bool& has_audio);

    // Solo mode (session-only, not serialized)
    CommandResult set_solo(const std::string& node_id);  // empty string = clear solo
    std::string solo_node_id() const;

    void set_resources_dir(const std::string& dir) { resources_dir_ = dir; }

    bool has_pending() const { return pending_topology_change_; }
    bool needs_gpu_realloc() const { return needs_gpu_realloc_; }
    void clear_gpu_realloc() { needs_gpu_realloc_ = false; }
    uint64_t reload_serial() const { return reload_serial_; }
    bool consume_preserve_undo_history_reload() {
        bool preserve = preserve_undo_history_on_reload_;
        preserve_undo_history_on_reload_ = false;
        return preserve;
    }

private:
    static bool split_addr(const std::string& addr, std::string& node, std::string& port);

    Graph& graph_;
    RuntimeCore& core_;
    AudioEngine& audio_engine_;
    OperatorRegistry& registry_;
    SystemMidiListener* system_midi_ = nullptr;
    bool pending_topology_change_ = false;
    bool needs_gpu_realloc_ = false;
    uint64_t reload_serial_ = 0;
    bool preserve_undo_history_on_reload_ = false;

    // Quantized variation switching state
    struct PendingVariation {
        int variation_idx = -1;
        enum Quantize { Instant, Beat, Bar, FourBar } quantize = Instant;
        int beats_remaining = 0;
        bool armed = false;
    };
    PendingVariation pending_variation_;
    float prev_beat_phase_ = 0.0f;
    bool variation_dirty_ = false;
    bool graph_dirty_ = false;
    std::string last_saved_graph_json_;
    std::string active_graph_source_path_;
    std::string resources_dir_;

    // Internal helper to apply a variation's params to live nodes
    void apply_variation(int idx);

    // State-preset mapping: track previous state per mapped state machine
    std::unordered_map<std::string, float> prev_sm_state_;

    // Active crossfades (sm_node_id -> crossfade data)
    std::unordered_map<std::string, ActiveCrossfade> active_crossfades_;

    // Active preset per node (node_id -> preset name)
    std::unordered_map<std::string, std::string> active_presets_;

    // Graph base directory for resolving/relativizing file paths
    std::filesystem::path graph_base_dir() const;

    // Resolve+persist helper: stores absolute in CompiledNode, relative in NodeDef
    void set_file_param_internal(CompiledNode& cn, const std::string& param, const std::string& value);
    bool is_path_string_param(const CompiledNode& cn, const std::string& param) const;
    std::string to_runtime_string_value(const CompiledNode& cn, const std::string& param,
                                        const std::string& value) const;
    std::string to_persisted_string_value(const CompiledNode& cn, const std::string& param,
                                          const std::string& value) const;
    void mark_graph_dirty();
    void capture_saved_snapshot();
    void refresh_graph_dirty_from_saved_snapshot();
};

} // namespace vivid
