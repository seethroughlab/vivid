#pragma once

#include "runtime/graph/compiled_graph.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/frame_executor.h"
#include "runtime/graph/graph_compiler.h"
#include "runtime/graph/subgraph_module.h"
#include <filesystem>
#include <memory>
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
    bool needs_gpu_realloc() const { return needs_gpu_realloc_; }
    void clear_gpu_realloc() { needs_gpu_realloc_ = false; }

    // ── Direct access to owned state ────────────────────────────────────────

    CompiledGraph* compiled_graph() { return compiled_graph_.get(); }
    const CompiledGraph* compiled_graph() const { return compiled_graph_.get(); }
    AudioFrameBridge& audio_frame_bridge() { return audio_frame_bridge_; }
    const AudioFrameBridge& audio_frame_bridge() const { return audio_frame_bridge_; }
    FrameExecutor& frame_executor() { return frame_executor_; }
    const FrameExecutor& frame_executor() const { return frame_executor_; }

    // Modulation lowering records from the most recent flatten pass
    const std::vector<ModulationLoweringRecord>& modulation_records() const { return modulation_records_; }

private:
    std::unique_ptr<CompiledGraph> compiled_graph_;
    AudioFrameBridge audio_frame_bridge_;
    FrameExecutor frame_executor_;

    std::string operators_src_dir_;
    std::filesystem::path graph_base_dir_;
    const SubgraphModuleRegistry* subgraph_modules_ = nullptr;
    std::vector<ModulationLoweringRecord> modulation_records_;
    bool needs_gpu_realloc_ = false;

    int solo_node_idx_ = -1;
    std::vector<bool> solo_active_set_;

    // Main-thread update hook for audio-cadence operators that need it
    // (e.g. media decoding, file I/O). Called during pre_tick_audio_sync.
    void update_audio_sources(double time);
};

} // namespace vivid
