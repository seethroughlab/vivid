#include <miniaudio.h>

#include "runtime/audio_executor.h"
#include "runtime/audio_frame_bridge.h"
#include "runtime/crash_guard.h"
#include "runtime/shared_handle_registry.h"
#include "operator_api/type_id.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vivid {

static void* lane_state_fn_bridge(void* ctx_ptr, uint32_t lane_id, uint32_t byte_size) {
    auto* lsc = static_cast<AudioExecutor::NodeLaneCtx*>(ctx_ptr);
    return lsc->service->get(lsc->node_idx, lane_id, byte_size);
}

static uint32_t allocate_lane_id_fn_bridge(void* ctx_ptr) {
    auto* lsc = static_cast<AudioExecutor::NodeLaneCtx*>(ctx_ptr);
    return lsc->service->allocate_lane_id();
}

static void retire_lane_id_fn_bridge(void* ctx_ptr, uint32_t lane_id) {
    auto* lsc = static_cast<AudioExecutor::NodeLaneCtx*>(ctx_ptr);
    lsc->service->retire(lsc->node_idx, lane_id);
}

AudioExecutor::AudioExecutor() = default;

AudioExecutor::~AudioExecutor() {
    shutdown();
}

bool AudioExecutor::build(AudioFrameBridge& bridge, CompiledGraph& cg) {
    bridge_ = &bridge;
    graph_ = &cg;
    sink_node_idx_ = -1;
    lane_lift_groups_.clear();
    node_to_lift_group_.clear();

    if (cg.audio_order.empty()) return false;

    // Detect sink node (audio_out)
    for (uint32_t idx : cg.audio_order) {
        if (cg.nodes[idx].type_name == "audio_out") {
            sink_node_idx_ = static_cast<int>(idx);
            break;
        }
    }

    // Set up lane lift groups for pointwise operators with multi-lane inputs
    for (uint32_t idx : cg.audio_order) {
        auto& cn = cg.nodes[idx];
        if (!cn.audio || cn.audio->execution_strategy != LaneExecutionStrategy::InstancePerLane || !cn.loader) continue;

        uint32_t lanes = cn.audio->lane_lift_count;

        LaneLiftGroup group;
        group.node_idx = idx;
        group.lane_count = lanes;
        group.lane_set_id = cn.audio->lane_lift_set_id;

        // Create additional instances (lane 0 uses primary instance)
        group.instances.resize(lanes);
        group.instances[0] = cn.instance;
        for (uint32_t c = 1; c < lanes; ++c)
            group.instances[c] = cn.loader->create_instance();

        // Derived positional lane IDs for non-identity-bearing lifted sets.
        // These enable per-lane-distinct vivid_lane_state() lookups but are NOT
        // allocator-managed identities — no continuity guarantees across rebuilds.
        group.lane_ids.resize(lanes);
        for (uint32_t c = 0; c < lanes; ++c)
            group.lane_ids[c] = lane_state_.allocate_lane_id();

        // Allocate per-lane mono buffers
        group.per_lane_inputs.resize(lanes);
        group.per_lane_outputs.resize(lanes);
        group.per_lane_in_ptrs.resize(lanes);
        group.per_lane_out_ptrs.resize(lanes);
        for (uint32_t c = 0; c < lanes; ++c) {
            group.per_lane_inputs[c].resize(cn.input_port_count,
                std::vector<float>(kBufferSize, 0.0f));
            group.per_lane_outputs[c].resize(cn.output_port_count,
                std::vector<float>(kBufferSize, 0.0f));
            group.per_lane_in_ptrs[c].resize(cn.input_port_count);
            group.per_lane_out_ptrs[c].resize(cn.output_port_count);
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                group.per_lane_in_ptrs[c][p] = group.per_lane_inputs[c][p].data();
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                group.per_lane_out_ptrs[c][p] = group.per_lane_outputs[c][p].data();
        }

        uint32_t group_idx = static_cast<uint32_t>(lane_lift_groups_.size());
        node_to_lift_group_[idx] = group_idx;
        lane_lift_groups_.push_back(std::move(group));
    }

    // Reset lane state service
    lane_state_.clear();

    // Build per-node lane state contexts
    node_lane_contexts_.resize(cg.audio_order.size());
    for (size_t i = 0; i < cg.audio_order.size(); ++i) {
        node_lane_contexts_[i].service = &lane_state_;
        node_lane_contexts_[i].node_idx = cg.audio_order[i];
    }

    // Allocate waveform ring buffers
    uint32_t audio_count = static_cast<uint32_t>(cg.audio_order.size());
    waveform_rings_.resize(audio_count);
    waveform_ring_pos_.assign(audio_count, 0);
    for (auto& ring : waveform_rings_) ring.fill(0.0f);

    return true;
}

bool AudioExecutor::start(bool use_null_device) {
    if (!bridge_ || !graph_) return false;

    // Configure miniaudio
    device_ = new ma_device;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kSampleRate;
    config.periodSizeInFrames = kBufferSize;
    config.dataCallback = ma_data_callback;
    config.pUserData = this;

    ma_result init_result;
    if (use_null_device) {
        ma_backend backends[] = { ma_backend_null };
        init_result = ma_device_init_ex(backends, 1, nullptr, &config, device_);
    } else {
        init_result = ma_device_init(nullptr, &config, device_);
    }
    if (init_result != MA_SUCCESS) {
        delete device_;
        device_ = nullptr;
        return false;
    }
    if (ma_device_start(device_) != MA_SUCCESS) {
        ma_device_uninit(device_);
        delete device_;
        device_ = nullptr;
        return false;
    }
    running_ = true;
    return true;
}

void AudioExecutor::shutdown() {
    if (device_) {
        ma_device_uninit(device_);
        delete device_;
        device_ = nullptr;
    }
    running_ = false;

    // Destroy lane-lift extra instances
    for (auto& group : lane_lift_groups_) {
        if (group.node_idx < graph_->nodes.size()) {
            auto& cn = graph_->nodes[group.node_idx];
            for (size_t c = 1; c < group.instances.size(); ++c) {
                if (group.instances[c] && cn.loader)
                    cn.loader->destroy_instance(group.instances[c]);
            }
        }
    }
    lane_lift_groups_.clear();
    node_to_lift_group_.clear();
}

void AudioExecutor::pause() {
    if (device_) ma_device_stop(device_);
}

void AudioExecutor::resume() {
    if (device_) ma_device_start(device_);
}

void AudioExecutor::ma_data_callback(ma_device* device, void* output,
                                      const void* /*input*/, unsigned int frame_count) {
    auto* self = static_cast<AudioExecutor*>(device->pUserData);
    self->audio_callback(static_cast<float*>(output), frame_count);
}

void AudioExecutor::process_audio_for_test(float* output, uint32_t frame_count) {
    audio_callback(output, frame_count);
}

// ---------------------------------------------------------------------------
// audio_callback — the real-time audio processing loop
// ---------------------------------------------------------------------------

void AudioExecutor::audio_callback(float* output, uint32_t frame_count) {
    auto cb_start = std::chrono::steady_clock::now();

    if (!bridge_ || !graph_) {
        if (std::getenv("VIVID_DEBUG_AUDIO"))
            std::fprintf(stderr, "[audio-debug] callback: bridge=%p graph=%p — skipping\n",
                         (void*)bridge_, (void*)graph_);
        std::memset(output, 0, frame_count * 2 * sizeof(float));
        return;
    }
    if (std::getenv("VIVID_DEBUG_AUDIO"))
        std::fprintf(stderr, "[audio-debug] callback: audio_order=%zu nodes\n",
                     graph_->audio_order.size());

    const auto& snap = bridge_->active_params();
    auto& cg = *graph_;

    // Apply params from snapshot to audio nodes
    for (uint32_t i = 0; i < static_cast<uint32_t>(cg.audio_order.size()); ++i) {
        uint32_t gi = cg.audio_order[i];
        auto& cn = cg.nodes[gi];
        auto& a = *cn.audio;

        // Copy params
        if (i < snap.node_params.size()) {
            for (size_t p = 0; p < cn.param_values.size() && p < snap.node_params[i].size(); ++p)
                cn.param_values[p] = snap.node_params[i][p];
        }
        // Copy lane inputs from snapshot to CompiledNode
        if (a.has_lane_ports && i < snap.lane_inputs.size()) {
            for (size_t p = 0; p < cn.input_port_count && p < snap.lane_inputs[i].size(); ++p) {
                const auto& ss = snap.lane_inputs[i][p];
                if (p < cn.input_lanes.size()) {
                    cn.input_lanes[p].assign(ss.data, ss.data + ss.length);
                }
            }
        }
        // Copy string inputs and rebuild c_str() pointers
        if (a.has_string_input_ports && i < snap.input_string_values.size()) {
            for (size_t p = 0; p < cn.input_port_count && p < snap.input_string_values[i].size(); ++p) {
                if (p < cn.input_string_values.size())
                    cn.input_string_values[p] = snap.input_string_values[i][p];
            }
            for (size_t p = 0; p < cn.input_string_values.size() && p < cn.c_input_string_values.size(); ++p)
                cn.c_input_string_values[p] = cn.input_string_values[p].c_str();
        }
        // Apply custom port snapshots (e.g. media_stream from MovieLoaded → MovieAudioOut).
        // The snapshot bytes[] array in ParamSnapshot persists until the next push_to_audio,
        // so we can point resolved_custom_inputs directly at it.
        if (a.has_custom_input_ports && i < snap.custom_inputs.size()) {
            for (size_t ci = 0; ci < cn.custom_input_port_indices.size(); ++ci) {
                uint32_t port_idx = cn.custom_input_port_indices[ci];
                if (port_idx < snap.custom_inputs[i].size() &&
                    snap.custom_inputs[i][port_idx].valid) {
                    cn.resolved_custom_inputs[ci] =
                        const_cast<uint8_t*>(snap.custom_inputs[i][port_idx].bytes);
                }
            }
        }
    }

    // Process in chunks of kBufferSize
    uint32_t frames_written = 0;
    while (frames_written < frame_count) {
        uint32_t chunk = std::min(kBufferSize, frame_count - frames_written);

        for (uint32_t ni_ord = 0; ni_ord < static_cast<uint32_t>(cg.audio_order.size()); ++ni_ord) {
            uint32_t ni = cg.audio_order[ni_ord];
            auto& cn = cg.nodes[ni];
            auto& a = *cn.audio;

            // Zero input buffers
            for (auto& buf : a.buffers_in)
                std::memset(buf.data(), 0, buf.size() * sizeof(float));

            // Solo mode check
            if (!snap.solo_active_set.empty() && ni_ord < snap.solo_active_set.size() &&
                !snap.solo_active_set[ni_ord] && ni != static_cast<uint32_t>(sink_node_idx_)) {
                for (auto& buf : a.buffers_out)
                    std::memset(buf.data(), 0, buf.size() * sizeof(float));
                continue;
            }

            // Route audio buffers from upstream (Direct edges)
            for (uint32_t ei : cg.audio_direct_edges) {
                const auto& e = cg.edges[ei];
                if (e.to_node != ni || e.targets_param) continue;
                if (e.data_type != VIVID_PORT_AUDIO_BUFFER && e.data_type != VIVID_PORT_SCALAR) continue;

                auto& from_cn = cg.nodes[e.from_node];
                auto& from_a = *from_cn.audio;
                if (e.from_port >= from_a.buffers_out.size() ||
                    e.to_port >= a.buffers_in.size()) continue;

                float* src = from_a.buffers_out[e.from_port].data();
                float* dst = a.buffers_in[e.to_port].data();
                uint8_t fc = e.from_channels;
                uint8_t tc = e.to_channels;
                float scale = e.remap_scale();

                // Channel data is laid out as [ch0_0..ch0_255][ch1_0..ch1_255]...
                // Each channel block is kBufferSize samples, regardless of chunk size.
                if (fc == tc) {
                    for (uint8_t c = 0; c < fc; ++c) {
                        float* sc = src + c * kBufferSize;
                        float* dc = dst + c * kBufferSize;
                        for (uint32_t s = 0; s < chunk; ++s)
                            dc[s] += sc[s] * scale;
                    }
                } else if (fc == 1 && tc > 1) {
                    for (uint8_t c = 0; c < tc; ++c) {
                        float* dc = dst + c * kBufferSize;
                        for (uint32_t s = 0; s < chunk; ++s)
                            dc[s] += src[s] * scale;
                    }
                } else if (fc > 1 && tc == 1) {
                    float inv_n = 1.0f / fc;
                    for (uint32_t s = 0; s < chunk; ++s) {
                        float sum = 0.0f;
                        for (uint8_t c = 0; c < fc; ++c)
                            sum += src[c * kBufferSize + s];
                        dst[s] += sum * inv_n * scale;
                    }
                } else {
                    uint8_t min_ch = std::min(fc, tc);
                    for (uint8_t c = 0; c < min_ch; ++c) {
                        float* sc = src + c * kBufferSize;
                        float* dc = dst + c * kBufferSize;
                        for (uint32_t s = 0; s < chunk; ++s)
                            dc[s] += sc[s] * scale;
                    }
                }
            }

            // Set up buffer pointers
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                a.in_ptrs[p] = a.buffers_in[p].data();
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                a.out_ptrs[p] = a.buffers_out[p].data();

            double node_time = static_cast<double>(audio_frame_ + frames_written) / kSampleRate;

            // Debug node state
            if (std::getenv("VIVID_DEBUG_AUDIO") && frames_written == 0) {
                std::fprintf(stderr, "[audio-debug] node '%s' loader=%p instance=%p errored=%d\n",
                             cn.node_id.c_str(), (void*)cn.loader, cn.instance, cn.errored);
            }
            // Debug lane data
            if (std::getenv("VIVID_DEBUG_AUDIO")) {
                for (uint32_t p = 0; p < cn.input_port_count; ++p) {
                    if (p < cn.input_lanes.size() && !cn.input_lanes[p].empty()) {
                        std::fprintf(stderr, "[audio-debug] node '%s' input_lane[%u] len=%zu val[0]=%.2f\n",
                                     cn.node_id.c_str(), p, cn.input_lanes[p].size(),
                                     cn.input_lanes[p][0]);
                    }
                }
            }

            // Process
            if (!cn.loader || !cn.instance || cn.errored) continue;

            // Set up lane ports for context
            for (uint32_t p = 0; p < cn.input_port_count && p < cn.c_in_lanes.size(); ++p) {
                if (p < cn.input_lanes.size()) {
                    cn.c_in_lanes[p].data = cn.input_lanes[p].empty() ? nullptr : cn.input_lanes[p].data();
                    cn.c_in_lanes[p].length = static_cast<uint32_t>(cn.input_lanes[p].size());
                    cn.c_in_lanes[p].capacity = static_cast<uint32_t>(cn.input_lanes[p].size());
                } else {
                    cn.c_in_lanes[p].data = nullptr;
                    cn.c_in_lanes[p].length = 0;
                    cn.c_in_lanes[p].capacity = 0;
                }
            }
            for (uint32_t p = 0; p < cn.output_port_count && p < cn.c_out_lanes.size(); ++p) {
                cn.c_out_lanes[p].data = cn.out_lane_buf[p].data();
                cn.c_out_lanes[p].length = 0;
                cn.c_out_lanes[p].capacity = static_cast<uint32_t>(cn.out_lane_buf[p].size());
            }

            // Check for lane-lifted processing
            auto lift_it = node_to_lift_group_.find(ni);
            if (lift_it != node_to_lift_group_.end()) {
                // ── Lane-lifted: deinterleave → per-lane process → interleave ──
                auto& group = lane_lift_groups_[lift_it->second];
                uint32_t lanes = group.lane_count;

                // Deinterleave: extract lane c from multi-lane buffers into per-lane mono buffers
                for (uint32_t c = 0; c < lanes; ++c) {
                    for (uint32_t p = 0; p < cn.input_port_count; ++p) {
                        if (p < cn.input_port_types.size() && cn.input_port_types[p] == VIVID_PORT_AUDIO_BUFFER) {
                            const float* mc = a.buffers_in[p].data() + c * kBufferSize;
                            std::memcpy(group.per_lane_inputs[c][p].data(), mc, chunk * sizeof(float));
                        } else {
                            // Non-audio ports: broadcast same data to all lanes
                            std::memcpy(group.per_lane_inputs[c][p].data(),
                                        a.buffers_in[p].data(), chunk * sizeof(float));
                        }
                    }
                }

                // Process each lane instance
                for (uint32_t c = 0; c < lanes; ++c) {
                    VividAudioContext ctx{};
                    ctx.time = node_time;
                    ctx.delta_time = static_cast<double>(chunk) / kSampleRate;
                    ctx.frame = audio_frame_ + frames_written;
                    ctx.param_values = cn.param_values.data();
                    ctx.input_buffers = group.per_lane_in_ptrs[c].data();
                    ctx.output_buffers = group.per_lane_out_ptrs[c].data();
                    ctx.buffer_size = chunk;
                    ctx.sample_rate = kSampleRate;
                    ctx.input_channel_counts = nullptr;  // mono view
                    ctx.output_channel_counts = nullptr;
                    ctx.input_lanes = cn.c_in_lanes.empty() ? nullptr : cn.c_in_lanes.data();
                    ctx.output_lanes = cn.c_out_lanes.empty() ? nullptr : cn.c_out_lanes.data();
                    ctx.input_string_values = cn.c_input_string_values.empty() ? nullptr : cn.c_input_string_values.data();
                    ctx.custom_inputs = a.has_custom_input_ports ? cn.resolved_custom_inputs.data() : nullptr;
                    ctx.custom_input_count = static_cast<uint32_t>(cn.custom_input_port_indices.size());
                    ctx.custom_outputs = a.custom_output_ptrs.empty() ? nullptr : a.custom_output_ptrs.data();
                    ctx.custom_output_count = a.custom_output_count;
                    ctx.shared_handles = vivid::shared_handle_service();
                    ctx.file_param_values = cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data();
                    ctx.file_param_count = static_cast<uint32_t>(cn.file_param_ptrs.size());
                    ctx.lane_count = lanes;
                    ctx.lane_index = c;
                    ctx.lane_set_id = group.lane_set_id;
                    ctx.lane_id = group.lane_ids[c];
                    ctx.lane_state_fn = lane_state_fn_bridge;
                    ctx.lane_state_service = &node_lane_contexts_[ni_ord];
                    ctx.allocate_lane_id_fn = allocate_lane_id_fn_bridge;
                    ctx.retire_lane_id_fn = retire_lane_id_fn_bridge;
                    // Note: allocate/retire use lane_state_ directly (not per-node context)

                    try {
                        cn.loader->process_audio(group.instances[c], &ctx);
                    } catch (const std::exception& ex) {
                        cn.errored = true;
                        std::strncpy(a.error_message, ex.what(), sizeof(a.error_message) - 1);
                        a.error_message[sizeof(a.error_message) - 1] = '\0';
                    } catch (...) {
                        cn.errored = true;
                        std::strncpy(a.error_message, "unknown exception", sizeof(a.error_message) - 1);
                    }
                }

                if (cn.errored) {
                    for (auto& buf : a.buffers_out)
                        std::memset(buf.data(), 0, buf.size() * sizeof(float));
                } else {
                    // Interleave: copy per-lane mono output back into multi-lane output buffers.
                    // Include SIGNAL ports — dual-cadence operators write audio buffers
                    // on SIGNAL outputs when promoted to audio cadence.
                    for (uint32_t c = 0; c < lanes; ++c) {
                        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
                            if (p < cn.output_port_types.size() &&
                                (cn.output_port_types[p] == VIVID_PORT_AUDIO_BUFFER ||
                                 cn.output_port_types[p] == VIVID_PORT_SCALAR)) {
                                float* mc = a.buffers_out[p].data() + c * kBufferSize;
                                std::memcpy(mc, group.per_lane_outputs[c][p].data(), chunk * sizeof(float));
                            }
                        }
                    }
                }
            } else if (cn.audio && cn.audio->execution_strategy == LaneExecutionStrategy::LoopBased) {
                // ── LoopBased: single instance, runtime-driven loop over lanes ──
                // Discover lane count from lane inputs at runtime.
                uint32_t loop_lanes = 0;
                for (uint32_t p = 0; p < cn.input_port_count && p < cn.c_in_lanes.size(); ++p) {
                    if (cn.c_in_lanes[p].length > loop_lanes)
                        loop_lanes = cn.c_in_lanes[p].length;
                }
                uint32_t max_ll = graph_->max_loop_lanes;
                if (loop_lanes > max_ll) {
                    std::fprintf(stderr, "[vivid] LoopBased node '%s': lane count %u exceeds max %u, clamping\n",
                                 cn.node_id.c_str(), loop_lanes, max_ll);
                    loop_lanes = max_ll;
                }

                if (loop_lanes > 0) {
                    // Read identity-bearing lane_ids from upstream lane array, or fall back to positional.
                    std::vector<uint32_t> loop_lane_ids(loop_lanes);
                    int32_t lid_port = a.lane_id_port;
                    bool has_identity_ids = false;
                    if (lid_port >= 0 && static_cast<uint32_t>(lid_port) < cn.c_in_lanes.size()) {
                        const auto& lid_sp = cn.c_in_lanes[lid_port];
                        if (lid_sp.length >= loop_lanes) {
                            for (uint32_t c = 0; c < loop_lanes; ++c)
                                loop_lane_ids[c] = static_cast<uint32_t>(lid_sp.data[c]);
                            has_identity_ids = true;
                        }
                    }
                    if (!has_identity_ids) {
                        for (uint32_t c = 0; c < loop_lanes; ++c)
                            loop_lane_ids[c] = c + 1;  // derived positional IDs
                    }

                    // Per-lane mono buffer pointers (reused across iterations)
                    std::vector<float*> loop_in_ptrs(cn.input_port_count);
                    std::vector<float*> loop_out_ptrs(cn.output_port_count);

                    for (uint32_t c = 0; c < loop_lanes; ++c) {
                        // Set up per-lane buffer pointers (slice into pre-allocated buffers)
                        for (uint32_t p = 0; p < cn.input_port_count; ++p) {
                            if (p < cn.input_port_types.size() && cn.input_port_types[p] == VIVID_PORT_AUDIO_BUFFER) {
                                loop_in_ptrs[p] = a.buffers_in[p].data() + c * kBufferSize;
                            } else {
                                loop_in_ptrs[p] = a.buffers_in[p].data();  // broadcast non-audio
                            }
                        }
                        for (uint32_t p = 0; p < cn.output_port_count; ++p)
                            loop_out_ptrs[p] = a.buffers_out[p].data() + c * kBufferSize;

                        VividAudioContext ctx{};
                        ctx.time = node_time;
                        ctx.delta_time = static_cast<double>(chunk) / kSampleRate;
                        ctx.frame = audio_frame_ + frames_written;
                        ctx.param_values = cn.param_values.data();
                        ctx.input_buffers = loop_in_ptrs.data();
                        ctx.output_buffers = loop_out_ptrs.data();
                        ctx.buffer_size = chunk;
                        ctx.sample_rate = kSampleRate;
                        ctx.input_channel_counts = nullptr;  // mono view
                        ctx.output_channel_counts = nullptr;
                        ctx.input_lanes = cn.c_in_lanes.empty() ? nullptr : cn.c_in_lanes.data();
                        ctx.output_lanes = cn.c_out_lanes.empty() ? nullptr : cn.c_out_lanes.data();
                        ctx.input_string_values = cn.c_input_string_values.empty() ? nullptr : cn.c_input_string_values.data();
                        ctx.custom_inputs = a.has_custom_input_ports ? cn.resolved_custom_inputs.data() : nullptr;
                        ctx.custom_input_count = static_cast<uint32_t>(cn.custom_input_port_indices.size());
                        ctx.custom_outputs = a.custom_output_ptrs.empty() ? nullptr : a.custom_output_ptrs.data();
                        ctx.custom_output_count = a.custom_output_count;
                        ctx.shared_handles = vivid::shared_handle_service();
                        ctx.file_param_values = cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data();
                        ctx.file_param_count = static_cast<uint32_t>(cn.file_param_ptrs.size());
                        ctx.lane_count = loop_lanes;
                        ctx.lane_index = c;
                        ctx.lane_set_id = cn.audio->lane_lift_set_id;
                        ctx.lane_id = loop_lane_ids[c];
                        ctx.lane_state_fn = lane_state_fn_bridge;
                        ctx.lane_state_service = &node_lane_contexts_[ni_ord];
                        ctx.allocate_lane_id_fn = allocate_lane_id_fn_bridge;
                        ctx.retire_lane_id_fn = retire_lane_id_fn_bridge;

                        try {
                            cn.loader->process_audio(cn.instance, &ctx);
                        } catch (const std::exception& ex) {
                            cn.errored = true;
                            std::strncpy(a.error_message, ex.what(), sizeof(a.error_message) - 1);
                            a.error_message[sizeof(a.error_message) - 1] = '\0';
                        } catch (...) {
                            cn.errored = true;
                            std::strncpy(a.error_message, "unknown exception", sizeof(a.error_message) - 1);
                        }
                    }

                    if (cn.errored) {
                        for (auto& buf : a.buffers_out)
                            std::memset(buf.data(), 0, buf.size() * sizeof(float));
                    }

                    // Collect per-lane SIGNAL output values into output_lanes.
                    // Dual-cadence operators write audio buffers on SIGNAL ports;
                    // extract the last sample per lane so frame-side inspection
                    // and analysis snapshots see correct per-lane values.
                    for (uint32_t p = 0; p < cn.output_port_count; ++p) {
                        if (p < cn.output_port_types.size() &&
                            cn.output_port_types[p] == VIVID_PORT_SCALAR &&
                            p < cn.c_out_lanes.size() &&
                            cn.c_out_lanes[p].capacity >= loop_lanes) {
                            cn.c_out_lanes[p].length = loop_lanes;
                            for (uint32_t c = 0; c < loop_lanes; ++c) {
                                float* lane_buf = a.buffers_out[p].data() + c * kBufferSize;
                                cn.c_out_lanes[p].data[c] = (chunk > 0) ? lane_buf[chunk - 1] : 0.0f;
                            }
                        }
                    }
                }
            } else {
                // ── Normal (non-lifted) processing ──
                VividAudioContext ctx{};
                ctx.time = node_time;
                ctx.delta_time = static_cast<double>(chunk) / kSampleRate;
                ctx.frame = audio_frame_ + frames_written;
                ctx.param_values = cn.param_values.data();
                ctx.input_buffers = a.in_ptrs.data();
                ctx.output_buffers = a.out_ptrs.data();
                ctx.buffer_size = chunk;
                ctx.sample_rate = kSampleRate;
                ctx.input_channel_counts = a.input_channel_counts.data();
                ctx.output_channel_counts = a.output_channel_counts.data();
                ctx.input_lanes = cn.c_in_lanes.empty() ? nullptr : cn.c_in_lanes.data();
                ctx.output_lanes = cn.c_out_lanes.empty() ? nullptr : cn.c_out_lanes.data();
                ctx.input_string_values = cn.c_input_string_values.empty() ? nullptr : cn.c_input_string_values.data();
                ctx.custom_inputs = a.has_custom_input_ports ? cn.resolved_custom_inputs.data() : nullptr;
                ctx.custom_input_count = static_cast<uint32_t>(cn.custom_input_port_indices.size());
                ctx.custom_outputs = a.custom_output_ptrs.empty() ? nullptr : a.custom_output_ptrs.data();
                ctx.custom_output_count = a.custom_output_count;
                ctx.shared_handles = vivid::shared_handle_service();
                ctx.file_param_values = cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data();
                ctx.file_param_count = static_cast<uint32_t>(cn.file_param_ptrs.size());
                ctx.lane_count = 1;
                ctx.lane_index = 0;
                ctx.lane_set_id = 0;
                ctx.lane_id = 0;
                ctx.lane_state_fn = lane_state_fn_bridge;
                ctx.lane_state_service = &node_lane_contexts_[ni_ord];
                ctx.allocate_lane_id_fn = allocate_lane_id_fn_bridge;
                ctx.retire_lane_id_fn = retire_lane_id_fn_bridge;

                try {
                    cn.loader->process_audio(cn.instance, &ctx);
                } catch (const std::exception& ex) {
                    cn.errored = true;
                    std::strncpy(a.error_message, ex.what(), sizeof(a.error_message) - 1);
                    a.error_message[sizeof(a.error_message) - 1] = '\0';
                    for (auto& buf : a.buffers_out)
                        std::memset(buf.data(), 0, buf.size() * sizeof(float));
                } catch (...) {
                    cn.errored = true;
                    std::strncpy(a.error_message, "unknown exception", sizeof(a.error_message) - 1);
                    for (auto& buf : a.buffers_out)
                        std::memset(buf.data(), 0, buf.size() * sizeof(float));
                }
            }

            // Read back lane outputs
            for (uint32_t p = 0; p < cn.output_port_count && p < cn.c_out_lanes.size(); ++p) {
                if (cn.c_out_lanes[p].length > 0 && p < cn.output_lanes.size()) {
                    cn.output_lanes[p].assign(
                        cn.out_lane_buf[p].begin(),
                        cn.out_lane_buf[p].begin() + cn.c_out_lanes[p].length);
                }
            }

            // Route float/lane/custom outputs to downstream audio nodes
            for (uint32_t ei : cg.audio_direct_edges) {
                const auto& e = cg.edges[ei];
                if (e.from_node != ni || e.targets_param) continue;

                if (e.data_type == VIVID_PORT_LANE_ARRAY) {
                    // Lane routing between audio nodes
                    auto& to_cn = cg.nodes[e.to_node];
                    if (e.from_port < cn.output_lanes.size() &&
                        e.to_port < to_cn.input_lanes.size()) {
                        const auto& src = cn.output_lanes[e.from_port];
                        to_cn.input_lanes[e.to_port].assign(src.begin(), src.end());
                    }
                } else if (vivid_is_custom_port_type(e.data_type)) {
                    // Custom port routing between audio nodes
                    auto& to_cn = cg.nodes[e.to_node];
                    // Find custom output ordinal on source
                    uint32_t from_ord = 0;
                    for (uint32_t ci = 0; ci < cn.custom_output_port_indices.size(); ++ci) {
                        if (cn.custom_output_port_indices[ci] == e.from_port) { from_ord = ci; break; }
                    }
                    // Find custom input ordinal on destination
                    uint32_t to_ord = 0;
                    for (uint32_t ci = 0; ci < to_cn.custom_input_port_indices.size(); ++ci) {
                        if (to_cn.custom_input_port_indices[ci] == e.to_port) { to_ord = ci; break; }
                    }
                    if (from_ord < a.custom_output_ptrs.size() &&
                        to_ord < to_cn.resolved_custom_inputs.size())
                        to_cn.resolved_custom_inputs[to_ord] = a.custom_output_ptrs[from_ord];
                }
            }
        }

        // Sink extraction — interleave to device output
        float* dst = output + frames_written * 2;
        if (sink_node_idx_ >= 0 && static_cast<uint32_t>(sink_node_idx_) < cg.nodes.size()) {
            auto& sink = cg.nodes[sink_node_idx_];
            if (std::getenv("VIVID_DEBUG_AUDIO") && frames_written == 0) {
                float max_in = 0;
                for (const auto& buf : sink.audio->buffers_in) {
                    for (uint32_t s = 0; s < chunk && s < buf.size(); ++s) {
                        float av = std::fabs(buf[s]);
                        if (av > max_in) max_in = av;
                    }
                }
                std::fprintf(stderr, "[audio-debug] sink '%s' max_input=%.4f, buf_count=%zu\n",
                             sink.node_id.c_str(), max_in, sink.audio->buffers_in.size());
            }
            // audio_out reads from input buffers
            if (!sink.audio->buffers_in.empty() && sink.audio->buffers_in[0].size() >= chunk) {
                float* L = sink.audio->buffers_in[0].data();
                uint8_t ch = sink.audio->input_channel_counts.empty() ? 1 : sink.audio->input_channel_counts[0];
                if (ch >= 2) {
                    float* R = L + kBufferSize;
                    for (uint32_t s = 0; s < chunk; ++s) {
                        dst[s * 2]     = L[s];
                        dst[s * 2 + 1] = R[s];
                    }
                } else {
                    for (uint32_t s = 0; s < chunk; ++s) {
                        dst[s * 2]     = L[s];
                        dst[s * 2 + 1] = L[s];
                    }
                }
            } else {
                std::memset(dst, 0, chunk * 2 * sizeof(float));
            }
        } else {
            std::memset(dst, 0, chunk * 2 * sizeof(float));
        }

        // Recording tap
        if (recording_tap_.active.load(std::memory_order_relaxed)) {
            uint64_t wp = recording_tap_.write_pos.load(std::memory_order_relaxed);
            uint64_t rp = recording_tap_.read_pos.load(std::memory_order_acquire);
            uint64_t available = RecordingTap::kRingSize - (wp - rp);
            uint32_t to_write = chunk * 2;
            if (to_write <= available) {
                for (uint32_t i = 0; i < to_write; ++i)
                    recording_tap_.ring[(wp + i) % RecordingTap::kRingSize] = dst[i];
                recording_tap_.write_pos.store(wp + to_write, std::memory_order_release);
            } else {
                if (recording_overrun_count_++ == 0)
                    std::fprintf(stderr, "[vivid] Recording tap overrun — samples dropped\n");
            }
        }

        frames_written += chunk;
    }

    // Underrun detection
    auto cb_end = std::chrono::steady_clock::now();
    double elapsed_us = std::chrono::duration<double, std::micro>(cb_end - cb_start).count();
    double budget_us = static_cast<double>(frame_count) / kSampleRate * 1e6;
    audio_load_.store(static_cast<float>(elapsed_us / budget_us), std::memory_order_relaxed);
    if (elapsed_us > budget_us) {
        underrun_count_.fetch_add(1, std::memory_order_relaxed);
        last_buffer_underrun_.store(true, std::memory_order_relaxed);
        std::memset(output, 0, frame_count * 2 * sizeof(float));
    } else {
        last_buffer_underrun_.store(false, std::memory_order_relaxed);
    }

    // Analysis snapshot write
    auto& analysis = bridge_->analysis_write_buffer();
    for (uint32_t i = 0; i < static_cast<uint32_t>(cg.audio_order.size()); ++i) {
        uint32_t gi = cg.audio_order[i];
        auto& cn = cg.nodes[gi];
        auto& a = *cn.audio;

        // Get buffer to analyze (sink uses input, others use output)
        float* buf = nullptr;
        uint32_t buf_len = 0;
        if (gi == static_cast<uint32_t>(sink_node_idx_) && !a.buffers_in.empty()) {
            buf = a.buffers_in[0].data();
            buf_len = std::min(frame_count, static_cast<uint32_t>(a.buffers_in[0].size()));
        } else if (!a.buffers_out.empty()) {
            buf = a.buffers_out[0].data();
            buf_len = std::min(frame_count, static_cast<uint32_t>(a.buffers_out[0].size()));
        }

        // RMS & peak (gated by analysis toggle)
        if (analysis_enabled_.load(std::memory_order_relaxed) &&
            buf && buf_len > 0 && i < analysis.rms.size()) {
            float sum_sq = 0.0f, peak = 0.0f;
            for (uint32_t s = 0; s < buf_len; ++s) {
                float v = buf[s];
                sum_sq += v * v;
                float av = std::fabs(v);
                if (av > peak) peak = av;
            }
            analysis.rms[i] = std::sqrt(sum_sq / buf_len);
            analysis.peak[i] = peak;
        }

        // Waveform ring (gated by analysis toggle)
        if (analysis_enabled_.load(std::memory_order_relaxed) &&
            buf && buf_len > 0 && i < waveform_rings_.size()) {
            auto& ring = waveform_rings_[i];
            auto& pos = waveform_ring_pos_[i];
            for (uint32_t s = 0; s < buf_len; ++s) {
                ring[pos] = buf[s];
                pos = (pos + 1) % 1024;
            }
            if (i < analysis.waveform.size()) {
                for (uint32_t j = 0; j < 1024; ++j)
                    analysis.waveform[i][j] = ring[(pos + j) % 1024];
            }
        }

        // Spread outputs
        if (i < analysis.lane_outputs.size()) {
            for (size_t p = 0; p < cn.output_port_count && p < analysis.lane_outputs[i].size(); ++p) {
                if (p < cn.output_lanes.size()) {
                    auto& dst = analysis.lane_outputs[i][p];
                    const auto& src = cn.output_lanes[p];
                    dst.length = std::min(static_cast<uint32_t>(src.size()),
                                          LaneSnapshot::kMaxLength);
                    for (uint32_t j = 0; j < dst.length; ++j)
                        dst.data[j] = src[j];
                }
            }
        }

        // Scalar outputs: extract last sample from each output buffer for bridge delivery
        if (i < analysis.scalar_outputs.size()) {
            for (uint32_t p = 0; p < cn.output_port_count && p < analysis.scalar_outputs[i].size(); ++p) {
                if (p < a.buffers_out.size() && !a.buffers_out[p].empty()) {
                    analysis.scalar_outputs[i][p] = a.buffers_out[p].back();
                }
            }
        }

        // Error state
        if (i < analysis.errored.size()) {
            analysis.errored[i] = cn.errored;
            if (cn.errored && i < analysis.error_msgs.size()) {
                std::strncpy(analysis.error_msgs[i].data(), a.error_message,
                             analysis.error_msgs[i].size() - 1);
            }
        }
    }
    bridge_->publish_analysis();

    audio_frame_ += frame_count;
}

// Recording tap methods
void AudioExecutor::start_recording_tap() {
    recording_tap_.write_pos.store(0, std::memory_order_relaxed);
    recording_tap_.read_pos.store(0, std::memory_order_relaxed);
    recording_tap_.active.store(true, std::memory_order_release);
}

void AudioExecutor::stop_recording_tap() {
    recording_tap_.active.store(false, std::memory_order_release);
}

uint64_t AudioExecutor::available_recorded_samples() const {
    uint64_t wp = recording_tap_.write_pos.load(std::memory_order_acquire);
    uint64_t rp = recording_tap_.read_pos.load(std::memory_order_relaxed);
    return wp - rp;
}

uint64_t AudioExecutor::pop_recorded_samples(float* dst, uint64_t max_samples) {
    uint64_t wp = recording_tap_.write_pos.load(std::memory_order_acquire);
    uint64_t rp = recording_tap_.read_pos.load(std::memory_order_relaxed);
    uint64_t avail = wp - rp;
    uint64_t to_read = std::min(avail, max_samples);
    for (uint64_t i = 0; i < to_read; ++i)
        dst[i] = recording_tap_.ring[(rp + i) % RecordingTap::kRingSize];
    recording_tap_.read_pos.store(rp + to_read, std::memory_order_release);
    return to_read;
}

} // namespace vivid
