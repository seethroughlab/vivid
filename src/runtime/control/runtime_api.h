#pragma once

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "runtime/graph/graph.h"

namespace vivid {
class RuntimeCore;
class AudioEngine;
class OperatorRegistry;
class PackageManager;
class SubgraphModuleRegistry;
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
    struct PreservedRuntimeState {
        bool active = false;
        std::unordered_map<std::string, std::unordered_map<std::string, float>> params;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> string_params;
        std::unordered_map<std::string, std::unordered_map<std::string, uint8_t>> lock_flags;
    };

    RuntimeAPI(Graph& graph, RuntimeCore& core, AudioEngine& audio_engine,
               OperatorRegistry& registry, SystemMidiListener* system_midi = nullptr);

    // Immediate: modify param on live runtime node
    CommandResult set_param(const std::string& node_id, const std::string& param, float value);
    CommandResult set_string_param(const std::string& node_id, const std::string& param,
                                   const std::string& value);
    CommandResult get_string_param(const std::string& node_id, const std::string& param);

    // Layout: persist node position for the graph UI
    CommandResult set_node_layout(const std::string& node_id, float x, float y);

    // Set GPU texture resolution for a node (0 = inherit/default)
    CommandResult set_resolution(const std::string& node_id, uint32_t width, uint32_t height);

    // Toggle bypass on a node. Bypass causes the executor to skip the
    // operator's process_*() call and pass the first input port through to
    // the first output port (same type) each tick. Only honored when the
    // operator is bypass-eligible (first input/output port types match);
    // turning bypass on for an ineligible operator returns an error.
    CommandResult set_node_bypassed(const std::string& node_id, bool bypassed);

    CommandResult get_param(const std::string& node_id, const std::string& param);

    // Per-parameter lock flags
    CommandResult set_param_lock(const std::string& node_id, const std::string& param, uint8_t flags);
    CommandResult get_param_lock(const std::string& node_id, const std::string& param);

    // Buffered topology changes (require apply_pending)
    CommandResult add_node(const std::string& type, const std::string& id);
    CommandResult remove_node(const std::string& id);
    CommandResult connect(const std::string& from_addr, const std::string& to_addr,
                          bool semantic_defaults = false,
                          const std::string& bridge = "");
    CommandResult disconnect(const std::string& from_addr, const std::string& to_addr);
    CommandResult set_connection_remap(const std::string& from_addr, const std::string& to_addr,
                                       float from_min, float from_max, float to_min, float to_max,
                                       bool clamp, uint8_t curve = 0);

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

    // --- Modulation Assignments ---
    CommandResult add_mod_assignment(const std::string& node_id, const std::string& source,
                                     const std::string& destination, float amount,
                                     const std::string& polarity, const std::string& curve);
    CommandResult remove_mod_assignment(const std::string& node_id,
                                        const std::string& source, const std::string& destination);
    CommandResult update_mod_assignment(const std::string& node_id,
                                        const std::string& source, const std::string& destination,
                                        float amount, const std::string& polarity, const std::string& curve);
    CommandResult list_mod_sources(const std::string& node_id);
    CommandResult list_mod_destinations(const std::string& node_id);
    CommandResult list_mod_assignments(const std::string& node_id);

    // --- Quantize / Metronome ---
    CommandResult set_quantize_clock(const std::string& node_id);
    CommandResult set_graph_metronome(float bpm, int beats_per_bar);
    GraphMetronomeSample current_metronome_sample() const;

    // --- Per-Track StateMachine State Transitions ---
    CommandResult queue_state_transition(const std::string& sm_node_id, int state_idx,
                                         const std::string& quantize);
    int queued_state_for(const std::string& sm_node_id) const;
    void tick_quantized_state_transitions();

    // --- Per-Operator Presets ---
    CommandResult save_preset(const std::string& node_id, const std::string& name,
                              const std::string& metadata = "");
    CommandResult recall_preset(const std::string& node_id, const std::string& name);
    CommandResult update_preset(const std::string& node_id, const std::string& name);
    CommandResult remove_preset(const std::string& node_id, const std::string& name);
    CommandResult rename_preset(const std::string& node_id, const std::string& old_name,
                                const std::string& new_name);
    CommandResult list_presets(const std::string& node_id);
    // Structured listing: {"ok":true,"presets":[{name, metadata?}], "msg":"names"}.
    std::string list_presets_json(const std::string& node_id) const;
    CommandResult list_factory_presets(const std::string& node_id);

    // --- State-Preset Mapping ---
    CommandResult set_state_preset(const std::string& sm_node, int state_idx,
                                   const std::string& target_node, const std::string& preset_name);
    CommandResult remove_state_preset(const std::string& sm_node, int state_idx,
                                      const std::string& target_node);
    CommandResult clear_state_presets(const std::string& sm_node);
    CommandResult ensure_state_mapping(const std::string& sm_node);
    CommandResult inspect_state_presets(const std::string& sm_node);

    // Per-frame: check for state machine transitions and recall mapped presets
    void tick_state_presets();

    // Per-operator preset state accessor for snapshot
    const std::string& active_preset(const std::string& node_id) const;

    // --- Session: Track CRUD ---
    CommandResult create_track(const std::string& name);
    CommandResult rename_track(const std::string& track_id, const std::string& name);
    CommandResult remove_track(const std::string& track_id);
    CommandResult move_track(const std::string& track_id, int to_index);
    CommandResult assign_nodes_to_track(const std::string& track_id,
                                         const std::vector<std::string>& node_ids);
    CommandResult unassign_nodes_from_track(const std::string& track_id,
                                             const std::vector<std::string>& node_ids);

    // --- Session: Clip CRUD + launch ---
    CommandResult save_clip(const std::string& track_id, const std::string& name);
    CommandResult update_clip(const std::string& track_id, const std::string& clip_id);
    CommandResult rename_clip(const std::string& track_id, const std::string& clip_id,
                               const std::string& new_name);
    CommandResult remove_clip(const std::string& track_id, const std::string& clip_id);
    CommandResult move_clip(const std::string& track_id, const std::string& clip_id, int to_index);
    CommandResult launch_clip(const std::string& track_id, const std::string& clip_id);

    // Active clip per track (set by launch_clip / queue_clip; empty = none launched)
    const std::string& active_clip(const std::string& track_id) const;

    // --- Session: Scene CRUD ---
    CommandResult save_scene(const std::string& name);
    CommandResult update_scene(const std::string& scene_id);
    CommandResult rename_scene(const std::string& scene_id, const std::string& new_name);
    CommandResult remove_scene(const std::string& scene_id);
    CommandResult move_scene(const std::string& scene_id, int to_index);

    // --- Session: Scene assignment ---
    CommandResult set_scene_assignment(const std::string& scene_id,
                                        const std::string& track_id,
                                        const std::string& clip_id);
    CommandResult set_scene_leave_unchanged(const std::string& scene_id,
                                             const std::string& track_id);
    CommandResult clear_scene_assignment(const std::string& scene_id,
                                          const std::string& track_id);

    // --- Session: Cue paths ---
    CommandResult create_cue_path(const std::string& name);
    CommandResult rename_cue_path(const std::string& path_id, const std::string& name);
    CommandResult remove_cue_path(const std::string& path_id);
    CommandResult move_cue_path(const std::string& path_id, int to_index);
    CommandResult add_cue_step(const std::string& path_id, const std::string& scene_id,
                               int index = -1);
    CommandResult remove_cue_step(const std::string& path_id, const std::string& step_id);
    CommandResult move_cue_step(const std::string& path_id, const std::string& step_id,
                                int to_index);
    CommandResult set_cue_step_advance(const std::string& path_id, const std::string& step_id,
                                       const std::string& advance_mode, int bars);
    CommandResult launch_cue_step(const std::string& path_id, const std::string& step_id,
                                  const std::string& quantize);
    CommandResult advance_cue_path(const std::string& path_id, const std::string& quantize);
    CommandResult stop_cue_path(const std::string& path_id);

    // --- Session: Quantized launch ---
    CommandResult queue_clip(const std::string& track_id, const std::string& clip_id,
                              const std::string& quantize);
    CommandResult queue_scene(const std::string& scene_id, const std::string& quantize);

    // Per-tick: fire pending clip/scene launches at beat boundary
    void tick_quantized_clip_scene_launches();

    // Accessors for snapshot (Phase 4)
    const std::string& queued_scene_id() const;
    const std::string& queued_clip_for(const std::string& track_id) const;
    const std::string& active_cue_path_id() const { return active_cue_path_id_; }
    const std::string& active_cue_step_id() const { return active_cue_step_id_; }
    const std::string& queued_cue_path_id() const { return queued_cue_path_id_; }
    const std::string& queued_cue_step_id() const { return queued_cue_step_id_; }
    int cue_follow_beats_remaining() const;

    bool graph_dirty() const { return graph_dirty_; }

    // Persistence
    CommandResult save();
    CommandResult save_as(const std::string& path);
    // load_graph accepts an optional trailing lockfile_mode override:
    //   ""         — no override; treated as "studio" (caller passes
    //                Settings.lockfile_load_mode when a persisted pref exists).
    //   "studio"   — verify runs on sibling vivid.lock; findings stored; nothing disabled.
    //   "strict"   — critical findings mark affected nodes locked_unavailable.
    //   "recovery" — identical to studio for now; reserved.
    // Unknown values fall back to studio.
    CommandResult load_graph(const std::string& path, bool& has_gpu_ops, bool& has_audio,
                             const std::string& lockfile_mode = "");
    CommandResult reload(bool& has_gpu_ops, bool& has_audio);
    CommandResult new_graph(bool& has_gpu_ops, bool& has_audio);
    CommandResult new_project(const std::string& dir_path, bool& has_gpu_ops, bool& has_audio);
    CommandResult apply_snapshot_json(const std::string& graph_json,
                                      bool& has_gpu_ops, bool& has_audio);
    CommandResult rebuild_current_graph(bool& has_gpu_ops, bool& has_audio);

    // Write a project lockfile next to graph_path (or to output_path if non-empty).
    // Uses the currently-loaded graph for content-hash and node walking is sourced
    // from the graph loaded at graph_path so lockfile generation is deterministic
    // against on-disk state.
    CommandResult write_project_lockfile(PackageManager& package_manager,
                                         const std::string& graph_path,
                                         const std::string& output_path);

    // Verify the lockfile at lockfile_path against the graph at graph_path and
    // the live environment. Returns ok = true on success with message =
    // LockfileStatus JSON. ok = false only for I/O errors (missing graph/lockfile).
    CommandResult verify_project_lockfile(PackageManager& package_manager,
                                          const std::string& graph_path,
                                          const std::string& lockfile_path);

    // Convenience: look for a sibling vivid.lock next to graph_path and verify.
    // Returns message = {"overall":"no_lockfile","findings":[]} when the sibling
    // is absent so callers can render a distinct "no lockfile" state.
    CommandResult get_project_dependency_status(PackageManager& package_manager,
                                                const std::string& graph_path);

    // Solo mode (session-only, not serialized)
    CommandResult set_solo(const std::string& node_id);  // empty string = clear solo
    std::string solo_node_id() const;

    // Inject a single MIDI message into the named node. The operator must
    // export the optional `vivid_op_inject_midi` symbol (currently MidiInput
    // does this). Returns false on lookup failure or no-such-symbol.
    bool inject_midi_to_node(const std::string& node_id,
                             const uint8_t* bytes,
                             uint32_t count);

    void set_resources_dir(const std::string& dir) { resources_dir_ = dir; }
    void set_subgraph_modules(const SubgraphModuleRegistry* modules) { subgraph_modules_ = modules; }

    bool has_pending() const { return pending_topology_change_; }
    void request_recompile() { pending_topology_change_ = true; }
    bool needs_gpu_realloc() const { return needs_gpu_realloc_; }
    void clear_gpu_realloc() { needs_gpu_realloc_ = false; }
    uint64_t reload_serial() const { return reload_serial_; }
    void notify_external_graph_mutation();
    void finalize_external_graph_load();
    PreservedRuntimeState capture_preserved_runtime_state_for_path(const std::string& path);
    PreservedRuntimeState capture_current_runtime_state();
    void apply_preserved_runtime_state(const PreservedRuntimeState& state);
    bool consume_preserve_undo_history_reload() {
        bool preserve = preserve_undo_history_on_reload_;
        preserve_undo_history_on_reload_ = false;
        return preserve;
    }

private:
    static bool split_addr(const std::string& addr, std::string& node, std::string& port);

    // Module param proxy: resolve an exposed module param to the internal compiled node
    struct ResolvedModuleParam {
        CompiledNode* cn;
        uint32_t param_idx;
        std::string flat_node_id;
        std::string internal_param;
    };
    std::optional<ResolvedModuleParam> resolve_module_param(
        const std::string& node_id, const std::string& param);

    Graph& graph_;
    RuntimeCore& core_;
    AudioEngine& audio_engine_;
    OperatorRegistry& registry_;
    SystemMidiListener* system_midi_ = nullptr;
    const SubgraphModuleRegistry* subgraph_modules_ = nullptr;
    bool pending_topology_change_ = false;
    bool needs_gpu_realloc_ = false;
    uint64_t reload_serial_ = 0;
    bool preserve_undo_history_on_reload_ = false;

    // Per-track quantized state transitions (one per StateMachine node)
    struct PendingStateTransition {
        std::string sm_node_id;
        int target_state       = -1;
        enum Quantize { Instant, Beat, Bar, FourBar } quantize = Instant;
        int64_t target_beat_index = -1;
        bool armed             = false;
    };
    std::vector<PendingStateTransition> pending_state_transitions_;

    bool graph_dirty_ = false;
    std::string last_saved_graph_json_;
    std::string active_graph_source_path_;
    std::string resources_dir_;

    // State-preset mapping: track previous state per mapped state machine
    std::unordered_map<std::string, float> prev_sm_state_;

    // Active crossfades (sm_node_id -> crossfade data)
    std::unordered_map<std::string, ActiveCrossfade> active_crossfades_;

    // Active preset per node (node_id -> preset name)
    std::unordered_map<std::string, std::string> active_presets_;

    // Active clip per track (track_id -> clip_id), set by launch_clip / queue_clip
    std::unordered_map<std::string, std::string> active_clips_;

    // Pending quantized clip launches (one per track; new entry replaces old for same track)
    struct PendingClipLaunch {
        std::string track_id;
        std::string clip_id;
        int64_t target_beat_index = 0;
    };
    std::vector<PendingClipLaunch> pending_clip_launches_;

    // Pending quantized scene launch (at most one at a time)
    struct PendingSceneLaunch {
        std::string scene_id;
        int64_t target_beat_index = 0;
        std::string cue_path_id;
        std::string cue_step_id;
    };
    std::optional<PendingSceneLaunch> pending_scene_launch_;

    std::string active_cue_path_id_;
    std::string active_cue_step_id_;
    std::string queued_cue_path_id_;
    std::string queued_cue_step_id_;
    int64_t cue_follow_target_beat_index_ = -1;

    // Session clip capture/apply — used by save_clip, update_clip, launch_clip, queue_clip
    std::pair<
        std::unordered_map<std::string, std::unordered_map<std::string, float>>,
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
    > capture_clip_params(const std::string& track_id);
    void apply_clip_params(const std::string& track_id, const SessionClipDef& clip);
    bool fire_scene(const std::string& scene_id);
    void mark_cue_step_fired(const std::string& path_id, const std::string& step_id);
    int64_t compute_quantize_target_beat(const std::string& quantize) const;

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
