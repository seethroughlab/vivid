#include "runtime/core/runtime_core.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/graph/subgraph_module.h"
#include <algorithm>
#include <cstdio>

namespace vivid {

void RuntimeCore::write_live_metronome_state(const LiveMetronomeState& state) {
    const uint32_t current = live_metronome_store_.active_index.load(std::memory_order_relaxed);
    const uint32_t next = (current + 1u) % live_metronome_store_.states.size();
    live_metronome_store_.states[next] = state;
    live_metronome_store_.active_index.store(next, std::memory_order_release);
}

GraphMetronomeSample RuntimeCore::sample_live_metronome(double time) const {
    return vivid::sample_live_metronome(live_metronome_store_, time);
}

RuntimeCore::LiveMetronomeUpdateOutcome
RuntimeCore::update_live_metronome(const GraphMetronomeDef& metronome, double time) {
    LiveMetronomeUpdateOutcome outcome;
    GraphMetronomeDef sanitized = metronome;
    sanitized.bpm = std::max(1.0f, sanitized.bpm);
    sanitized.beats_per_bar = std::max(1, sanitized.beats_per_bar);

    const auto current = sample_live_metronome(time);
    LiveMetronomeState next_state{};
    next_state.bpm = sanitized.bpm;
    next_state.beats_per_bar = sanitized.beats_per_bar;
    next_state.anchor_time = time;

    if (current.beats_per_bar != sanitized.beats_per_bar) {
        next_state.anchor_beats_elapsed = 0.0;
        outcome.bar_epoch_reset = true;
    } else {
        next_state.anchor_beats_elapsed = current.beats_elapsed;
    }

    write_live_metronome_state(next_state);
    live_metronome_initialized_ = true;
    return outcome;
}

void RuntimeCore::reset_live_metronome(const GraphMetronomeDef& metronome, double time) {
    write_live_metronome_state(live_metronome_state_from_graph(metronome, time));
    live_metronome_initialized_ = true;
}

// ---------------------------------------------------------------------------
// build
// ---------------------------------------------------------------------------

bool RuntimeCore::build(const Graph& graph, OperatorRegistry& registry) {
    PreparedBuild prepared;
    if (!prepare_build(graph, registry, prepared))
        return false;
    adopt_prepared_build(std::move(prepared));
    return true;
}

bool RuntimeCore::prepare_build(const Graph& graph, OperatorRegistry& registry,
                                PreparedBuild& out, std::string* error) const {
    out = {};

    // Extract graph base directory for resolving relative file paths
    std::filesystem::path graph_base_dir = std::filesystem::path(graph.source_path()).parent_path();

    // Flatten subgraph modules before compilation (if any are registered)
    const Graph* compile_target = &graph;
    Graph flattened;
    if (subgraph_modules_ && !subgraph_modules_->empty()) {
        auto result = flatten_subgraphs(graph, *subgraph_modules_);
        flattened = std::move(result.graph);
        out.modulation_records = std::move(result.modulation_records);
        flattened.set_source_path(std::string(graph.source_path()));
        compile_target = &flattened;
    }

    GraphCompiler::Options opts;
    opts.graph_base_dir = graph_base_dir;
    opts.operators_src_dir = operators_src_dir_;
    opts.audio_buffer_size = audio_buffer_size_;
    opts.audio_sample_rate = audio_sample_rate_;
    opts.disabled_node_ids = safe_mode_.disabled_node_ids;
    opts.disabled_types    = safe_mode_.disabled_types;
    opts.quarantined_types = safe_mode_.quarantined_types;
    out.compiled_graph = GraphCompiler::compile(*compile_target, registry, opts);
    if (!out.compiled_graph) {
        if (error) *error = "graph compile failed";
        std::fprintf(stderr, "[vivid] CompiledGraph: compile failed\n");
        return false;
    }
    out.graph_base_dir = std::move(graph_base_dir);
    return true;
}

void RuntimeCore::adopt_prepared_build(PreparedBuild prepared) {
    solo_node_idx_ = -1;
    solo_active_set_.clear();
    graph_base_dir_ = std::move(prepared.graph_base_dir);
    compiled_graph_ = std::move(prepared.compiled_graph);
    modulation_records_ = std::move(prepared.modulation_records);

    audio_frame_bridge_.build(*compiled_graph_);
    frame_executor_.set_operators_src_dir(operators_src_dir_);

    if (std::getenv("VIVID_VERBOSE")) {
        std::fprintf(stderr, "[vivid] Evaluation order:");
        for (uint32_t i = 0; i < compiled_graph_->nodes.size(); ++i) {
            std::fprintf(stderr, "%s%s", (i == 0 ? " " : " \xe2\x86\x92 "),
                         compiled_graph_->nodes[i].node_id.c_str());
        }
        std::fprintf(stderr, "\n");
        std::fprintf(stderr, "[vivid] CompiledGraph: %zu nodes (%zu frame, %zu audio), "
                     "%zu edges (%zu snapshot)\n",
                     compiled_graph_->nodes.size(),
                     compiled_graph_->frame_order.size(),
                     compiled_graph_->audio_order.size(),
                     compiled_graph_->edges.size(),
                     compiled_graph_->frame_to_audio_edges.size() +
                     compiled_graph_->audio_to_frame_edges.size());
    }

    if (!live_metronome_initialized_ && compiled_graph_) {
        reset_live_metronome(compiled_graph_->metronome, last_tick_time_);
    }
}

// ---------------------------------------------------------------------------
// tick
// ---------------------------------------------------------------------------

void RuntimeCore::tick(double time, double delta_time, uint64_t frame, void* gpu_state,
                       PostNodeFn on_gpu_node, const VividInputState* input) {
    if (!compiled_graph_) {
        std::fprintf(stderr, "[vivid] RuntimeCore::tick() skipped: no CompiledGraph\n");
        return;
    }
    last_tick_time_ = time;
    const auto metronome = sample_live_metronome(time);
    frame_executor_.tick(*compiled_graph_, metronome, time, delta_time, frame,
                         gpu_state, on_gpu_node, input);

    // Update audio nodes' param_values for inspector display.
    audio_frame_bridge_.propagate_audio_display_params(*compiled_graph_);

    needs_gpu_realloc_ = frame_executor_.needs_gpu_realloc();
    if (needs_gpu_realloc_) frame_executor_.clear_gpu_realloc();
}

// ---------------------------------------------------------------------------
// Audio synchronization
// ---------------------------------------------------------------------------

void RuntimeCore::pre_tick_audio_sync(double time) {
    if (!compiled_graph_ || compiled_graph_->audio_order.empty()) return;
    audio_frame_bridge_.pull_from_audio(*compiled_graph_);
    update_audio_sources(time);
}

void RuntimeCore::post_tick_audio_sync() {
    if (!compiled_graph_ || compiled_graph_->audio_order.empty()) return;
    audio_frame_bridge_.push_to_audio(*compiled_graph_);
}

// ---------------------------------------------------------------------------
// Solo
// ---------------------------------------------------------------------------

void RuntimeCore::set_solo(int node_idx) {
    if (!compiled_graph_) return;
    if (node_idx == solo_node_idx_) return;
    uint32_t n = static_cast<uint32_t>(compiled_graph_->nodes.size());
    if (node_idx < 0 || node_idx >= static_cast<int>(n)) {
        solo_node_idx_ = -1;
        solo_active_set_.clear();
        audio_frame_bridge_.set_solo_active_set({});
        frame_executor_.set_solo(-1, {});
        return;
    }
    solo_node_idx_ = node_idx;
    solo_active_set_.assign(n, false);

    // BFS: mark solo node and all transitive upstream dependencies
    std::vector<uint32_t> queue;
    queue.push_back(static_cast<uint32_t>(node_idx));
    solo_active_set_[node_idx] = true;
    while (!queue.empty()) {
        uint32_t cur = queue.back();
        queue.pop_back();
        for (uint32_t up : compiled_graph_->nodes[cur].upstream_nodes) {
            if (!solo_active_set_[up]) {
                solo_active_set_[up] = true;
                queue.push_back(up);
            }
        }
    }

    // Sync to audio side via snapshot bridge
    audio_frame_bridge_.set_solo_active_set(solo_active_set_);
    // Sync to frame executor for frame-rate skip logic
    frame_executor_.set_solo(solo_node_idx_, solo_active_set_);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool RuntimeCore::has_gpu_operators() const {
    if (compiled_graph_)
        return frame_executor_.has_gpu_operators(*compiled_graph_);
    return false;
}

bool RuntimeCore::has_audio_operators() const {
    if (compiled_graph_)
        return !compiled_graph_->audio_order.empty();
    return false;
}

bool RuntimeCore::gpu_sink_source_size(int sink_idx, uint32_t& w, uint32_t& h) const {
    if (!compiled_graph_) return false;
    return frame_executor_.gpu_sink_source_size(*compiled_graph_, sink_idx, w, h);
}

WGPUTexture RuntimeCore::gpu_sink_source_texture(int sink_idx) const {
    if (!compiled_graph_) return nullptr;
    return frame_executor_.gpu_sink_source_texture(*compiled_graph_, sink_idx);
}

bool RuntimeCore::has_audio_cadence_type(const std::string& type_name) const {
    if (compiled_graph_)
        return compiled_graph_->has_audio_cadence_instances(type_name);
    return false;
}

std::string RuntimeCore::type_name(uint32_t node_idx) const {
    if (compiled_graph_ && node_idx < compiled_graph_->nodes.size())
        return compiled_graph_->nodes[node_idx].type_name;
    return {};
}

bool RuntimeCore::reload_operator(const std::string& type_name, OperatorRegistry& registry,
                                   const std::string& new_dylib_path) {
    if (!compiled_graph_) return false;
    return GraphCompiler::reload_operator(*compiled_graph_, type_name, registry,
                                          new_dylib_path, graph_base_dir_);
}

void RuntimeCore::allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                                         WGPUTextureFormat format,
                                         WGPUTextureUsage extra_usage) {
    if (!compiled_graph_) return;
    frame_executor_.allocate_gpu_textures(*compiled_graph_, device,
                                          default_w, default_h, format, extra_usage);
}

int RuntimeCore::find_gpu_sink() const {
    if (compiled_graph_)
        return frame_executor_.find_gpu_sink(*compiled_graph_);
    return -1;
}

int RuntimeCore::find_effective_gpu_sink() const {
    if (compiled_graph_)
        return frame_executor_.find_effective_gpu_sink(*compiled_graph_);
    return -1;
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void RuntimeCore::shutdown() {
    // Destroy all operator instances. Audio auto-dup extras are owned by
    // AudioExecutor and destroyed during AudioEngine::shutdown() which must
    // be called before RuntimeCore::shutdown(). Primary instances (both frame
    // and audio cadence) are destroyed here.
    if (compiled_graph_) {
        // Release GPU textures and flush device.
        frame_executor_.shutdown_gpu(*compiled_graph_);

        for (auto& cn : compiled_graph_->nodes) {
            if (cn.audio_instance && cn.audio_instance != cn.instance) {
                if (cn.loader)
                    cn.loader->destroy_instance(cn.audio_instance);
                cn.audio_instance = nullptr;
            }
            if (cn.instance) {
                if (cn.loader)
                    cn.loader->destroy_instance(cn.instance);
                cn.instance = nullptr;
            }
        }
    }
    compiled_graph_.reset();
}

// ---------------------------------------------------------------------------
// update_audio_sources — main-thread update hook for audio operators
// ---------------------------------------------------------------------------

void RuntimeCore::update_audio_sources(double time) {
    if (!compiled_graph_) return;
    for (uint32_t idx : compiled_graph_->audio_order) {
        auto& cn = compiled_graph_->nodes[idx];
        if (!cn.loader || !cn.loader->has_main_thread_update()) continue;
        cn.loader->main_thread_update(
            cn.instance, time,
            cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data(),
            static_cast<uint32_t>(cn.file_param_ptrs.size()));
    }
}

} // namespace vivid
