#pragma once

#include "runtime/graph/compiled_graph.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/core/hot_reload.h"
#include "runtime/core/runtime_health_samplers.h"
#include "runtime/graph/frame_executor.h"
#include "runtime/graph/graph_compiler.h"
#include "runtime/graph/subgraph_module.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vivid {

class Graph;
class OperatorRegistry;
class SubgraphModuleRegistry;

// ---------------------------------------------------------------------------
// RuntimeCore — shared runtime state accessed by both frame and audio sides.
//
// Owns the CompiledGraph, AudioFrameBridge, FrameExecutor, and solo state.
// Provides build/tick/shutdown lifecycle and runtime queries.
// ---------------------------------------------------------------------------

class RuntimeCore {
public:
    struct LiveMetronomeUpdateOutcome {
        bool bar_epoch_reset = false;
    };

    struct PreparedBuild {
        std::unique_ptr<CompiledGraph> compiled_graph;
        std::filesystem::path graph_base_dir;
        std::vector<ModulationLoweringRecord> modulation_records;
    };

    // ── Build / lifecycle ───────────────────────────────────────────────────

    void set_subgraph_modules(const SubgraphModuleRegistry* reg) { subgraph_modules_ = reg; }
    const SubgraphModuleRegistry* subgraph_modules() const { return subgraph_modules_; }

    bool build(const Graph& graph, OperatorRegistry& registry);
    bool prepare_build(const Graph& graph, OperatorRegistry& registry,
                       PreparedBuild& out, std::string* error = nullptr) const;
    void adopt_prepared_build(PreparedBuild prepared);
    void tick(double time, double delta_time, uint64_t frame, void* gpu_state = nullptr,
              PostNodeFn on_gpu_node = nullptr,
              const VividInputState* input = nullptr);

    // Audio synchronization — call around tick() to bridge cadence worlds.
    void pre_tick_audio_sync(double time);
    void post_tick_audio_sync();

    void shutdown();

    // Hot-reload: destroy old instances, swap dylib, recreate with param reconciliation.
    bool reload_operator(const std::string& type_name, OperatorRegistry& registry,
                         const std::string& new_dylib_path);

    // ── Solo mode (session-only, not serialized) ────────────────────────────

    void set_solo(int node_idx);
    int solo_node_idx() const { return solo_node_idx_; }
    bool is_solo_active() const { return solo_node_idx_ >= 0; }
    const std::vector<bool>& solo_active_set() const { return solo_active_set_; }

    // ── Queries ─────────────────────────────────────────────────────────────

    bool has_gpu_operators() const;
    bool has_audio_operators() const;
    int find_gpu_sink() const;
    int find_effective_gpu_sink() const;
    bool gpu_sink_source_size(int sink_idx, uint32_t& w, uint32_t& h) const;
    WGPUTexture gpu_sink_source_texture(int sink_idx) const;
    void allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                               WGPUTextureFormat format,
                               WGPUTextureUsage extra_usage = 0);
    std::string type_name(uint32_t node_idx) const;
    bool has_audio_cadence_type(const std::string& type_name) const;

    // ── Config ──────────────────────────────────────────────────────────────

    void set_operators_src_dir(const std::string& dir) { operators_src_dir_ = dir; }
    const std::string& operators_src_dir() const { return operators_src_dir_; }
    void set_audio_buffer_size(uint32_t buffer_size) { audio_buffer_size_ = buffer_size; }
    uint32_t audio_buffer_size() const { return audio_buffer_size_; }
    uint32_t audio_sample_rate() const { return audio_sample_rate_; }
    bool needs_gpu_realloc() const { return needs_gpu_realloc_; }
    void clear_gpu_realloc() { needs_gpu_realloc_ = false; }

    // Operator name from the previous session's crash, set by main.cpp at
    // startup if CrashRecoveryManager recovered a CrashRecord. Empty when no
    // prior crash was recovered. Surfaced as a `recovered_from_crash` finding
    // by runtime_health::collect().
    void set_prior_crash_operator(std::string op_name) { prior_crash_operator_ = std::move(op_name); }
    const std::string& prior_crash_operator() const { return prior_crash_operator_; }

    // Most recent hot-reload outcome. Set by main_helpers' poll_hot_reload()
    // after each ReloadResult drains from HotReloader::poll_ready(). Surfaced
    // as `hot_reload_failed_required` (Error) or `hot_reload_failed_stale`
    // (Warning) findings by runtime_health::collect(), depending on whether
    // the failed target's operator type still appears in the compiled graph.
    void set_last_reload(ReloadResult r) { last_reload_ = std::move(r); }
    const std::optional<ReloadResult>& last_reload() const { return last_reload_; }

    // Per-frame sampler for sustained silence/black detection. Call once per
    // frame after `tick()` (so the sample reflects this frame's analysis
    // output). `time` is caller-domain — wall-clock from main.cpp, sim time
    // from tests; window queries on the samplers use the same domain.
    void sample_runtime_health(double time);
    const RuntimeHealthSamplers& health_samplers() const { return health_samplers_; }
    RuntimeHealthSamplers& health_samplers() { return health_samplers_; }

    // ── Direct access to owned state ────────────────────────────────────────

    CompiledGraph* compiled_graph() { return compiled_graph_.get(); }
    const CompiledGraph* compiled_graph() const { return compiled_graph_.get(); }
    AudioFrameBridge& audio_frame_bridge() { return audio_frame_bridge_; }
    const AudioFrameBridge& audio_frame_bridge() const { return audio_frame_bridge_; }
    FrameExecutor& frame_executor() { return frame_executor_; }
    const FrameExecutor& frame_executor() const { return frame_executor_; }
    double last_tick_time() const { return last_tick_time_; }
    GraphMetronomeSample sample_live_metronome(double time) const;
    const LiveMetronomeStateStore& live_metronome_store() const { return live_metronome_store_; }
    LiveMetronomeUpdateOutcome update_live_metronome(const GraphMetronomeDef& metronome, double time);
    void reset_live_metronome(const GraphMetronomeDef& metronome, double time);

    // Modulation lowering records from the most recent flatten pass
    const std::vector<ModulationLoweringRecord>& modulation_records() const { return modulation_records_; }

private:
    std::unique_ptr<CompiledGraph> compiled_graph_;
    AudioFrameBridge audio_frame_bridge_;
    FrameExecutor frame_executor_;

    std::string operators_src_dir_;
    std::string prior_crash_operator_;
    std::optional<ReloadResult> last_reload_;
    RuntimeHealthSamplers health_samplers_;
    std::filesystem::path graph_base_dir_;
    const SubgraphModuleRegistry* subgraph_modules_ = nullptr;
    std::vector<ModulationLoweringRecord> modulation_records_;
    bool needs_gpu_realloc_ = false;
    uint32_t audio_buffer_size_ = 256;
    uint32_t audio_sample_rate_ = 48000;

    int solo_node_idx_ = -1;
    std::vector<bool> solo_active_set_;
    double last_tick_time_ = 0.0;
    LiveMetronomeStateStore live_metronome_store_;
    bool live_metronome_initialized_ = false;

    void write_live_metronome_state(const LiveMetronomeState& state);

    // Main-thread update hook for audio-cadence operators that need it
    // (e.g. media decoding, file I/O). Called during pre_tick_audio_sync.
    void update_audio_sources(double time);
};

} // namespace vivid
