#include "runtime/frame_executor.h"
#include "runtime/crash_guard.h"
#include "runtime/shared_handle_registry.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/type_id.h"
#include <webgpu/webgpu.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace vivid {

static constexpr uint32_t kMaxSpreadCapacity = 1024;

// Check if an edge has non-default remap
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
    }
    float out = e.to_min + t * (e.to_max - e.to_min);
    if (e.clamp) {
        float lo = std::min(e.to_min, e.to_max);
        float hi = std::max(e.to_min, e.to_max);
        out = std::max(lo, std::min(hi, out));
    }
    return out;
}

void FrameExecutor::tick(CompiledGraph& cg, double time, double delta_time,
                         uint64_t frame, void* gpu_state,
                         PostNodeFn on_gpu_node,
                         const VividInputState* input) {
    // Reset per-tick flags
    for (auto& cn : cg.nodes) cn.processed_this_tick = false;

    for (uint32_t ni : cg.frame_order) {
        auto& cn = cg.nodes[ni];

        // Clear transient GPU shader error each tick
        if (cn.is_gpu) {
            cn.gpu_shader_error = false;
            cn.gpu_shader_error_msg.clear();
        }

        // Skip errored nodes — zero outputs
        if (cn.errored) {
            std::fill(cn.output_values.begin(), cn.output_values.end(), 0.0f);
            for (auto& sp : cn.output_spreads) sp.clear();
            for (auto& sp : cn.output_string_spreads) sp.clear();
            std::fill(cn.output_string_values.begin(), cn.output_string_values.end(), std::string());
            continue;
        }

        // Solo mode: skip non-active nodes
        if (solo_node_idx_ >= 0 && !solo_active_set_.empty() && !solo_active_set_[ni]) {
            std::fill(cn.output_values.begin(), cn.output_values.end(), 0.0f);
            for (auto& sp : cn.output_spreads) sp.clear();
            continue;
        }

        // Zero inputs
        std::fill(cn.input_values.begin(), cn.input_values.end(), 0.0f);
        std::fill(cn.input_string_values.begin(), cn.input_string_values.end(), std::string());
        for (auto& sp : cn.input_spreads) sp.clear();
        for (auto& sp : cn.input_string_spreads) sp.clear();

        // ── Wire propagation ────────────────────────────────────────────
        for (uint32_t ei : cg.frame_direct_edges) {
            const auto& e = cg.edges[ei];
            if (e.to_node != ni) continue;
            if (e.data_type == VIVID_PORT_TEXTURE || vivid_is_custom_port_type(e.data_type))
                continue;

            const auto& from_cn = cg.nodes[e.from_node];

            if (e.targets_file_param) {
                const std::string& src = e.sources_file_param
                    ? from_cn.file_param_storage[e.from_file_param_idx]
                    : from_cn.output_string_values[e.from_port];
                cn.file_param_storage[e.to_file_param_idx] = src;
                cn.file_param_ptrs[e.to_file_param_idx] =
                    cn.file_param_storage[e.to_file_param_idx].c_str();
                continue;
            }

            if (e.data_type == VIVID_PORT_STRING) {
                cn.input_string_values[e.to_port] = from_cn.output_string_values[e.from_port];
                continue;
            }
            if (e.data_type == VIVID_PORT_STRING_SPREAD) {
                cn.input_string_spreads[e.to_port] = from_cn.output_string_spreads[e.from_port];
                continue;
            }

            // Float wire (SIGNAL) with optional remap
            float raw = e.sources_param
                ? from_cn.param_values[e.from_port]
                : from_cn.output_values[e.from_port];
            float val = has_remap(e) ? apply_remap(raw, e) : raw;

            if (e.targets_param) {
                if (cn.param_lock_flags.size() > e.to_port &&
                    (cn.param_lock_flags[e.to_port] & 1))
                    continue;  // PARAM_LOCK_WIRES
                cn.param_values[e.to_port] = val;
            } else {
                cn.input_values[e.to_port] = val;

                // Spread propagation
                if (!e.sources_param && e.from_port < from_cn.output_spreads.size()) {
                    const auto& src_spread = from_cn.output_spreads[e.from_port];
                    if (!src_spread.empty()) {
                        auto& dst_spread = cn.input_spreads[e.to_port];
                        if (dst_spread.empty()) {
                            dst_spread = src_spread;
                            if (has_remap(e)) {
                                for (auto& v : dst_spread) v = apply_remap(v, e);
                            }
                        } else {
                            size_t old_len = dst_spread.size();
                            size_t src_len = src_spread.size();
                            size_t new_len = std::max(old_len, src_len);
                            if (new_len > kMaxSpreadCapacity) new_len = kMaxSpreadCapacity;
                            if (new_len > old_len) {
                                dst_spread.resize(new_len);
                                for (size_t si = old_len; si < new_len; ++si)
                                    dst_spread[si] = dst_spread[si % old_len];
                            }
                            for (size_t j = 0; j < new_len; ++j) {
                                float sv = src_spread[j % src_len];
                                if (has_remap(e)) sv = apply_remap(sv, e);
                                dst_spread[j] += sv;
                            }
                        }
                        if (!dst_spread.empty())
                            cn.input_values[e.to_port] = dst_spread[0];
                    }
                }
            }
        }

        // ── Generation-based memoization ────────────────────────────────
        // Only apply skip logic for non-time-dependent nodes that have upstreams.
        // Root nodes (no upstream) always process.
        bool should_process = cn.time_dependent || cn.upstream_nodes.empty();
        if (!should_process) {
            for (uint32_t up : cn.upstream_nodes) {
                if (cg.nodes[up].processed_this_tick) { should_process = true; break; }
            }
        }
        if (!should_process) {
            for (uint32_t up : cn.upstream_nodes) {
                if (cg.nodes[up].active_cadence == Cadence::Audio &&
                    cg.nodes[up].generation != cn.last_processed_gen) {
                    should_process = true; break;
                }
            }
        }
        if (!should_process && cn.generation != cn.last_processed_gen)
            should_process = true;
        if (!should_process) continue;

        // ── Build spread port staging ───────────────────────────────────
        for (uint32_t p = 0; p < cn.input_port_count; ++p) {
            cn.c_in_spreads[p].data = cn.input_spreads[p].data();
            cn.c_in_spreads[p].length = static_cast<uint32_t>(cn.input_spreads[p].size());
            cn.c_in_spreads[p].capacity = static_cast<uint32_t>(cn.input_spreads[p].capacity());
        }
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            cn.c_out_spreads[p].data = cn.out_spread_buf[p].data();
            cn.c_out_spreads[p].length = 0;
            cn.c_out_spreads[p].capacity = kMaxSpreadCapacity;
        }
        for (uint32_t p = 0; p < cn.input_port_count; ++p) {
            for (size_t si = 0; si < cn.input_string_spreads[p].size() && si < cn.in_string_spread_ptrs[p].size(); ++si)
                cn.in_string_spread_ptrs[p][si] = cn.input_string_spreads[p][si].c_str();
            cn.c_in_string_spreads[p].data = cn.in_string_spread_ptrs[p].data();
            cn.c_in_string_spreads[p].length = static_cast<uint32_t>(cn.input_string_spreads[p].size());
            cn.c_in_string_spreads[p].capacity = kMaxSpreadCapacity;
        }
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            cn.c_out_string_spreads[p].data = cn.out_string_spread_ptr_buf[p].data();
            cn.c_out_string_spreads[p].length = 0;
            cn.c_out_string_spreads[p].capacity = kMaxSpreadCapacity;
        }
        std::fill(cn.custom_output_buf.begin(), cn.custom_output_buf.end(), nullptr);

        for (auto& sv : cn.c_input_string_values)
            sv = nullptr;
        for (uint32_t p = 0; p < cn.input_port_count; ++p)
            cn.c_input_string_values[p] = cn.input_string_values[p].c_str();
        for (auto& sv : cn.c_output_string_values)
            sv = nullptr;

        // ── Process ─────────────────────────────────────────────────────
        if (cn.missing_operator || !cn.loader) {
            std::fill(cn.output_values.begin(), cn.output_values.end(), 0.0f);
        } else if (cn.is_gpu && gpu_state) {
            // ── GPU path: build VividGpuContext ─────────────────────────
            auto* base_gpu = static_cast<VividGpuContext*>(gpu_state);
            VividGpuContext gpu_ctx{};

            gpu_ctx.time          = time;
            gpu_ctx.delta_time    = delta_time;
            gpu_ctx.frame         = frame;
            gpu_ctx.param_values  = cn.param_values.data();
            gpu_ctx.input_values  = cn.input_values.data();
            gpu_ctx.output_values = cn.output_values.data();
            gpu_ctx.input_spreads  = cn.c_in_spreads.data();
            gpu_ctx.output_spreads = cn.c_out_spreads.data();
            gpu_ctx.input_string_values = cn.c_input_string_values.empty()
                                        ? nullptr : cn.c_input_string_values.data();
            gpu_ctx.output_string_values = cn.c_output_string_values.empty()
                                        ? nullptr : cn.c_output_string_values.data();
            gpu_ctx.input_string_spreads = cn.c_in_string_spreads.empty()
                                        ? nullptr : cn.c_in_string_spreads.data();
            gpu_ctx.output_string_spreads = cn.c_out_string_spreads.empty()
                                        ? nullptr : cn.c_out_string_spreads.data();
            gpu_ctx.file_param_values = cn.file_param_ptrs.empty()
                                        ? nullptr : cn.file_param_ptrs.data();
            gpu_ctx.file_param_count  = static_cast<uint32_t>(cn.file_param_ptrs.size());
            gpu_ctx.input = input;
            gpu_ctx.shared_handles = vivid::shared_handle_service();
            gpu_ctx.preferred_tex_width  = 0;
            gpu_ctx.preferred_tex_height = 0;

            // GPU-specific resources
            gpu_ctx.device          = base_gpu->device;
            gpu_ctx.queue           = base_gpu->queue;
            gpu_ctx.command_encoder = base_gpu->command_encoder;
            gpu_ctx.output_format   = base_gpu->output_format;
            gpu_ctx.output_texture      = cn.gpu_texture;
            gpu_ctx.output_texture_view = cn.gpu_texture_view;
            gpu_ctx.output_width    = cn.gpu_tex_width;
            gpu_ctx.output_height   = cn.gpu_tex_height;

            // Resolve texture inputs from upstream via edges
            size_t tex_count = cn.texture_input_port_indices.size();
            cn.resolved_tex_inputs.clear();
            cn.resolved_tex_inputs.resize(tex_count, nullptr);
            cn.resolved_tex_raw.clear();
            cn.resolved_tex_raw.resize(tex_count, nullptr);
            cn.resolved_tex_widths.clear();
            cn.resolved_tex_widths.resize(tex_count, 0);
            cn.resolved_tex_heights.clear();
            cn.resolved_tex_heights.resize(tex_count, 0);
            for (size_t ti = 0; ti < tex_count; ++ti) {
                uint32_t port_idx = cn.texture_input_port_indices[ti];
                for (uint32_t ei : cg.frame_direct_edges) {
                    const auto& e = cg.edges[ei];
                    if (e.to_node == ni && !e.targets_param &&
                        e.to_port == port_idx && e.data_type == VIVID_PORT_TEXTURE) {
                        const auto& upstream = cg.nodes[e.from_node];
                        bool routed_aux = false;
                        for (size_t ai = 0; ai < upstream.aux_texture_output_port_indices.size(); ++ai) {
                            if (e.from_port ==
                                    static_cast<uint32_t>(upstream.aux_texture_output_port_indices[ai])) {
                                cn.resolved_tex_inputs[ti] = upstream.aux_gpu_texture_views[ai];
                                cn.resolved_tex_raw[ti]    = upstream.aux_gpu_textures[ai];
                                routed_aux = true;
                                break;
                            }
                        }
                        if (!routed_aux) {
                            cn.resolved_tex_inputs[ti] = upstream.gpu_texture_view;
                            cn.resolved_tex_raw[ti]    = upstream.gpu_texture;
                        }
                        cn.resolved_tex_widths[ti]  = upstream.gpu_tex_width;
                        cn.resolved_tex_heights[ti] = upstream.gpu_tex_height;
                        break;
                    }
                }
            }
            gpu_ctx.input_texture_views = cn.resolved_tex_inputs.empty()
                                            ? nullptr : cn.resolved_tex_inputs.data();
            gpu_ctx.input_texture_count = static_cast<uint32_t>(tex_count);
            gpu_ctx.input_textures       = cn.resolved_tex_raw.empty()
                                            ? nullptr : cn.resolved_tex_raw.data();
            gpu_ctx.input_texture_widths  = cn.resolved_tex_widths.empty()
                                            ? nullptr : cn.resolved_tex_widths.data();
            gpu_ctx.input_texture_heights = cn.resolved_tex_heights.empty()
                                            ? nullptr : cn.resolved_tex_heights.data();
            gpu_ctx.operators_src_dir = operators_src_dir_.empty()
                                            ? nullptr : operators_src_dir_.c_str();
            gpu_ctx.aux_output_texture_views = cn.aux_gpu_texture_views.empty()
                ? nullptr : cn.aux_gpu_texture_views.data();
            gpu_ctx.aux_output_texture_count =
                static_cast<uint32_t>(cn.aux_gpu_texture_views.size());

            // Resolve custom inputs from upstream via edges
            for (size_t hi = 0; hi < cn.custom_input_port_indices.size(); ++hi) {
                uint32_t port_idx = cn.custom_input_port_indices[hi];
                cn.resolved_custom_inputs[hi] = nullptr;
                for (uint32_t ei : cg.frame_direct_edges) {
                    const auto& e = cg.edges[ei];
                    if (!vivid_is_custom_port_type(e.data_type) || e.to_node != ni || e.targets_param)
                        continue;
                    if (e.to_port != port_idx) continue;
                    const auto& upstream = cg.nodes[e.from_node];
                    for (uint32_t s = 0; s < upstream.custom_output_port_indices.size(); ++s) {
                        if (upstream.custom_output_port_indices[s] == e.from_port) {
                            if (s < upstream.custom_outputs.size())
                                cn.resolved_custom_inputs[hi] = upstream.custom_outputs[s];
                            break;
                        }
                    }
                    break;
                }
            }
            gpu_ctx.custom_inputs      = cn.resolved_custom_inputs.empty()
                                            ? nullptr : cn.resolved_custom_inputs.data();
            gpu_ctx.custom_input_count = static_cast<uint32_t>(cn.resolved_custom_inputs.size());
            gpu_ctx.custom_outputs      = cn.custom_output_buf.empty()
                                            ? nullptr : cn.custom_output_buf.data();
            gpu_ctx.custom_output_count = static_cast<uint32_t>(cn.custom_output_buf.size());

            try {
                CrashGuard guard(cn.node_id.c_str());
                cn.loader->process_gpu(cn.instance, &gpu_ctx);
            } catch (const std::exception& ex) {
                cn.errored = true;
                cn.error_message = ex.what();
                std::fill(cn.output_values.begin(), cn.output_values.end(), 0.0f);
                std::fprintf(stderr, "[vivid] GPU operator '%s' threw: %s\n",
                             cn.node_id.c_str(), ex.what());
            } catch (...) {
                cn.errored = true;
                cn.error_message = "Unknown exception";
                std::fill(cn.output_values.begin(), cn.output_values.end(), 0.0f);
            }

            if (gpu_ctx.operator_errored) {
                cn.gpu_shader_error = true;
                cn.gpu_shader_error_msg = gpu_ctx.operator_error_msg
                    ? gpu_ctx.operator_error_msg : "";
            }

            if (gpu_ctx.preferred_tex_width > 0 && gpu_ctx.preferred_tex_height > 0) {
                if (gpu_ctx.preferred_tex_width != cn.gpu_tex_width ||
                    gpu_ctx.preferred_tex_height != cn.gpu_tex_height) {
                    cn.gpu_tex_width = gpu_ctx.preferred_tex_width;
                    cn.gpu_tex_height = gpu_ctx.preferred_tex_height;
                    needs_gpu_realloc_ = true;
                }
            }
        } else {
            // Control processing — build VividFrameContext
            VividFrameContext ctx{};
            ctx.time = time;
            ctx.delta_time = delta_time;
            ctx.frame = frame;
            ctx.param_values = cn.param_values.data();
            ctx.input_values = cn.input_values.data();
            ctx.output_values = cn.output_values.data();
            ctx.input_spreads = cn.c_in_spreads.data();
            ctx.output_spreads = cn.c_out_spreads.data();
            ctx.custom_inputs = cn.resolved_custom_inputs.data();
            ctx.custom_input_count = static_cast<uint32_t>(cn.resolved_custom_inputs.size());
            ctx.custom_outputs = cn.custom_output_buf.data();
            ctx.custom_output_count = static_cast<uint32_t>(cn.custom_output_buf.size());
            ctx.input_string_values = cn.c_input_string_values.data();
            ctx.output_string_values = cn.c_output_string_values.data();
            ctx.input_string_spreads = cn.c_in_string_spreads.data();
            ctx.output_string_spreads = cn.c_out_string_spreads.data();
            ctx.file_param_values = cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data();
            ctx.file_param_count = static_cast<uint32_t>(cn.file_param_ptrs.size());
            ctx.input = const_cast<void*>(static_cast<const void*>(input));
            ctx.shared_handles = vivid::shared_handle_service();
            ctx.preferred_tex_width = 0;
            ctx.preferred_tex_height = 0;

            try {
                cn.loader->process_frame(cn.instance, &ctx);
            } catch (const std::exception& ex) {
                cn.errored = true;
                cn.error_message = ex.what();
                std::fill(cn.output_values.begin(), cn.output_values.end(), 0.0f);
            } catch (...) {
                cn.errored = true;
                cn.error_message = "unknown exception in process_frame()";
                std::fill(cn.output_values.begin(), cn.output_values.end(), 0.0f);
            }

            if (ctx.preferred_tex_width > 0 && ctx.preferred_tex_height > 0) {
                if (ctx.preferred_tex_width != cn.gpu_tex_width ||
                    ctx.preferred_tex_height != cn.gpu_tex_height) {
                    cn.gpu_tex_width = ctx.preferred_tex_width;
                    cn.gpu_tex_height = ctx.preferred_tex_height;
                    needs_gpu_realloc_ = true;
                }
            }
        }

        // ── Output readback ─────────────────────────────────────────────
        cn.custom_outputs = cn.custom_output_buf;
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            if (cn.c_out_spreads[p].length > 0) {
                cn.output_spreads[p].assign(
                    cn.out_spread_buf[p].begin(),
                    cn.out_spread_buf[p].begin() + cn.c_out_spreads[p].length);
            } else {
                cn.output_spreads[p].clear();
            }
        }
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            if (cn.c_output_string_values[p])
                cn.output_string_values[p] = cn.c_output_string_values[p];
        }
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            if (cn.c_out_string_spreads[p].length > 0) {
                cn.output_string_spreads[p].resize(cn.c_out_string_spreads[p].length);
                for (uint32_t si = 0; si < cn.c_out_string_spreads[p].length; ++si) {
                    if (cn.out_string_spread_ptr_buf[p][si])
                        cn.output_string_spreads[p][si] = cn.out_string_spread_ptr_buf[p][si];
                }
            } else {
                cn.output_string_spreads[p].clear();
            }
        }

        // ── PostNodeFn callback ─────────────────────────────────────────
        if (cn.is_gpu && on_gpu_node && cn.gpu_texture_view)
            on_gpu_node(ni, cn.node_id, cn.gpu_texture_view);

        // ── Generation update ───────────────────────────────────────────
        bool changed = cn.is_gpu;  // GPU nodes always bump
        if (!changed) {
            if (cn.output_values != cn.prev_output_values) changed = true;
        }
        if (!changed) {
            for (uint32_t p = 0; p < cn.output_port_count && !changed; ++p) {
                if (!cn.output_spreads[p].empty()) changed = true;
            }
        }
        if (changed) {
            cn.generation++;
            cn.prev_output_values = cn.output_values;
        }
        cn.last_processed_gen = cn.generation;
        cn.processed_this_tick = true;
    }

    // Sync audio-cadence nodes' last_processed_gen
    for (uint32_t ni : cg.audio_order) {
        cg.nodes[ni].last_processed_gen = cg.nodes[ni].generation;
    }
}

void FrameExecutor::set_solo(int node_idx) {
    solo_node_idx_ = node_idx;
    // TODO: Rebuild solo_active_set_ from graph topology
}

int FrameExecutor::find_gpu_sink(const CompiledGraph& cg) const {
    for (uint32_t ni : cg.frame_order) {
        if (cg.nodes[ni].is_gpu_sink) return static_cast<int>(ni);
    }
    return -1;
}

int FrameExecutor::find_effective_gpu_sink(const CompiledGraph& cg) const {
    if (solo_node_idx_ >= 0 &&
        static_cast<uint32_t>(solo_node_idx_) < cg.nodes.size() &&
        cg.nodes[solo_node_idx_].has_texture_output)
        return solo_node_idx_;
    return find_gpu_sink(cg);
}

void FrameExecutor::allocate_gpu_textures(CompiledGraph& /*cg*/, WGPUDevice /*device*/,
                                          uint32_t /*default_w*/, uint32_t /*default_h*/,
                                          WGPUTextureFormat /*format*/,
                                          WGPUTextureUsage /*extra_usage*/) {
    // TODO: Port from Scheduler::allocate_gpu_textures()
}

} // namespace vivid
