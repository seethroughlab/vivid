#include "runtime/scheduler.h"
#include "common/gpu_util.h"
#include "operator_api/gpu_operator.h"
#include <algorithm>
#include <cstdio>

namespace vivid {

// Check if a CompiledEdge has non-default remap (any field differs from identity mapping)
inline bool has_remap(const CompiledEdge& e) {
    return e.from_min != 0.0f || e.from_max != 1.0f ||
           e.to_min  != 0.0f || e.to_max  != 1.0f || e.clamp;
}

// Apply remap: maps val from [from_min, from_max] to [to_min, to_max]
inline float apply_remap(float val, const CompiledEdge& e) {
    float range = e.from_max - e.from_min;
    float t;
    if (range != 0.0f) {
        t = (val - e.from_min) / range;
    } else {
        t = 0.5f;
        std::fprintf(stderr, "[vivid] Scheduler: edge remap has zero input range "
                     "(from_min == from_max == %g) — using midpoint\n", e.from_min);
    }
    float out = e.to_min + t * (e.to_max - e.to_min);
    if (e.clamp) {
        float lo = std::min(e.to_min, e.to_max);
        float hi = std::max(e.to_min, e.to_max);
        out = std::max(lo, std::min(hi, out));
    }
    return out;
}

// ---------------------------------------------------------------------------
// build
// ---------------------------------------------------------------------------

bool Scheduler::build(const Graph& graph, OperatorRegistry& registry) {
    solo_node_idx_ = -1;
    solo_active_set_.clear();

    // Extract graph base directory for resolving relative file paths
    graph_base_dir_ = std::filesystem::path(graph.source_path()).parent_path();

    // ── Compile the graph ─────────────────────────────────────────────────
    GraphCompiler::Options opts;
    opts.graph_base_dir = graph_base_dir_;
    opts.operators_src_dir = operators_src_dir_;
    compiled_graph_ = GraphCompiler::compile(graph, registry, opts);
    if (!compiled_graph_) {
        std::fprintf(stderr, "[vivid] CompiledGraph: compile failed\n");
        return false;
    }

    cadence_bridge_.build(*compiled_graph_);
    frame_executor_.set_operators_src_dir(operators_src_dir_);

    if (std::getenv("VIVID_VERBOSE")) {
        std::fprintf(stderr, "[vivid] Evaluation order:");
        for (uint32_t i = 0; i < compiled_graph_->nodes.size(); ++i) {
            std::fprintf(stderr, "%s%s", (i == 0 ? " " : " → "),
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

    return true;
}

void Scheduler::tick(double time, double delta_time, uint64_t frame, void* gpu_state,
                     PostNodeFn on_gpu_node, const VividInputState* input) {
    if (!compiled_graph_) {
        std::fprintf(stderr, "[vivid] Scheduler::tick() skipped: no CompiledGraph\n");
        return;
    }
    frame_executor_.tick(*compiled_graph_, time, delta_time, frame,
                         gpu_state, on_gpu_node, input);

    // Propagate control→audio param wires on CompiledNode.
    // Audio nodes are skipped by the frame executor but their param_values
    // must reflect modulation so the snapshot/inspector shows animated values.
    for (const auto& e : compiled_graph_->edges) {
        if (!e.targets_param) continue;
        auto& to_cn = compiled_graph_->nodes[e.to_node];
        if (to_cn.active_cadence != Cadence::Audio) continue;
        const auto& from_cn = compiled_graph_->nodes[e.from_node];
        if (e.targets_file_param) {
            const std::string& src = e.sources_file_param
                ? from_cn.file_param_storage[e.from_file_param_idx]
                : from_cn.output_string_values[e.from_port];
            to_cn.file_param_storage[e.to_file_param_idx] = src;
            to_cn.file_param_ptrs[e.to_file_param_idx] =
                to_cn.file_param_storage[e.to_file_param_idx].c_str();
            continue;
        }
        float raw = e.sources_param
            ? from_cn.param_values[e.from_port]
            : from_cn.output_values[e.from_port];
        float val = has_remap(e) ? apply_remap(raw, e) : raw;
        if (!(to_cn.param_lock_flags[e.to_port] & PARAM_LOCK_WIRES))
            to_cn.param_values[e.to_port] = val;
    }

    needs_gpu_realloc_ = frame_executor_.needs_gpu_realloc();
    if (needs_gpu_realloc_) frame_executor_.clear_gpu_realloc();
}

bool Scheduler::has_gpu_operators() const {
    if (compiled_graph_) {
        for (const auto& cn : compiled_graph_->nodes)
            if (cn.is_gpu) return true;
    }
    return false;
}

bool Scheduler::has_audio_operators() const {
    if (compiled_graph_)
        return !compiled_graph_->audio_order.empty();
    return false;
}

bool Scheduler::gpu_sink_source_size(int sink_idx, uint32_t& w, uint32_t& h) const {
    if (!compiled_graph_) return false;
    for (const auto& e : compiled_graph_->edges) {
        if (e.to_node == static_cast<uint32_t>(sink_idx) &&
            e.data_type == VIVID_PORT_TEXTURE && !e.targets_param) {
            const auto& up = compiled_graph_->nodes[e.from_node];
            w = up.gpu_tex_width;
            h = up.gpu_tex_height;
            return w > 0 && h > 0;
        }
    }
    return false;
}

WGPUTexture Scheduler::gpu_sink_source_texture(int sink_idx) const {
    if (!compiled_graph_) return nullptr;
    for (const auto& e : compiled_graph_->edges) {
        if (e.to_node == static_cast<uint32_t>(sink_idx) &&
            e.data_type == VIVID_PORT_TEXTURE && !e.targets_param) {
            const auto& up = compiled_graph_->nodes[e.from_node];
            for (size_t ai = 0; ai < up.aux_texture_output_port_indices.size(); ++ai) {
                if (e.from_port ==
                        static_cast<uint32_t>(up.aux_texture_output_port_indices[ai]))
                    return up.aux_gpu_textures[ai];
            }
            return up.gpu_texture;  // primary
        }
    }
    return nullptr;
}

bool Scheduler::is_audio_type(const std::string& type_name) const {
    if (compiled_graph_) {
        for (const auto& cn : compiled_graph_->nodes) {
            if (cn.type_name == type_name && cn.active_cadence == Cadence::Audio)
                return true;
        }
    }
    return false;
}


std::string Scheduler::type_name(uint32_t node_idx) const {
    if (compiled_graph_ && node_idx < compiled_graph_->nodes.size())
        return compiled_graph_->nodes[node_idx].type_name;
    return {};
}

bool Scheduler::reload_operator(const std::string& type_name, OperatorRegistry& registry,
                                const std::string& new_dylib_path) {
    if (!compiled_graph_) return false;

    // 1. Find all CompiledNodes of this type and save their param values by name
    struct SavedParams {
        uint32_t node_idx;
        std::unordered_map<std::string, float> values;
        std::unordered_map<std::string, std::string> string_values;
        std::unordered_map<std::string, uint8_t> lock_flags;
    };
    std::vector<SavedParams> saved;

    for (uint32_t i = 0; i < static_cast<uint32_t>(compiled_graph_->nodes.size()); ++i) {
        auto& cn = compiled_graph_->nodes[i];
        if (!cn.loader) continue;
        const auto* desc = cn.loader->descriptor();
        if (!desc || std::string(desc->name) != type_name) continue;

        SavedParams sp;
        sp.node_idx = i;
        for (const auto& [name, idx] : cn.param_indices) {
            sp.values[name] = cn.param_values[idx];
            if (cn.param_lock_flags[idx] != PARAM_LOCK_NONE)
                sp.lock_flags[name] = cn.param_lock_flags[idx];
        }
        for (const auto& [name, idx] : cn.file_param_indices) {
            sp.string_values[name] = cn.file_param_storage[idx];
        }
        saved.push_back(std::move(sp));
    }

    if (saved.empty()) return true;  // no instances to reload

    // 2. Destroy old instances while the old dylib is still loaded
    for (const auto& sp : saved) {
        auto& cn = compiled_graph_->nodes[sp.node_idx];
        if (cn.instance) {
            cn.loader->destroy_instance(cn.instance);
            cn.instance = nullptr;
        }
    }

    // 3. Reload the dylib
    if (!registry.reload_operator(type_name, new_dylib_path)) {
        std::fprintf(stderr, "[vivid] Scheduler: dylib reload failed for '%s'\n", type_name.c_str());
        // Old dylib is still loaded. Recreate instances using old loader so nodes keep running.
        OperatorLoader* old_loader = registry.find(type_name);
        if (old_loader && old_loader->is_loaded()) {
            const auto* old_desc = old_loader->descriptor();
            if (old_desc) {
                for (const auto& sp : saved) {
                    auto& cn = compiled_graph_->nodes[sp.node_idx];
                    cn.instance = old_loader->create_instance();
                    GraphCompiler::init_frame_state(cn, old_desc, &sp.values,
                                                    sp.string_values.empty() ? nullptr : &sp.string_values,
                                                    graph_base_dir_);
                    for (const auto& [pname, flags] : sp.lock_flags) {
                        auto pi = cn.param_indices.find(pname);
                        if (pi != cn.param_indices.end())
                            cn.param_lock_flags[pi->second] = flags;
                    }
                    cn.generation++;
                }
            }
        }
        return false;
    }

    // 4. Update loader pointer and recreate instances with param reconciliation
    OperatorLoader* new_loader = registry.find(type_name);
    if (!new_loader) return false;
    const auto* new_desc = new_loader->descriptor();
    if (!new_desc) return false;

    for (const auto& sp : saved) {
        auto& cn = compiled_graph_->nodes[sp.node_idx];
        cn.loader = new_loader;
        cn.instance = new_loader->create_instance();
        GraphCompiler::init_frame_state(cn, new_desc, &sp.values,
                                        sp.string_values.empty() ? nullptr : &sp.string_values,
                                        graph_base_dir_);

        // Restore lock flags
        for (const auto& [pname, flags] : sp.lock_flags) {
            auto pi = cn.param_indices.find(pname);
            if (pi != cn.param_indices.end())
                cn.param_lock_flags[pi->second] = flags;
        }

        // Clear error state on successful reload
        cn.errored = false;
        cn.error_message.clear();

        // Bump generation to force downstream recompute
        cn.generation++;
    }

    return true;
}

void Scheduler::allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                                      WGPUTextureFormat format,
                                      WGPUTextureUsage extra_usage) {
    if (!compiled_graph_) return;
    gpu_device_ = device;

    // Iterate nodes in topological order (they're already sorted)
    for (uint32_t ni = 0; ni < static_cast<uint32_t>(compiled_graph_->nodes.size()); ++ni) {
        auto& cn = compiled_graph_->nodes[ni];
        if (!cn.is_gpu) continue;

        // Release existing primary textures.
        if (cn.gpu_texture_view) { wgpuTextureViewRelease(cn.gpu_texture_view); cn.gpu_texture_view = nullptr; }
        if (cn.gpu_texture) { wgpuTextureRelease(cn.gpu_texture); cn.gpu_texture = nullptr; }
        for (auto& v : cn.aux_gpu_texture_views) v = nullptr;
        for (auto& t : cn.aux_gpu_textures)      t = nullptr;

        // GPU sinks and scene-only nodes don't produce their own textures
        cn.gpu_tex_inherited = false;
        if (cn.is_gpu_sink || !cn.has_texture_output) {
            cn.gpu_tex_width  = 0;
            cn.gpu_tex_height = 0;
            continue;
        }

        // Resolve texture size
        uint32_t w = cn.gpu_tex_width;
        uint32_t h = cn.gpu_tex_height;

        // Nodes with texture inputs always inherit from upstream (filters).
        if (!cn.texture_input_port_indices.empty()) {
            uint32_t first_tex_port = cn.texture_input_port_indices[0];
            for (const auto& e : compiled_graph_->edges) {
                if (e.to_node == ni && !e.targets_param &&
                    e.to_port == first_tex_port && e.data_type == VIVID_PORT_TEXTURE) {
                    const auto& upstream = compiled_graph_->nodes[e.from_node];
                    if (upstream.gpu_tex_width > 0 && upstream.gpu_tex_height > 0) {
                        w = upstream.gpu_tex_width;
                        h = upstream.gpu_tex_height;
                        cn.gpu_tex_inherited = true;
                    }
                    break;
                }
            }
        }

        // Fall back to default if still unresolved
        if (w == 0 || h == 0) {
            w = default_w;
            h = default_h;
        }

        cn.gpu_tex_width  = w;
        cn.gpu_tex_height = h;

        // Create texture
        WGPUTextureDescriptor tex_desc{};
        std::string label = "Node Texture [" + cn.node_id + "]";
        tex_desc.label = to_sv(label.c_str());
        tex_desc.size = { w, h, 1 };
        tex_desc.mipLevelCount = 1;
        tex_desc.sampleCount = 1;
        tex_desc.dimension = WGPUTextureDimension_2D;
        tex_desc.format = format;
        tex_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
                       | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | extra_usage;
        cn.gpu_texture = wgpuDeviceCreateTexture(device, &tex_desc);
        if (!cn.gpu_texture) {
            std::fprintf(stderr, "[vivid] GPU texture alloc failed for node '%s'\n", cn.node_id.c_str());
            continue;
        }

        WGPUTextureViewDescriptor view_desc{};
        std::string view_label = "Node View [" + cn.node_id + "]";
        view_desc.label = to_sv(view_label.c_str());
        view_desc.format = format;
        view_desc.dimension = WGPUTextureViewDimension_2D;
        view_desc.baseMipLevel = 0;
        view_desc.mipLevelCount = 1;
        view_desc.baseArrayLayer = 0;
        view_desc.arrayLayerCount = 1;
        view_desc.aspect = WGPUTextureAspect_All;
        cn.gpu_texture_view = wgpuTextureCreateView(cn.gpu_texture, &view_desc);
        if (!cn.gpu_texture_view) {
            std::fprintf(stderr, "[vivid] GPU texture view creation failed for node '%s'\n", cn.node_id.c_str());
            wgpuTextureRelease(cn.gpu_texture);
            cn.gpu_texture = nullptr;
            continue;
        }

        std::fprintf(stderr, "[vivid] Allocated %ux%u texture for node '%s'\n",
                     w, h, cn.node_id.c_str());
    }

}

int Scheduler::find_gpu_sink() const {
    if (compiled_graph_) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(compiled_graph_->nodes.size()); ++i) {
            if (compiled_graph_->nodes[i].is_gpu_sink) return static_cast<int>(i);
        }
    }
    return -1;
}

int Scheduler::find_effective_gpu_sink() const {
    if (compiled_graph_ && solo_node_idx_ >= 0 &&
        solo_node_idx_ < static_cast<int>(compiled_graph_->nodes.size()) &&
        compiled_graph_->nodes[solo_node_idx_].has_texture_output) {
        return solo_node_idx_;
    }
    return find_gpu_sink();
}

void Scheduler::set_solo(int node_idx) {
    if (!compiled_graph_) return;
    if (node_idx == solo_node_idx_) return;
    uint32_t n = static_cast<uint32_t>(compiled_graph_->nodes.size());
    if (node_idx < 0 || node_idx >= static_cast<int>(n)) {
        solo_node_idx_ = -1;
        solo_active_set_.clear();
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
}

void Scheduler::shutdown() {
    // Destroy instances and release GPU textures from CompiledGraph (authoritative owner).
    // Audio-cadence instances are owned and destroyed by AudioEngine::shutdown().
    // We must not destroy them here — AudioEngine's shutdown clears its own nodes_
    // before nulling cn.instance, so the pointers may still be non-null but already freed.
    if (compiled_graph_) {
        for (auto& cn : compiled_graph_->nodes) {
            // Release per-node primary GPU textures.
            if (cn.gpu_texture_view) { wgpuTextureViewRelease(cn.gpu_texture_view); cn.gpu_texture_view = nullptr; }
            if (cn.gpu_texture) { wgpuTextureRelease(cn.gpu_texture); cn.gpu_texture = nullptr; }
            for (auto& v : cn.aux_gpu_texture_views) v = nullptr;
            for (auto& t : cn.aux_gpu_textures)      t = nullptr;

            if (cn.active_cadence == Cadence::Audio) {
                cn.instance = nullptr;  // owned by AudioEngine, already destroyed
                continue;
            }
            if (cn.instance) {
                if (cn.loader)
                    cn.loader->destroy_instance(cn.instance);
                cn.instance = nullptr;
            }
        }
    }
    compiled_graph_.reset();

    // Flush deferred wgpu-core resource cleanup.
    if (gpu_device_) {
        wgpuDevicePoll(gpu_device_, true, nullptr);
        gpu_device_ = nullptr;
    }
}

} // namespace vivid
