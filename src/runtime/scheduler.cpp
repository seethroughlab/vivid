#include "runtime/scheduler.h"
#include <cstdio>

namespace vivid {

// ---------------------------------------------------------------------------
// build
// ---------------------------------------------------------------------------

bool Scheduler::build(const Graph& graph, OperatorRegistry& registry) {
    core_.solo_node_idx = -1;
    core_.solo_active_set.clear();

    // Extract graph base directory for resolving relative file paths
    graph_base_dir_ = std::filesystem::path(graph.source_path()).parent_path();

    // ── Compile the graph ─────────────────────────────────────────────────
    GraphCompiler::Options opts;
    opts.graph_base_dir = graph_base_dir_;
    opts.operators_src_dir = operators_src_dir_;
    core_.compiled_graph = GraphCompiler::compile(graph, registry, opts);
    if (!core_.compiled_graph) {
        std::fprintf(stderr, "[vivid] CompiledGraph: compile failed\n");
        return false;
    }

    core_.cadence_bridge.build(*core_.compiled_graph);
    frame_executor_.set_operators_src_dir(operators_src_dir_);

    if (std::getenv("VIVID_VERBOSE")) {
        std::fprintf(stderr, "[vivid] Evaluation order:");
        for (uint32_t i = 0; i < core_.compiled_graph->nodes.size(); ++i) {
            std::fprintf(stderr, "%s%s", (i == 0 ? " " : " → "),
                         core_.compiled_graph->nodes[i].node_id.c_str());
        }
        std::fprintf(stderr, "\n");
        std::fprintf(stderr, "[vivid] CompiledGraph: %zu nodes (%zu frame, %zu audio), "
                     "%zu edges (%zu snapshot)\n",
                     core_.compiled_graph->nodes.size(),
                     core_.compiled_graph->frame_order.size(),
                     core_.compiled_graph->audio_order.size(),
                     core_.compiled_graph->edges.size(),
                     core_.compiled_graph->frame_to_audio_edges.size() +
                     core_.compiled_graph->audio_to_frame_edges.size());
    }

    return true;
}

void Scheduler::pre_tick_audio_sync(double time) {
    if (!core_.compiled_graph || core_.compiled_graph->audio_order.empty()) return;
    core_.cadence_bridge.pull_from_audio(*core_.compiled_graph);
    core_.cadence_bridge.update_sources(time, *core_.compiled_graph);
}

void Scheduler::post_tick_audio_sync() {
    if (!core_.compiled_graph || core_.compiled_graph->audio_order.empty()) return;
    core_.cadence_bridge.push_to_audio(*core_.compiled_graph);
}

void Scheduler::tick(double time, double delta_time, uint64_t frame, void* gpu_state,
                     PostNodeFn on_gpu_node, const VividInputState* input) {
    if (!core_.compiled_graph) {
        std::fprintf(stderr, "[vivid] Scheduler::tick() skipped: no CompiledGraph\n");
        return;
    }
    frame_executor_.tick(*core_.compiled_graph, time, delta_time, frame,
                         gpu_state, on_gpu_node, input);

    // Update audio nodes' param_values for inspector display.
    core_.cadence_bridge.propagate_audio_display_params(*core_.compiled_graph);

    needs_gpu_realloc_ = frame_executor_.needs_gpu_realloc();
    if (needs_gpu_realloc_) frame_executor_.clear_gpu_realloc();
}

void Scheduler::set_solo(int node_idx) {
    core_.set_solo(node_idx);
    // Sync to frame executor for frame-rate skip logic
    frame_executor_.set_solo(core_.solo_node_idx, core_.solo_active_set);
}

bool Scheduler::has_gpu_operators() const {
    if (core_.compiled_graph)
        return frame_executor_.has_gpu_operators(*core_.compiled_graph);
    return false;
}

bool Scheduler::has_audio_operators() const {
    if (core_.compiled_graph)
        return !core_.compiled_graph->audio_order.empty();
    return false;
}

bool Scheduler::gpu_sink_source_size(int sink_idx, uint32_t& w, uint32_t& h) const {
    if (!core_.compiled_graph) return false;
    return frame_executor_.gpu_sink_source_size(*core_.compiled_graph, sink_idx, w, h);
}

WGPUTexture Scheduler::gpu_sink_source_texture(int sink_idx) const {
    if (!core_.compiled_graph) return nullptr;
    return frame_executor_.gpu_sink_source_texture(*core_.compiled_graph, sink_idx);
}

bool Scheduler::is_audio_type(const std::string& type_name) const {
    if (core_.compiled_graph)
        return core_.compiled_graph->has_audio_cadence_instances(type_name);
    return false;
}


std::string Scheduler::type_name(uint32_t node_idx) const {
    if (core_.compiled_graph && node_idx < core_.compiled_graph->nodes.size())
        return core_.compiled_graph->nodes[node_idx].type_name;
    return {};
}

bool Scheduler::reload_operator(const std::string& type_name, OperatorRegistry& registry,
                                const std::string& new_dylib_path) {
    if (!core_.compiled_graph) return false;
    return GraphCompiler::reload_operator(*core_.compiled_graph, type_name, registry,
                                          new_dylib_path, graph_base_dir_);
}

void Scheduler::allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                                      WGPUTextureFormat format,
                                      WGPUTextureUsage extra_usage) {
    if (!core_.compiled_graph) return;
    frame_executor_.allocate_gpu_textures(*core_.compiled_graph, device,
                                          default_w, default_h, format, extra_usage);
}

int Scheduler::find_gpu_sink() const {
    if (core_.compiled_graph)
        return frame_executor_.find_gpu_sink(*core_.compiled_graph);
    return -1;
}

int Scheduler::find_effective_gpu_sink() const {
    if (core_.compiled_graph)
        return frame_executor_.find_effective_gpu_sink(*core_.compiled_graph);
    return -1;
}

void Scheduler::shutdown() {
    // Destroy all operator instances. Audio auto-dup extras are owned by
    // AudioExecutor and destroyed during AudioEngine::shutdown() which must
    // be called before Scheduler::shutdown(). Primary instances (both frame
    // and audio cadence) are destroyed here.
    if (core_.compiled_graph) {
        // Release GPU textures and flush device.
        frame_executor_.shutdown_gpu(*core_.compiled_graph);

        for (auto& cn : core_.compiled_graph->nodes) {
            if (cn.instance) {
                if (cn.loader)
                    cn.loader->destroy_instance(cn.instance);
                cn.instance = nullptr;
            }
        }
    }
    core_.compiled_graph.reset();
}

} // namespace vivid
