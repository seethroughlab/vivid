#include "runtime/graph/frame_executor.h"
#include "runtime/graph/lane_buffer_gpu.h"
#include "runtime/core/crash_guard.h"
#include "runtime/core/shared_handle_registry.h"
#include "common/gpu_util.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/type_id.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace vivid {

// tick() processes all frame-cadence nodes once per frame in topological order.
//
// Per-node steps: zero inputs → propagate upstream wire values (with lane
// merging when lane counts match) → call process_frame() or dispatch the GPU
// callback. Solo mode zeros non-solo GPU nodes and skips their processing.
// Lane contexts are reinitialized each tick because a graph rebuild can produce
// a different node layout with the same frame_order length. Retired lane IDs
// are swept at tick start.
void FrameExecutor::tick(CompiledGraph& cg, const GraphMetronomeSample& metronome, double time,
                         double delta_time,
                         uint64_t frame, void* gpu_state,
                         PostNodeFn on_gpu_node,
                         const VividInputState* input) {
    // Reset per-tick flags
    for (auto& cn : cg.nodes) cn.processed_this_tick = false;

    // Initialize per-node lane contexts for LoopBased frame operators.
    // Always reinitialize (not just on size change) because a rebuild can
    // produce a different graph with the same frame_order length but
    // different node indices.
    frame_lane_contexts_.resize(cg.frame_order.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(cg.frame_order.size()); ++i) {
        frame_lane_contexts_[i].service = &frame_lane_state_;
        frame_lane_contexts_[i].node_idx = cg.frame_order[i];
    }
    frame_lane_state_.sweep_retired();
    lane_pool_.sweep();

    for (uint32_t fi_ord = 0; fi_ord < static_cast<uint32_t>(cg.frame_order.size()); ++fi_ord) {
        uint32_t ni = cg.frame_order[fi_ord];
        auto& cn = cg.nodes[ni];

        // Clear transient GPU shader error each tick
        if (cn.is_gpu()) {
            cn.gpu->shader_error = false;
            cn.gpu->shader_error_msg.clear();
        }

        // Skip errored nodes — zero outputs
        if (cn.errored) {
            std::fill(cn.output_values.begin(), cn.output_values.end(), 0.0f);
            for (auto& ref : cn.output_lane_refs) ref = {};
            for (auto& sp : cn.output_lanes) sp.clear();
            for (auto& sp : cn.output_string_lanes) sp.clear();
            std::fill(cn.output_string_values.begin(), cn.output_string_values.end(), std::string());
            continue;
        }

        // Solo mode: skip non-active nodes
        if (solo_node_idx_ >= 0 && !solo_active_set_.empty() && !solo_active_set_[ni]) {
            std::fill(cn.output_values.begin(), cn.output_values.end(), 0.0f);
            for (auto& ref : cn.output_lane_refs) ref = {};
            for (auto& sp : cn.output_lanes) sp.clear();
            continue;
        }

        // Zero inputs
        std::fill(cn.input_values.begin(), cn.input_values.end(), 0.0f);
        std::fill(cn.input_string_values.begin(), cn.input_string_values.end(), std::string());
        for (auto& ref : cn.input_lane_refs) ref = {};
        for (auto& sp : cn.input_lanes) sp.clear();
        for (auto& sp : cn.input_string_lanes) sp.clear();

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
            if (e.data_type == VIVID_PORT_STRING_LANES) {
                cn.input_string_lanes[e.to_port] = from_cn.output_string_lanes[e.from_port];
                continue;
            }

            // Float wire (SIGNAL) with optional remap
            float raw = e.sources_param
                ? from_cn.param_values[e.from_port]
                : from_cn.output_values[e.from_port];
            float val = e.has_remap() ? e.apply_remap(raw) : raw;

            if (e.targets_param) {
                if (cn.param_lock_flags.size() > e.to_port &&
                    (cn.param_lock_flags[e.to_port] & 1))
                    continue;  // PARAM_LOCK_WIRES
                cn.param_values[e.to_port] = val;
            } else {
                cn.input_values[e.to_port] = val;

                // Lane-aware ref-based propagation.
                // No cycle-expand, no modulo indexing. Compiler legality
                // (Pass 2.6) guarantees that non-scalar inputs sharing a
                // port have the same lane_set_id.
                if (!e.sources_param && e.from_port < from_cn.output_lane_refs.size()) {
                    const auto& src_ref = from_cn.output_lane_refs[e.from_port];
                    if (src_ref) {
                        auto& dst_ref = cn.input_lane_refs[e.to_port];
                        if (dst_ref.empty()) {
                            // First non-scalar wire: establish destination.
                            if (!e.has_remap()) {
                                // Passthrough — share ref (zero copy).
                                dst_ref = src_ref;
                            } else {
                                // Remap — materialize new buffer.
                                LaneBuffer* buf = lane_pool_.acquire();
                                uint32_t len = src_ref.length();
                                float* dst = buf->resize(len);
                                if (dst) {
                                    const float* src = src_ref.data();
                                    for (uint32_t j = 0; j < len; ++j)
                                        dst[j] = e.apply_remap(src[j]);
                                    buf->commit(len);
                                }
                                dst_ref = LaneBufferRef(buf);
                            }
                        } else if (src_ref.length() == dst_ref.length()) {
                            // Same-provenance, same length: element-wise add (COW).
                            uint32_t len = dst_ref.length();
                            // COW: if shared, copy first.
                            LaneBuffer* dst_buf = dst_ref.buf;
                            if (dst_buf->ref_count.load(std::memory_order_relaxed) > 1) {
                                LaneBuffer* new_buf = lane_pool_.acquire();
                                float* nd = new_buf->resize(len);
                                if (nd) std::memcpy(nd, dst_ref.data(), len * sizeof(float));
                                new_buf->commit(len);
                                dst_ref = LaneBufferRef(new_buf);
                                dst_buf = dst_ref.buf;
                            }
                            float* dd = dst_buf->data.data();
                            const float* sd = src_ref.data();
                            for (uint32_t j = 0; j < len; ++j) {
                                float sv = sd[j];
                                if (e.has_remap()) sv = e.apply_remap(sv);
                                dd[j] += sv;
                            }
                        } else {
                            // Same-provenance but different runtime length.
                            std::fprintf(stderr,
                                "[vivid] frame_executor: lane length mismatch at node '%s' "
                                "port %u (dst %u vs src %u) — skipping merge\n",
                                cn.node_id.c_str(), e.to_port,
                                dst_ref.length(), src_ref.length());
                            assert(false && "lane-aware lane merge: same-provenance runtime length mismatch");
                        }
                        if (!dst_ref.empty())
                            cn.input_values[e.to_port] = dst_ref.data()[0];
                    } else if (e.data_type == VIVID_PORT_LANE_ARRAY) {
                        // Scalar source → lane_array destination: lift
                        // the scalar into a 1-element lane array.
                        auto& dst_ref = cn.input_lane_refs[e.to_port];
                        if (dst_ref.empty()) {
                            LaneBuffer* buf = lane_pool_.acquire();
                            buf->data[0] = val;
                            buf->commit(1);
                            dst_ref = LaneBufferRef(buf);
                        } else {
                            // Add scalar to all elements (COW).
                            uint32_t len = dst_ref.length();
                            LaneBuffer* dst_buf = dst_ref.buf;
                            if (dst_buf->ref_count.load(std::memory_order_relaxed) > 1) {
                                LaneBuffer* new_buf = lane_pool_.acquire();
                                float* nd = new_buf->resize(len);
                                if (nd) std::memcpy(nd, dst_ref.data(), len * sizeof(float));
                                new_buf->commit(len);
                                dst_ref = LaneBufferRef(new_buf);
                                dst_buf = dst_ref.buf;
                            }
                            for (uint32_t j = 0; j < len; ++j)
                                dst_buf->data[j] += val;
                        }
                    }
                }
            }
        }

        // ── Apply audio→frame bridge values (survive per-frame zeroing) ──
        for (size_t p = 0; p < cn.bridge_input_dirty.size() && p < cn.input_values.size(); ++p) {
            if (cn.bridge_input_dirty[p]) {
                cn.input_values[p] = cn.bridge_input_values[p];
                cn.bridge_input_dirty[p] = 0;
            }
        }

        // ── Lane count normalization ────────────────────────────────────
        // Expand shorter input lane refs to match the longest, repeating
        // the last element.  Ensures GPU and Kernel operators see uniform
        // lane counts across all input ports.
        {
            uint32_t max_lanes = 0;
            for (uint32_t p = 0; p < cn.input_port_count; ++p) {
                if (cn.input_lane_refs[p].length() > max_lanes)
                    max_lanes = cn.input_lane_refs[p].length();
            }
            if (max_lanes > 1) {
                for (uint32_t p = 0; p < cn.input_port_count; ++p) {
                    auto& ref = cn.input_lane_refs[p];
                    if (!ref.empty() && ref.length() < max_lanes) {
                        // Materialize expanded buffer.
                        LaneBuffer* buf = lane_pool_.acquire();
                        uint32_t old_len = ref.length();
                        float fill = ref.data()[old_len - 1];
                        float* dst = buf->resize(max_lanes);
                        if (dst) {
                            std::memcpy(dst, ref.data(), old_len * sizeof(float));
                            std::fill(dst + old_len, dst + max_lanes, fill);
                            buf->commit(max_lanes);
                        }
                        ref = LaneBufferRef(buf);
                    }
                }
            }
        }

        // ── Skip logic ──────────────────────────────────────────────────
        // Process if: time-dependent, root node, any upstream processed this
        // tick, any audio upstream has new data (dirty), or self was externally
        // modified (dirty from bridge/API/reload).
        bool should_process = cn.time_dependent || cn.upstream_nodes.empty() || cn.dirty;
        if (!should_process) {
            for (uint32_t up : cn.upstream_nodes) {
                const auto& up_cn = cg.nodes[up];
                if (up_cn.processed_this_tick || up_cn.dirty) {
                    should_process = true; break;
                }
            }
        }
        if (!should_process) continue;

        // ── Build lane view/output staging ──────────────────────────────
        for (uint32_t p = 0; p < cn.input_port_count; ++p) {
            const auto& ref = cn.input_lane_refs[p];
            cn.c_in_lane_views[p].data = ref.data();
            cn.c_in_lane_views[p].length = ref.length();
            // Propagate lane provenance from compile-time metadata or ref.
            cn.c_in_lane_views[p].lane_set_id =
                (ref && ref.buf->lane_set_id != 0) ? ref.buf->lane_set_id
                : (p < cn.input_lane_sets.size() && !cn.input_lane_sets[p].is_scalar())
                    ? cn.input_lane_sets[p].lane_set_id : 0;
            cn.c_in_lane_views[p].flags = 0;
        }
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            cn.out_lane_bufs[p].reset();
        }
        for (uint32_t p = 0; p < cn.input_port_count; ++p) {
            for (size_t si = 0; si < cn.input_string_lanes[p].size() && si < cn.in_string_lane_ptrs[p].size(); ++si)
                cn.in_string_lane_ptrs[p][si] = cn.input_string_lanes[p][si].c_str();
            cn.c_in_string_lane_views[p].data = cn.in_string_lane_ptrs[p].data();
            cn.c_in_string_lane_views[p].length = static_cast<uint32_t>(cn.input_string_lanes[p].size());
            cn.c_in_string_lane_views[p].lane_set_id = 0;
            cn.c_in_string_lane_views[p].flags = 0;
        }
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            cn.out_string_lane_bufs[p].reset();
        }
        std::fill(cn.custom_output_buf.begin(), cn.custom_output_buf.end(), nullptr);

        for (auto& sv : cn.c_input_string_values)
            sv = nullptr;
        for (uint32_t p = 0; p < cn.input_port_count; ++p)
            cn.c_input_string_values[p] = cn.input_string_values[p].c_str();
        for (auto& sv : cn.c_output_string_values)
            sv = nullptr;

        // ── Process ─────────────────────────────────────────────────────
        if (cn.missing_operator || !cn.loader || !cn.instance) {
            std::fill(cn.output_values.begin(), cn.output_values.end(), 0.0f);
        } else if (cn.is_gpu() && gpu_state) {
            // ── GPU path: build VividGpuContext ─────────────────────────
            auto* base_gpu = static_cast<VividGpuContext*>(gpu_state);
            VividGpuContext gpu_ctx{};

            gpu_ctx.time          = time;
            gpu_ctx.delta_time    = delta_time;
            gpu_ctx.frame         = frame;
            gpu_ctx.node_id       = cn.node_id.c_str();
            gpu_ctx.param_values  = cn.param_values.data();
            gpu_ctx.input_values     = cn.input_values.data();
            gpu_ctx.output_values    = cn.output_values.data();
            gpu_ctx.input_connected  = cn.input_connected.data();
            gpu_ctx.input_lanes  = cn.c_in_lane_views.empty() ? nullptr : cn.c_in_lane_views.data();
            gpu_ctx.output_lanes = cn.c_out_lane_outputs.empty() ? nullptr : cn.c_out_lane_outputs.data();

            // GPU storage-buffer lane inputs (Phase 4).
            if (!cn.gpu->lane_input_gpu_promoted.empty()) {
                auto* base_gpu2 = static_cast<VividGpuContext*>(gpu_state);
                for (uint32_t p = 0; p < cn.input_port_count; ++p) {
                    if (p < cn.gpu->lane_input_gpu_promoted.size() &&
                        cn.gpu->lane_input_gpu_promoted[p] &&
                        p < cn.input_lane_refs.size() && cn.input_lane_refs[p]) {
                        lane_buffer_ensure_gpu(cn.input_lane_refs[p].buf,
                                               base_gpu2->device, base_gpu2->queue);
                        cn.gpu->resolved_lane_gpu_bufs[p] = cn.input_lane_refs[p].buf->gpu_buffer;
                        cn.gpu->resolved_lane_gpu_lengths[p] = cn.input_lane_refs[p].buf->committed_length;
                    } else if (p < cn.gpu->resolved_lane_gpu_bufs.size()) {
                        cn.gpu->resolved_lane_gpu_bufs[p] = nullptr;
                        cn.gpu->resolved_lane_gpu_lengths[p] = 0;
                    }
                }
                gpu_ctx.input_lane_gpu_buffers = cn.gpu->resolved_lane_gpu_bufs.data();
                gpu_ctx.input_lane_gpu_lengths = cn.gpu->resolved_lane_gpu_lengths.data();
                gpu_ctx.input_lane_gpu_count = static_cast<uint32_t>(cn.gpu->resolved_lane_gpu_bufs.size());
            } else {
                gpu_ctx.input_lane_gpu_buffers = nullptr;
                gpu_ctx.input_lane_gpu_lengths = nullptr;
                gpu_ctx.input_lane_gpu_count = 0;
            }
            gpu_ctx.input_string_values = cn.c_input_string_values.empty()
                                        ? nullptr : cn.c_input_string_values.data();
            gpu_ctx.output_string_values = cn.c_output_string_values.empty()
                                        ? nullptr : cn.c_output_string_values.data();
            gpu_ctx.input_string_lanes = cn.c_in_string_lane_views.empty()
                                        ? nullptr : cn.c_in_string_lane_views.data();
            gpu_ctx.output_string_lanes = cn.c_out_string_lane_outputs.empty()
                                        ? nullptr : cn.c_out_string_lane_outputs.data();
            gpu_ctx.file_param_values = cn.file_param_ptrs.empty()
                                        ? nullptr : cn.file_param_ptrs.data();
            gpu_ctx.file_param_count  = static_cast<uint32_t>(cn.file_param_ptrs.size());
            gpu_ctx.input = input;
            gpu_ctx.shared_handles = vivid::shared_handle_service();
            populate_metronome_context(gpu_ctx, metronome);
            gpu_ctx.preferred_tex_width  = 0;
            gpu_ctx.preferred_tex_height = 0;

            // GPU-specific resources
            gpu_ctx.device          = base_gpu->device;
            gpu_ctx.queue           = base_gpu->queue;
            gpu_ctx.command_encoder = base_gpu->command_encoder;
            gpu_ctx.output_format   = base_gpu->output_format;
            gpu_ctx.output_texture      = cn.gpu->texture;
            gpu_ctx.output_texture_view = cn.gpu->texture_view;
            gpu_ctx.output_width    = cn.gpu->tex_width;
            gpu_ctx.output_height   = cn.gpu->tex_height;

            // Resolve texture inputs from upstream via edges
            size_t tex_count = cn.gpu->texture_input_port_indices.size();
            cn.gpu->resolved_tex_inputs.clear();
            cn.gpu->resolved_tex_inputs.resize(tex_count, nullptr);
            cn.gpu->resolved_tex_raw.clear();
            cn.gpu->resolved_tex_raw.resize(tex_count, nullptr);
            cn.gpu->resolved_tex_widths.clear();
            cn.gpu->resolved_tex_widths.resize(tex_count, 0);
            cn.gpu->resolved_tex_heights.clear();
            cn.gpu->resolved_tex_heights.resize(tex_count, 0);
            for (size_t ti = 0; ti < tex_count; ++ti) {
                uint32_t port_idx = cn.gpu->texture_input_port_indices[ti];
                for (uint32_t ei : cg.frame_direct_edges) {
                    const auto& e = cg.edges[ei];
                    if (e.to_node == ni && !e.targets_param &&
                        e.to_port == port_idx && e.data_type == VIVID_PORT_TEXTURE) {
                        const auto& upstream = cg.nodes[e.from_node];
                        bool routed_aux = false;
                        for (size_t ai = 0; ai < upstream.gpu->aux_texture_output_port_indices.size(); ++ai) {
                            if (e.from_port ==
                                    static_cast<uint32_t>(upstream.gpu->aux_texture_output_port_indices[ai])) {
                                cn.gpu->resolved_tex_inputs[ti] = upstream.gpu->aux_gpu_texture_views[ai];
                                cn.gpu->resolved_tex_raw[ti]    = upstream.gpu->aux_gpu_textures[ai];
                                routed_aux = true;
                                break;
                            }
                        }
                        if (!routed_aux) {
                            cn.gpu->resolved_tex_inputs[ti] = upstream.gpu->texture_view;
                            cn.gpu->resolved_tex_raw[ti]    = upstream.gpu->texture;
                        }
                        cn.gpu->resolved_tex_widths[ti]  = upstream.gpu->tex_width;
                        cn.gpu->resolved_tex_heights[ti] = upstream.gpu->tex_height;
                        break;
                    }
                }
            }
            gpu_ctx.input_texture_views = cn.gpu->resolved_tex_inputs.empty()
                                            ? nullptr : cn.gpu->resolved_tex_inputs.data();
            gpu_ctx.input_texture_count = static_cast<uint32_t>(tex_count);
            gpu_ctx.input_textures       = cn.gpu->resolved_tex_raw.empty()
                                            ? nullptr : cn.gpu->resolved_tex_raw.data();
            gpu_ctx.input_texture_widths  = cn.gpu->resolved_tex_widths.empty()
                                            ? nullptr : cn.gpu->resolved_tex_widths.data();
            gpu_ctx.input_texture_heights = cn.gpu->resolved_tex_heights.empty()
                                            ? nullptr : cn.gpu->resolved_tex_heights.data();
            gpu_ctx.operators_src_dir = operators_src_dir_.empty()
                                            ? nullptr : operators_src_dir_.c_str();
            gpu_ctx.aux_output_texture_views = cn.gpu->aux_gpu_texture_views.empty()
                ? nullptr : cn.gpu->aux_gpu_texture_views.data();
            gpu_ctx.aux_output_texture_count =
                static_cast<uint32_t>(cn.gpu->aux_gpu_texture_views.size());

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
                cn.gpu->shader_error = true;
                cn.gpu->shader_error_msg = gpu_ctx.operator_error_msg
                    ? gpu_ctx.operator_error_msg : "";
            }

            if (gpu_ctx.preferred_tex_width > 0 && gpu_ctx.preferred_tex_height > 0) {
                if (gpu_ctx.preferred_tex_width != cn.gpu->tex_width ||
                    gpu_ctx.preferred_tex_height != cn.gpu->tex_height) {
                    cn.gpu->tex_width = gpu_ctx.preferred_tex_width;
                    cn.gpu->tex_height = gpu_ctx.preferred_tex_height;
                    needs_gpu_realloc_ = true;
                }
            }
        } else if (cn.frame_execution_strategy == LaneExecutionStrategy::LoopBased) {
            // ── LoopBased frame processing: per-lane loop ──
            // Discover lane count from max input lane ref length.
            uint32_t loop_lanes = 0;
            for (uint32_t p = 0; p < cn.input_port_count; ++p) {
                if (cn.input_lane_refs[p].length() > loop_lanes)
                    loop_lanes = cn.input_lane_refs[p].length();
            }

            // Read identity-bearing lane_ids from lane ref, or fall back to positional.
            std::vector<uint32_t> loop_lane_ids(loop_lanes);
            int32_t lid_port = cn.frame_lane_id_port;
            bool has_identity_ids = false;
            if (lid_port >= 0 && static_cast<uint32_t>(lid_port) < cn.input_lane_refs.size()) {
                const auto& lid_ref = cn.input_lane_refs[lid_port];
                if (lid_ref.length() >= loop_lanes) {
                    for (uint32_t c = 0; c < loop_lanes; ++c)
                        loop_lane_ids[c] = static_cast<uint32_t>(lid_ref.data()[c]);
                    has_identity_ids = true;
                }
            }
            if (!has_identity_ids) {
                for (uint32_t c = 0; c < loop_lanes; ++c)
                    loop_lane_ids[c] = c + 1;
            }

            // lane_set_id from compilation
            uint32_t lane_set_id = 0;
            for (const auto& ils : cn.input_lane_sets) {
                if (!ils.is_scalar()) { lane_set_id = ils.lane_set_id; break; }
            }

            // Per-lane scratch for input/output values
            std::vector<float> lane_input_values(cn.input_port_count, 0.0f);
            std::vector<float> lane_output_values(cn.output_port_count, 0.0f);

            // Pre-commit output lane buffers to loop_lanes length
            for (uint32_t p = 0; p < cn.output_port_count; ++p) {
                if (loop_lanes <= static_cast<uint32_t>(cn.out_lane_bufs[p].data.size()))
                    cn.out_lane_bufs[p].committed_length = loop_lanes;
            }

            for (uint32_t c = 0; c < loop_lanes; ++c) {
                // Extract per-lane scalar from each input lane ref
                for (uint32_t p = 0; p < cn.input_port_count; ++p) {
                    if (cn.input_lane_refs[p] && c < cn.input_lane_refs[p].length())
                        lane_input_values[p] = cn.input_lane_refs[p].data()[c];
                    else
                        lane_input_values[p] = cn.input_values[p];  // scalar fallback
                }

                // Reset per-lane output
                std::fill(lane_output_values.begin(), lane_output_values.end(), 0.0f);

                VividFrameContext ctx{};
                ctx.time = time;
                ctx.delta_time = delta_time;
                ctx.frame = frame;
                ctx.node_id = cn.node_id.c_str();
                ctx.param_values = cn.param_values.data();
                ctx.input_values = lane_input_values.data();
                ctx.output_values = lane_output_values.data();
                ctx.input_lanes = cn.c_in_lane_views.data();
                ctx.output_lanes = cn.c_out_lane_outputs.data();
                ctx.custom_inputs = cn.resolved_custom_inputs.data();
                ctx.custom_input_count = static_cast<uint32_t>(cn.resolved_custom_inputs.size());
                ctx.custom_outputs = cn.custom_output_buf.data();
                ctx.custom_output_count = static_cast<uint32_t>(cn.custom_output_buf.size());
                ctx.input_string_values = cn.c_input_string_values.data();
                ctx.output_string_values = cn.c_output_string_values.data();
                ctx.input_string_lanes = cn.c_in_string_lane_views.data();
                ctx.output_string_lanes = cn.c_out_string_lane_outputs.data();
                ctx.file_param_values = cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data();
                ctx.file_param_count = static_cast<uint32_t>(cn.file_param_ptrs.size());
                ctx.input = const_cast<void*>(static_cast<const void*>(input));
                ctx.shared_handles = vivid::shared_handle_service();
                ctx.preferred_tex_width = 0;
                ctx.preferred_tex_height = 0;
                ctx.lane_count = loop_lanes;
                ctx.lane_index = c;
                ctx.lane_set_id = lane_set_id;
                ctx.lane_id = loop_lane_ids[c];
                ctx.lane_state_fn = lane_state_fn_bridge;
                ctx.lane_state_service = &frame_lane_contexts_[fi_ord];
                ctx.allocate_lane_id_fn = allocate_lane_id_fn_bridge;
                ctx.retire_lane_id_fn = retire_lane_id_fn_bridge;
                populate_metronome_context(ctx, metronome);

                try {
                    vivid::CrashGuard guard(cn.node_id.c_str());
                    cn.loader->process_frame(cn.instance, &ctx);
                } catch (const std::exception& ex) {
                    cn.errored = true;
                    cn.error_message = ex.what();
                    break;
                } catch (...) {
                    cn.errored = true;
                    cn.error_message = "unknown exception in process_frame()";
                    break;
                }

                // Write per-lane output into output lane buffer
                for (uint32_t p = 0; p < cn.output_port_count; ++p) {
                    if (cn.out_lane_bufs[p].committed_length > c)
                        cn.out_lane_bufs[p].data[c] = lane_output_values[p];
                }
            }

            // Set scalar output = first lane
            for (uint32_t p = 0; p < cn.output_port_count; ++p) {
                cn.output_values[p] = (loop_lanes > 0 && cn.out_lane_bufs[p].committed_length > 0)
                    ? cn.out_lane_bufs[p].data[0] : 0.0f;
            }

            if (cn.errored) {
                std::fill(cn.output_values.begin(), cn.output_values.end(), 0.0f);
            }
        } else {
            // ── Normal (non-lifted) control processing ──
            VividFrameContext ctx{};
            ctx.time = time;
            ctx.delta_time = delta_time;
            ctx.frame = frame;
            ctx.node_id = cn.node_id.c_str();
            ctx.param_values = cn.param_values.data();
            ctx.input_values = cn.input_values.data();
            ctx.output_values = cn.output_values.data();
            ctx.input_lanes = cn.c_in_lane_views.data();
            ctx.output_lanes = cn.c_out_lane_outputs.data();
            ctx.custom_inputs = cn.resolved_custom_inputs.data();
            ctx.custom_input_count = static_cast<uint32_t>(cn.resolved_custom_inputs.size());
            ctx.custom_outputs = cn.custom_output_buf.data();
            ctx.custom_output_count = static_cast<uint32_t>(cn.custom_output_buf.size());
            ctx.input_string_values = cn.c_input_string_values.data();
            ctx.output_string_values = cn.c_output_string_values.data();
            ctx.input_string_lanes = cn.c_in_string_lane_views.data();
            ctx.output_string_lanes = cn.c_out_string_lane_outputs.data();
            ctx.file_param_values = cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data();
            ctx.file_param_count = static_cast<uint32_t>(cn.file_param_ptrs.size());
            ctx.input = const_cast<void*>(static_cast<const void*>(input));
            ctx.shared_handles = vivid::shared_handle_service();
            ctx.preferred_tex_width = 0;
            ctx.preferred_tex_height = 0;
            populate_metronome_context(ctx, metronome);

            // Lane metadata.
            uint32_t max_lane_len = 0;
            for (uint32_t p = 0; p < cn.input_port_count; ++p) {
                if (cn.input_lane_refs[p].length() > max_lane_len)
                    max_lane_len = cn.input_lane_refs[p].length();
            }
            ctx.lane_count = max_lane_len > 1 ? max_lane_len : 1;
            ctx.lane_index = 0;
            ctx.lane_set_id = 0;
            for (const auto& ils : cn.input_lane_sets) {
                if (!ils.is_scalar()) { ctx.lane_set_id = ils.lane_set_id; break; }
            }

            try {
                vivid::CrashGuard guard(cn.node_id.c_str());
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
                if (ctx.preferred_tex_width != cn.gpu->tex_width ||
                    ctx.preferred_tex_height != cn.gpu->tex_height) {
                    cn.gpu->tex_width = ctx.preferred_tex_width;
                    cn.gpu->tex_height = ctx.preferred_tex_height;
                    needs_gpu_realloc_ = true;
                }
            }
        }

        // ── Output readback — publish refs ─────────────────────────────
        cn.custom_outputs = cn.custom_output_buf;
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            if (cn.out_lane_bufs[p].committed_length > 0) {
                cn.output_lane_refs[p] = make_ref_from_existing(&cn.out_lane_bufs[p]);
            } else {
                cn.output_lane_refs[p] = {};
            }
        }
        // Sync output_lane_refs → output_lanes for consumers that still read
        // the old field (tests, bridge analysis injection display).
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            const auto& ref = cn.output_lane_refs[p];
            if (ref)
                cn.output_lanes[p].assign(ref.data(), ref.data() + ref.length());
            else
                cn.output_lanes[p].clear();
        }
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            if (cn.c_output_string_values[p])
                cn.output_string_values[p] = cn.c_output_string_values[p];
        }
        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
            uint32_t slen = cn.out_string_lane_bufs[p].committed_length;
            if (slen > 0) {
                cn.output_string_lanes[p].resize(slen);
                for (uint32_t si = 0; si < slen; ++si)
                    cn.output_string_lanes[p][si] = cn.out_string_lane_bufs[p].owned[si];
            } else {
                cn.output_string_lanes[p].clear();
            }
        }

        // ── GPU frame analysis (readback + metrics) ───────────────────
        if (cn.is_gpu() && cn.gpu && analysis_enabled_ &&
            cn.gpu->analysis_frame_hash_idx != UINT32_MAX) {
            auto& fa = cn.gpu->frame_analysis;
            if (!fa) {
                fa = std::make_unique<GpuFrameAnalysis>();
            }
            auto* base_gpu = static_cast<VividGpuContext*>(gpu_state);
            if (base_gpu && !fa->is_inited()) {
                fa->init(base_gpu->device);
            }
            // Process previous frame's readback and write metrics.
            fa->compute_metrics();
            fa->inject(cn.output_values.data(),
                       cn.gpu->analysis_frame_hash_idx,
                       cn.gpu->analysis_brightness_idx,
                       cn.gpu->analysis_contrast_idx,
                       cn.gpu->analysis_dominant_hue_idx);
            // Queue readback for this frame's visible texture. GPU sinks such
            // as video_out do not own an output texture, so analyze their input.
            WGPUTextureView analysis_view = cn.gpu->texture_view;
            uint32_t analysis_w = cn.gpu->tex_width;
            uint32_t analysis_h = cn.gpu->tex_height;
            if (!analysis_view && cn.gpu->is_sink &&
                !cn.gpu->resolved_tex_inputs.empty()) {
                analysis_view = cn.gpu->resolved_tex_inputs[0];
                analysis_w = cn.gpu->resolved_tex_widths.empty()
                    ? 0 : cn.gpu->resolved_tex_widths[0];
                analysis_h = cn.gpu->resolved_tex_heights.empty()
                    ? 0 : cn.gpu->resolved_tex_heights[0];
            }
            if (base_gpu && analysis_view && analysis_w > 0 && analysis_h > 0) {
                fa->queue_readback(base_gpu->command_encoder, base_gpu->queue,
                                   analysis_view, analysis_w, analysis_h);
            }
        }

        // ── PostNodeFn callback ─────────────────────────────────────────
        if (cn.is_gpu() && on_gpu_node) {
            WGPUTextureView tex = cn.gpu->texture_view;
            // GPU sinks (e.g. video_out) have no output — use input texture
            if (!tex && cn.gpu->is_sink && !cn.gpu->resolved_tex_inputs.empty())
                tex = cn.gpu->resolved_tex_inputs[0];
            if (tex)
                on_gpu_node(ni, cn.node_id, tex);
        }

        // ── Mark processed ──────────────────────────────────────────────
        cn.dirty = false;
        cn.processed_this_tick = true;
    }

    // Clear dirty on audio nodes — their updates have been consumed this tick.
    for (uint32_t ni : cg.audio_order) {
        cg.nodes[ni].dirty = false;
    }
}

void FrameExecutor::set_solo(int node_idx, const std::vector<bool>& active_set) {
    solo_node_idx_ = node_idx;
    solo_active_set_ = active_set;
}

int FrameExecutor::find_gpu_sink(const CompiledGraph& cg) const {
    for (uint32_t ni : cg.frame_order) {
        if (cg.nodes[ni].is_gpu_sink()) return static_cast<int>(ni);
    }
    return -1;
}

int FrameExecutor::find_effective_gpu_sink(const CompiledGraph& cg) const {
    if (solo_node_idx_ >= 0 &&
        static_cast<uint32_t>(solo_node_idx_) < cg.nodes.size() &&
        cg.nodes[solo_node_idx_].has_texture_output())
        return solo_node_idx_;
    return find_gpu_sink(cg);
}

void FrameExecutor::allocate_gpu_textures(CompiledGraph& cg, WGPUDevice device,
                                          uint32_t default_w, uint32_t default_h,
                                          WGPUTextureFormat format,
                                          WGPUTextureUsage extra_usage) {
    gpu_device_ = device;

    // Iterate nodes in topological order (they're already sorted)
    for (uint32_t ni = 0; ni < static_cast<uint32_t>(cg.nodes.size()); ++ni) {
        auto& cn = cg.nodes[ni];
        if (!cn.is_gpu()) continue;

        // Release existing primary textures.
        if (cn.gpu->texture_view) { wgpuTextureViewRelease(cn.gpu->texture_view); cn.gpu->texture_view = nullptr; }
        if (cn.gpu->texture) { wgpuTextureRelease(cn.gpu->texture); cn.gpu->texture = nullptr; }
        for (auto& v : cn.gpu->aux_gpu_texture_views) v = nullptr;
        for (auto& t : cn.gpu->aux_gpu_textures)      t = nullptr;

        // GPU sinks and scene-only nodes don't produce their own textures
        cn.gpu->tex_inherited = false;
        if (cn.is_gpu_sink() || !cn.has_texture_output()) {
            cn.gpu->tex_width  = 0;
            cn.gpu->tex_height = 0;
            continue;
        }

        // Resolve texture size
        uint32_t w = cn.gpu->tex_width;
        uint32_t h = cn.gpu->tex_height;

        // Nodes with texture inputs always inherit from upstream (filters).
        if (!cn.gpu->texture_input_port_indices.empty()) {
            uint32_t first_tex_port = cn.gpu->texture_input_port_indices[0];
            for (const auto& e : cg.edges) {
                if (e.to_node == ni && !e.targets_param &&
                    e.to_port == first_tex_port && e.data_type == VIVID_PORT_TEXTURE) {
                    const auto& upstream = cg.nodes[e.from_node];
                    if (upstream.gpu->tex_width > 0 && upstream.gpu->tex_height > 0) {
                        w = upstream.gpu->tex_width;
                        h = upstream.gpu->tex_height;
                        cn.gpu->tex_inherited = true;
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

        cn.gpu->tex_width  = w;
        cn.gpu->tex_height = h;

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
        cn.gpu->texture = wgpuDeviceCreateTexture(device, &tex_desc);
        if (!cn.gpu->texture) {
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
        cn.gpu->texture_view = wgpuTextureCreateView(cn.gpu->texture, &view_desc);
        if (!cn.gpu->texture_view) {
            std::fprintf(stderr, "[vivid] GPU texture view creation failed for node '%s'\n", cn.node_id.c_str());
            wgpuTextureRelease(cn.gpu->texture);
            cn.gpu->texture = nullptr;
            continue;
        }

        std::fprintf(stderr, "[vivid] Allocated %ux%u texture for node '%s'\n",
                     w, h, cn.node_id.c_str());
    }
}

bool FrameExecutor::has_gpu_operators(const CompiledGraph& cg) const {
    for (const auto& cn : cg.nodes)
        if (cn.is_gpu()) return true;
    return false;
}

bool FrameExecutor::gpu_sink_source_size(const CompiledGraph& cg, int sink_idx,
                                         uint32_t& w, uint32_t& h) const {
    for (const auto& e : cg.edges) {
        if (e.to_node == static_cast<uint32_t>(sink_idx) &&
            e.data_type == VIVID_PORT_TEXTURE && !e.targets_param) {
            const auto& up = cg.nodes[e.from_node];
            w = up.gpu->tex_width;
            h = up.gpu->tex_height;
            return w > 0 && h > 0;
        }
    }
    return false;
}

WGPUTexture FrameExecutor::gpu_sink_source_texture(const CompiledGraph& cg, int sink_idx) const {
    for (const auto& e : cg.edges) {
        if (e.to_node == static_cast<uint32_t>(sink_idx) &&
            e.data_type == VIVID_PORT_TEXTURE && !e.targets_param) {
            const auto& up = cg.nodes[e.from_node];
            for (size_t ai = 0; ai < up.gpu->aux_texture_output_port_indices.size(); ++ai) {
                if (e.from_port ==
                        static_cast<uint32_t>(up.gpu->aux_texture_output_port_indices[ai]))
                    return up.gpu->aux_gpu_textures[ai];
            }
            return up.gpu->texture;  // primary
        }
    }
    return nullptr;
}

void FrameExecutor::shutdown_gpu(CompiledGraph& cg) {
    for (auto& cn : cg.nodes) {
        if (!cn.gpu) continue;
        if (cn.gpu->texture_view) { wgpuTextureViewRelease(cn.gpu->texture_view); cn.gpu->texture_view = nullptr; }
        if (cn.gpu->texture) { wgpuTextureRelease(cn.gpu->texture); cn.gpu->texture = nullptr; }
        for (auto& v : cn.gpu->aux_gpu_texture_views) v = nullptr;
        for (auto& t : cn.gpu->aux_gpu_textures)      t = nullptr;
    }

    if (gpu_device_) {
        wgpuDevicePoll(gpu_device_, true, nullptr);
        gpu_device_ = nullptr;
    }
}

} // namespace vivid
