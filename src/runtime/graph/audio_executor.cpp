#include <miniaudio.h>

#include "runtime/graph/audio_executor.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/core/crash_guard.h"
#include "runtime/core/shared_handle_registry.h"
#include "operator_api/type_id.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace vivid {

namespace {

using AudioClock = std::chrono::steady_clock;

float compute_port_peak(const float* buf, uint32_t channel_count,
                        uint32_t planar_stride, uint32_t frame_count) {
    if (!buf || channel_count == 0 || frame_count == 0) return 0.0f;
    float peak = 0.0f;
    for (uint32_t ch = 0; ch < channel_count; ++ch) {
        const float* chan = buf + ch * planar_stride;
        for (uint32_t i = 0; i < frame_count; ++i) {
            float mag = std::fabs(chan[i]);
            if (mag > peak) peak = mag;
        }
    }
    return peak;
}

uint32_t elapsed_us(AudioClock::time_point start, AudioClock::time_point end) {
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    if (micros <= 0) return 0;
    if (micros > static_cast<int64_t>(UINT32_MAX)) return UINT32_MAX;
    return static_cast<uint32_t>(micros);
}

void write_audio_port_telemetry(const CompiledNode& cn,
                                AudioNodeState& a,
                                bool input,
                                uint32_t frame_count,
                                uint32_t planar_stride) {
    const auto& port_types = input ? cn.input_port_types : cn.output_port_types;
    const auto& debug_counts = input ? a.debug_input_channel_counts : a.debug_output_channel_counts;
    auto* telemetry = input ? a.input_port_debug.get() : a.output_port_debug.get();
    const auto& buffers = input ? a.buffers_in : a.buffers_out;
    if (!telemetry) return;

    for (uint32_t p = 0; p < port_types.size() && p < debug_counts.size() && p < buffers.size(); ++p) {
        if (port_types[p] != VIVID_PORT_AUDIO_BUFFER) continue;
        telemetry[p].buffer_size.store(frame_count, std::memory_order_relaxed);
        telemetry[p].last_block_peak.store(
            compute_port_peak(buffers[p].data(), debug_counts[p], planar_stride, frame_count),
            std::memory_order_relaxed);
    }
}

void clear_audio_output_telemetry(const CompiledNode& cn,
                                  AudioNodeState& a,
                                  uint32_t frame_count) {
    if (!a.output_port_debug) return;
    for (uint32_t p = 0; p < cn.output_port_types.size() && p < a.debug_output_channel_counts.size(); ++p) {
        if (cn.output_port_types[p] != VIVID_PORT_AUDIO_BUFFER) continue;
        a.output_port_debug[p].buffer_size.store(frame_count, std::memory_order_relaxed);
        a.output_port_debug[p].last_block_peak.store(0.0f, std::memory_order_relaxed);
    }
}

void write_audio_node_telemetry(AudioNodeState& a,
                                uint32_t total_us,
                                uint32_t process_us,
                                float budget_pct,
                                uint32_t lane_count,
                                uint32_t lane_state_entries) {
    uint32_t prev_ema = a.node_debug.ema_block_us.load(std::memory_order_relaxed);
    uint32_t next_ema = (prev_ema == 0) ? total_us : ((prev_ema * 7u) + total_us) / 8u;
    a.node_debug.last_block_total_us.store(total_us, std::memory_order_relaxed);
    a.node_debug.last_process_us.store(process_us, std::memory_order_relaxed);
    a.node_debug.ema_block_us.store(next_ema, std::memory_order_relaxed);
    a.node_debug.last_block_budget_pct.store(budget_pct, std::memory_order_relaxed);
    a.node_debug.last_lane_count.store(lane_count, std::memory_order_relaxed);
    a.node_debug.lane_state_entries.store(lane_state_entries, std::memory_order_relaxed);
}

} // namespace

AudioExecutor::AudioExecutor() = default;

AudioExecutor::~AudioExecutor() {
    shutdown();
}

// build() prepares audio-side execution state from a freshly compiled graph.
// The key structure is lane lift groups: when a pointwise audio operator has
// multi-lane inputs (e.g. polyphonic voices), the audio executor creates
// per-lane cloned instances so each lane processes independently through its
// own operator state. The audio engine MUST be stopped before build() runs —
// instance pointers are read by the callback thread without synchronization.
bool AudioExecutor::build(AudioFrameBridge& bridge, CompiledGraph& cg,
                          const LiveMetronomeStateStore& metronome_store,
                          double wall_time) {
    bridge_ = &bridge;
    graph_ = &cg;
    metronome_store_ = &metronome_store;
    audio_start_wall_time_ = wall_time;
    sink_node_idx_ = -1;
    lane_lift_groups_.clear();
    node_to_lift_group_.clear();
    buffer_size_ = cg.audio_buffer_size;
    sample_rate_ = cg.audio_sample_rate;

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

        // Create additional instances (lane 0 uses primary instance).
        // INVARIANT: The audio engine must be stopped before this code runs.
        // These instance pointers are read by the audio callback thread without
        // synchronization, so the engine must not be executing callbacks during
        // graph rebuild.
        group.instances.resize(lanes);
        group.instances[0] = cn.audio_instance ? cn.audio_instance : cn.instance;
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
                std::vector<float>(buffer_size_, 0.0f));
            group.per_lane_outputs[c].resize(cn.output_port_count,
                std::vector<float>(buffer_size_, 0.0f));
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
    lane_state_.set_node_capacity(static_cast<uint32_t>(cg.nodes.size()));

    // Build per-node lane state contexts
    node_lane_contexts_.resize(cg.audio_order.size());
    for (size_t i = 0; i < cg.audio_order.size(); ++i) {
        node_lane_contexts_[i].service = &lane_state_;
        node_lane_contexts_[i].node_idx = cg.audio_order[i];
    }

    // Pre-allocate LoopBased scratch vectors (avoids audio-thread allocation).
    {
        uint32_t max_in = 0, max_out = 0;
        for (uint32_t idx : cg.audio_order) {
            auto& cn2 = cg.nodes[idx];
            if (cn2.audio && cn2.audio->execution_strategy == LaneExecutionStrategy::LoopBased) {
                max_in = std::max(max_in, cn2.input_port_count);
                max_out = std::max(max_out, cn2.output_port_count);
            }
        }
        loop_lane_ids_scratch_.resize(cg.max_loop_lanes);
        loop_in_ptrs_scratch_.resize(max_in);
        loop_out_ptrs_scratch_.resize(max_out);
    }

    // Allocate per-channel waveform ring buffers
    uint32_t audio_count = static_cast<uint32_t>(cg.audio_order.size());
    waveform_rings_.resize(audio_count);
    waveform_ring_pos_.resize(audio_count);
    for (auto& node_rings : waveform_rings_)
        for (auto& ch_ring : node_rings) ch_ring.fill(0.0f);
    for (auto& pos : waveform_ring_pos_) pos.fill(0);

    return true;
}

bool AudioExecutor::start(bool use_null_device) {
    if (!bridge_ || !graph_) return false;

    // Configure miniaudio
    device_ = new ma_device;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = sample_rate_;
    config.periodSizeInFrames = buffer_size_;
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
    metronome_store_ = nullptr;
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
//
// Called by the audio device at ~48kHz in blocks of frame_count samples.
// Real-time constraints: no allocation, no locking, no blocking, no I/O.
//
// Each call: consume the latest ParamSnapshot from the frame side (atomic
// index swap), walk audio_order processing each node, interleave the sink
// node's output into the device buffer, then publish AnalysisSnapshot back
// to the frame side. Lane-lifted nodes are processed per-lane-instance.
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

    // Reclaim per-lane state retired during the previous callback before we
    // touch any node state for this block. This keeps long-running polyphonic
    // sessions from accumulating stale lane-state entries indefinitely.
    lane_state_.sweep_retired();

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

        // Copy params into the audio-local buffer (not cn.param_values, which
        // is owned by the main thread — writing there would race with set_param).
        if (i < snap.node_params.size()) {
            for (size_t p = 0; p < a.audio_local_params.size() && p < snap.node_params[i].size(); ++p)
                a.audio_local_params[p] = snap.node_params[i][p];
        }
        // Populate lane views directly from bridge snapshot (zero-copy).
        // Bridge data is double-buffered and valid for the entire callback.
        if (a.has_lane_ports && i < snap.lane_inputs.size()) {
            for (size_t p = 0; p < cn.input_port_count && p < snap.lane_inputs[i].size(); ++p) {
                if (p >= cn.c_in_lane_views.size()) continue;
                const auto& ss = snap.lane_inputs[i][p];
                cn.c_in_lane_views[p].data = (ss.length > 0) ? ss.data : nullptr;
                cn.c_in_lane_views[p].length = ss.length;
                cn.c_in_lane_views[p].lane_set_id = ss.lane_set_id;
                cn.c_in_lane_views[p].flags = 0;
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

    // Process in chunks of the configured buffer size
    uint32_t frames_written = 0;
    while (frames_written < frame_count) {
        uint32_t chunk = std::min(buffer_size_, frame_count - frames_written);

        for (uint32_t ni_ord = 0; ni_ord < static_cast<uint32_t>(cg.audio_order.size()); ++ni_ord) {
            uint32_t ni = cg.audio_order[ni_ord];
            auto& cn = cg.nodes[ni];
            auto& a = *cn.audio;
            auto node_start = AudioClock::now();
            uint32_t node_process_us = 0;
            uint32_t node_lane_count = 0;
            double node_budget_us = sample_rate_ > 0
                ? (static_cast<double>(chunk) / sample_rate_) * 1e6
                : 0.0;
            auto finalize_node_debug = [&]() {
                uint32_t total_us = elapsed_us(node_start, AudioClock::now());
                float budget_pct = 0.0f;
                if (node_budget_us > 0.0) {
                    budget_pct = static_cast<float>((static_cast<double>(total_us) / node_budget_us) * 100.0);
                }
                write_audio_node_telemetry(a,
                                           total_us,
                                           node_process_us,
                                           budget_pct,
                                           node_lane_count,
                                           lane_state_.live_entry_count(ni));
            };

            // Reset input buffers to port default values (0 for most, 1.0 for multiplicative CVs)
            for (uint32_t bi = 0; bi < a.buffers_in.size(); ++bi) {
                float def = (bi < a.input_port_defaults.size()) ? a.input_port_defaults[bi] : 0.0f;
                if (def == 0.0f) {
                    std::memset(a.buffers_in[bi].data(), 0, a.buffers_in[bi].size() * sizeof(float));
                } else {
                    std::fill(a.buffers_in[bi].begin(), a.buffers_in[bi].end(), def);
                }
            }

            // Solo mode check
            if (!snap.solo_active_set.empty() && ni_ord < snap.solo_active_set.size() &&
                !snap.solo_active_set[ni_ord] && ni != static_cast<uint32_t>(sink_node_idx_)) {
                for (auto& buf : a.buffers_out)
                    std::memset(buf.data(), 0, buf.size() * sizeof(float));
                clear_audio_output_telemetry(cn, a, chunk);
                finalize_node_debug();
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

                // Channel data is laid out as [ch0...][ch1...], using the configured
                // audio buffer size as the planar stride regardless of chunk size.
                if (fc == tc) {
                    for (uint8_t c = 0; c < fc; ++c) {
                        float* sc = src + c * buffer_size_;
                        float* dc = dst + c * buffer_size_;
                        for (uint32_t s = 0; s < chunk; ++s)
                            dc[s] += sc[s] * scale;
                    }
                } else if (fc == 1 && tc > 1) {
                    for (uint8_t c = 0; c < tc; ++c) {
                        float* dc = dst + c * buffer_size_;
                        for (uint32_t s = 0; s < chunk; ++s)
                            dc[s] += src[s] * scale;
                    }
                } else if (fc > 1 && tc == 1) {
                    float inv_n = 1.0f / fc;
                    for (uint32_t s = 0; s < chunk; ++s) {
                        float sum = 0.0f;
                        for (uint8_t c = 0; c < fc; ++c)
                            sum += src[c * buffer_size_ + s];
                        dst[s] += sum * inv_n * scale;
                    }
                } else {
                    uint8_t min_ch = std::min(fc, tc);
                    for (uint8_t c = 0; c < min_ch; ++c) {
                        float* sc = src + c * buffer_size_;
                        float* dc = dst + c * buffer_size_;
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
            write_audio_port_telemetry(cn, a, true, chunk, buffer_size_);

            double node_time = static_cast<double>(audio_frame_ + frames_written) / sample_rate_;
            double wall_time = audio_start_wall_time_ + node_time;
            const auto metronome = metronome_store_
                ? sample_live_metronome(*metronome_store_, wall_time)
                : GraphMetronomeSample{};

            // Debug node state
            if (std::getenv("VIVID_DEBUG_AUDIO") && frames_written == 0) {
                std::fprintf(stderr, "[audio-debug] node '%s' loader=%p instance=%p errored=%d\n",
                             cn.node_id.c_str(), (void*)cn.loader,
                             cn.audio_instance ? cn.audio_instance : cn.instance, cn.errored);
            }
            // Debug lane data
            if (std::getenv("VIVID_DEBUG_AUDIO")) {
                for (uint32_t p = 0; p < cn.input_port_count && p < cn.c_in_lane_views.size(); ++p) {
                    if (cn.c_in_lane_views[p].length > 0) {
                        std::fprintf(stderr, "[audio-debug] node '%s' input_lane[%u] len=%u val[0]=%.2f\n",
                                     cn.node_id.c_str(), p, cn.c_in_lane_views[p].length,
                                     cn.c_in_lane_views[p].data[0]);
                    }
                }
            }

            // Process
            void* audio_instance = cn.audio_instance ? cn.audio_instance : cn.instance;
            if (!cn.loader || !audio_instance || cn.errored) {
                clear_audio_output_telemetry(cn, a, chunk);
                finalize_node_debug();
                continue;
            }

            // ── Bypass branch ─────────────────────────────────────────────
            // RT-safe passthrough: when the operator is bypassed and eligible
            // (first input/output port types match), skip process_audio() and
            // memcpy the first input port's buffer into the first output port.
            // Other output buffers are zeroed for deterministic state. Only
            // memcpy/memset, no allocation, no locking.
            if (cn.bypassed && cn.bypassable && !cn.input_port_types.empty() &&
                !cn.output_port_types.empty() &&
                cn.input_port_types[0] == VIVID_PORT_AUDIO_BUFFER &&
                cn.output_port_types[0] == VIVID_PORT_AUDIO_BUFFER &&
                !a.buffers_in.empty() && !a.buffers_out.empty()) {
                const uint8_t in_ch = a.input_channel_counts.empty()
                    ? 1 : a.input_channel_counts[0];
                const uint8_t out_ch = a.output_channel_counts.empty()
                    ? 1 : a.output_channel_counts[0];
                const uint8_t copy_ch = std::min(in_ch, out_ch);
                for (uint8_t c = 0; c < copy_ch; ++c) {
                    std::memcpy(a.buffers_out[0].data() + c * buffer_size_,
                                a.buffers_in[0].data()  + c * buffer_size_,
                                chunk * sizeof(float));
                }
                for (uint8_t c = copy_ch; c < out_ch; ++c) {
                    std::memset(a.buffers_out[0].data() + c * buffer_size_, 0,
                                chunk * sizeof(float));
                }
                for (size_t p = 1; p < a.buffers_out.size(); ++p) {
                    std::memset(a.buffers_out[p].data(), 0,
                                a.buffers_out[p].size() * sizeof(float));
                }
                write_audio_port_telemetry(cn, a, false, chunk, buffer_size_);
                finalize_node_debug();
                continue;
            }
            // For audio nodes whose first input/output port types match but are
            // not AUDIO_BUFFER (rare — e.g. pure scalar audio-cadence ops), there
            // is no audio buffer to pass through; treat as silent skip.
            if (cn.bypassed && cn.bypassable) {
                for (auto& buf : a.buffers_out)
                    std::memset(buf.data(), 0, buf.size() * sizeof(float));
                clear_audio_output_telemetry(cn, a, chunk);
                finalize_node_debug();
                continue;
            }

            // Set up lane views and output builders for context.
            // Priority: refs (audio-direct routing) > bridge views (already
            // populated from snapshot above) > empty.
            for (uint32_t p = 0; p < cn.input_port_count && p < cn.c_in_lane_views.size(); ++p) {
                if (p < cn.input_lane_refs.size() && cn.input_lane_refs[p]) {
                    const auto& ref = cn.input_lane_refs[p];
                    cn.c_in_lane_views[p].data = ref.data();
                    cn.c_in_lane_views[p].length = ref.length();
                    cn.c_in_lane_views[p].lane_set_id = 0;
                    cn.c_in_lane_views[p].flags = 0;
                }
                // Otherwise keep whatever was set during snapshot unpack (or empty).
            }
            for (uint32_t p = 0; p < cn.output_port_count && p < cn.out_lane_bufs.size(); ++p) {
                cn.out_lane_bufs[p].reset();
            }

            // Check for lane-lifted processing
            auto lift_it = node_to_lift_group_.find(ni);
            if (lift_it != node_to_lift_group_.end()) {
                // ── Lane-lifted: deinterleave → per-lane process → interleave ──
                auto& group = lane_lift_groups_[lift_it->second];
                uint32_t lanes = group.lane_count;
                node_lane_count = lanes;

                // Deinterleave: extract lane c from multi-lane buffers into per-lane mono buffers
                for (uint32_t c = 0; c < lanes; ++c) {
                    for (uint32_t p = 0; p < cn.input_port_count; ++p) {
                        if (p < cn.input_port_types.size() && cn.input_port_types[p] == VIVID_PORT_AUDIO_BUFFER) {
                            const float* mc = a.buffers_in[p].data() + c * buffer_size_;
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
                    ctx.delta_time = static_cast<double>(chunk) / sample_rate_;
                    ctx.frame = audio_frame_ + frames_written;
                    ctx.node_id = cn.node_id.c_str();
                    ctx.param_values = a.audio_local_params.data();
                    ctx.input_buffers = group.per_lane_in_ptrs[c].data();
                    ctx.output_buffers = group.per_lane_out_ptrs[c].data();
                    ctx.buffer_size = chunk;
                    ctx.sample_rate = sample_rate_;
                    ctx.input_channel_counts = nullptr;  // mono view
                    ctx.output_channel_counts = nullptr;
                    ctx.input_lanes = cn.c_in_lane_views.empty() ? nullptr : cn.c_in_lane_views.data();
                    ctx.output_lanes = cn.c_out_lane_outputs.empty() ? nullptr : cn.c_out_lane_outputs.data();
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
                    populate_metronome_context(ctx, metronome);
                    // Note: allocate/retire use lane_state_ directly (not per-node context)

                    auto process_start = AudioClock::now();
                    try {
                        vivid::CrashGuard guard(cn.node_id.c_str());
                        cn.loader->process_audio(group.instances[c], &ctx);
                    } catch (const std::exception& ex) {
                        cn.errored = true;
                        std::strncpy(a.error_message, ex.what(), sizeof(a.error_message) - 1);
                        a.error_message[sizeof(a.error_message) - 1] = '\0';
                    } catch (...) {
                        cn.errored = true;
                        std::strncpy(a.error_message, "unknown exception", sizeof(a.error_message) - 1);
                    }
                    node_process_us += elapsed_us(process_start, AudioClock::now());
                }

                if (cn.errored) {
                    for (auto& buf : a.buffers_out)
                        std::memset(buf.data(), 0, buf.size() * sizeof(float));
                } else {
                    // Interleave: copy per-lane mono output back into multi-lane output buffers.
                    for (uint32_t c = 0; c < lanes; ++c) {
                        for (uint32_t p = 0; p < cn.output_port_count; ++p) {
                            if (p < cn.output_port_types.size() &&
                                (cn.output_port_types[p] == VIVID_PORT_AUDIO_BUFFER ||
                                 cn.output_port_types[p] == VIVID_PORT_SCALAR)) {
                                float* mc = a.buffers_out[p].data() + c * buffer_size_;
                                std::memcpy(mc, group.per_lane_outputs[c][p].data(), chunk * sizeof(float));
                            }
                        }
                    }
                }
            } else if (cn.audio && cn.audio->execution_strategy == LaneExecutionStrategy::LoopBased) {
                // ── LoopBased: single instance, runtime-driven loop over lanes ──
                // Discover lane count from lane inputs at runtime.
                uint32_t loop_lanes = 0;
                for (uint32_t p = 0; p < cn.input_port_count && p < cn.c_in_lane_views.size(); ++p) {
                    if (cn.c_in_lane_views[p].length > loop_lanes)
                        loop_lanes = cn.c_in_lane_views[p].length;
                }
                uint32_t max_ll = graph_->max_loop_lanes;
                if (loop_lanes > max_ll) {
                    std::fprintf(stderr, "[vivid] LoopBased node '%s': lane count %u exceeds max %u, clamping\n",
                                 cn.node_id.c_str(), loop_lanes, max_ll);
                    loop_lanes = max_ll;
                }
                node_lane_count = loop_lanes;

                if (loop_lanes > 0) {
                    // Read identity-bearing lane_ids (pre-allocated scratch).
                    auto* loop_lane_ids = loop_lane_ids_scratch_.data();
                    int32_t lid_port = a.lane_id_port;
                    bool has_identity_ids = false;
                    if (lid_port >= 0 && static_cast<uint32_t>(lid_port) < cn.c_in_lane_views.size()) {
                        const auto& lid_sp = cn.c_in_lane_views[lid_port];
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

                    // Per-lane mono buffer pointers (pre-allocated scratch)
                    auto* loop_in_ptrs = loop_in_ptrs_scratch_.data();
                    auto* loop_out_ptrs = loop_out_ptrs_scratch_.data();

                    for (uint32_t c = 0; c < loop_lanes; ++c) {
                        // Set up per-lane buffer pointers (slice into pre-allocated buffers)
                        for (uint32_t p = 0; p < cn.input_port_count; ++p) {
                            if (p < cn.input_port_types.size() && cn.input_port_types[p] == VIVID_PORT_AUDIO_BUFFER) {
                                loop_in_ptrs[p] = a.buffers_in[p].data() + c * buffer_size_;
                            } else {
                                loop_in_ptrs[p] = a.buffers_in[p].data();  // broadcast non-audio
                            }
                        }
                        for (uint32_t p = 0; p < cn.output_port_count; ++p)
                            loop_out_ptrs[p] = a.buffers_out[p].data() + c * buffer_size_;

                        VividAudioContext ctx{};
                        ctx.time = node_time;
                        ctx.delta_time = static_cast<double>(chunk) / sample_rate_;
                        ctx.frame = audio_frame_ + frames_written;
                        ctx.node_id = cn.node_id.c_str();
                        ctx.param_values = a.audio_local_params.data();
                        ctx.input_buffers = loop_in_ptrs;
                        ctx.output_buffers = loop_out_ptrs;
                        ctx.buffer_size = chunk;
                        ctx.sample_rate = sample_rate_;
                        ctx.input_channel_counts = nullptr;  // mono view
                        ctx.output_channel_counts = nullptr;
                        ctx.input_lanes = cn.c_in_lane_views.empty() ? nullptr : cn.c_in_lane_views.data();
                        ctx.output_lanes = cn.c_out_lane_outputs.empty() ? nullptr : cn.c_out_lane_outputs.data();
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
                        populate_metronome_context(ctx, metronome);

                        auto process_start = AudioClock::now();
                        try {
                            vivid::CrashGuard guard(cn.node_id.c_str());
                            cn.loader->process_audio(audio_instance, &ctx);
                        } catch (const std::exception& ex) {
                            cn.errored = true;
                            std::strncpy(a.error_message, ex.what(), sizeof(a.error_message) - 1);
                            a.error_message[sizeof(a.error_message) - 1] = '\0';
                        } catch (...) {
                            cn.errored = true;
                            std::strncpy(a.error_message, "unknown exception", sizeof(a.error_message) - 1);
                        }
                        node_process_us += elapsed_us(process_start, AudioClock::now());
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
                            p < cn.out_lane_bufs.size() &&
                            loop_lanes <= static_cast<uint32_t>(cn.out_lane_bufs[p].data.size())) {
                            cn.out_lane_bufs[p].committed_length = loop_lanes;
                            for (uint32_t c = 0; c < loop_lanes; ++c) {
                                float* lane_buf = a.buffers_out[p].data() + c * buffer_size_;
                                cn.out_lane_bufs[p].data[c] = (chunk > 0) ? lane_buf[chunk - 1] : 0.0f;
                            }
                        }
                    }
                }
            } else {
                // ── Normal (non-lifted) processing ──
                node_lane_count = 1;
                VividAudioContext ctx{};
                ctx.time = node_time;
                ctx.delta_time = static_cast<double>(chunk) / sample_rate_;
                ctx.frame = audio_frame_ + frames_written;
                ctx.node_id = cn.node_id.c_str();
                ctx.param_values = a.audio_local_params.data();
                ctx.input_buffers = a.in_ptrs.data();
                ctx.output_buffers = a.out_ptrs.data();
                ctx.buffer_size = chunk;
                ctx.sample_rate = sample_rate_;
                ctx.input_channel_counts = a.input_channel_counts.data();
                ctx.output_channel_counts = a.output_channel_counts.data();
                ctx.input_lanes = cn.c_in_lane_views.empty() ? nullptr : cn.c_in_lane_views.data();
                ctx.output_lanes = cn.c_out_lane_outputs.empty() ? nullptr : cn.c_out_lane_outputs.data();
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
                populate_metronome_context(ctx, metronome);

                auto process_start = AudioClock::now();
                try {
                    vivid::CrashGuard guard(cn.node_id.c_str());
                    cn.loader->process_audio(audio_instance, &ctx);
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
                node_process_us += elapsed_us(process_start, AudioClock::now());
            }

            // Publish output lane refs
            for (uint32_t p = 0; p < cn.output_port_count && p < cn.out_lane_bufs.size(); ++p) {
                if (cn.out_lane_bufs[p].committed_length > 0 && p < cn.output_lane_refs.size()) {
                    cn.output_lane_refs[p] = make_ref_from_existing(&cn.out_lane_bufs[p]);
                } else if (p < cn.output_lane_refs.size()) {
                    cn.output_lane_refs[p] = {};
                }
            }
            write_audio_port_telemetry(cn, a, false, chunk, buffer_size_);
            // Note: analysis snapshot reads output_lane_refs directly (no compat sync needed).

            // Route float/lane/custom outputs to downstream audio nodes
            for (uint32_t ei : cg.audio_direct_edges) {
                const auto& e = cg.edges[ei];
                if (e.from_node != ni || e.targets_param) continue;

                if (e.data_type == VIVID_PORT_LANE_ARRAY) {
                    // Lane routing between audio nodes — share ref (zero copy)
                    auto& to_cn = cg.nodes[e.to_node];
                    if (e.from_port < cn.output_lane_refs.size() &&
                        e.to_port < to_cn.input_lane_refs.size()) {
                        to_cn.input_lane_refs[e.to_port] = cn.output_lane_refs[e.from_port];
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

            finalize_node_debug();
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
                    float* R = L + buffer_size_;
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
    double budget_us = static_cast<double>(frame_count) / sample_rate_ * 1e6;
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
        uint8_t ch_count = 1;
        if (gi == static_cast<uint32_t>(sink_node_idx_) && !a.buffers_in.empty()) {
            buf = a.buffers_in[0].data();
            buf_len = std::min(frame_count, static_cast<uint32_t>(a.buffers_in[0].size()));
            ch_count = a.input_channel_counts.empty() ? 1 :
                       std::min<uint8_t>(a.input_channel_counts[0], kMaxWaveformChannels);
        } else if (!a.buffers_out.empty()) {
            buf = a.buffers_out[0].data();
            buf_len = std::min(frame_count, static_cast<uint32_t>(a.buffers_out[0].size()));
            ch_count = a.output_channel_counts.empty() ? 1 :
                       std::min<uint8_t>(a.output_channel_counts[0], kMaxWaveformChannels);
        }
        // Per-channel sample count (planar layout: ch c at buf + c * buffer_size_)
        uint32_t per_ch_len = std::min(buf_len, buffer_size_);

        // RMS & peak per channel (gated by analysis toggle)
        if (analysis_enabled_.load(std::memory_order_relaxed) &&
            buf && per_ch_len > 0 && i < analysis.rms.size()) {
            for (uint8_t ch = 0; ch < ch_count; ++ch) {
                const float* chan = buf + ch * buffer_size_;
                float sum_sq = 0.0f, pk = 0.0f;
                for (uint32_t s = 0; s < per_ch_len; ++s) {
                    float v = chan[s];
                    sum_sq += v * v;
                    float av = std::fabs(v);
                    if (av > pk) pk = av;
                }
                analysis.rms[i][ch] = std::sqrt(sum_sq / per_ch_len);
                analysis.peak[i][ch] = pk;
            }
            for (uint8_t ch = ch_count; ch < kMaxWaveformChannels; ++ch) {
                analysis.rms[i][ch] = 0.0f;
                analysis.peak[i][ch] = 0.0f;
            }
        }

        // Waveform ring per channel (gated by analysis toggle)
        if (analysis_enabled_.load(std::memory_order_relaxed) &&
            buf && per_ch_len > 0 && i < waveform_rings_.size()) {
            for (uint8_t ch = 0; ch < ch_count; ++ch) {
                const float* chan = buf + ch * buffer_size_;
                auto& ring = waveform_rings_[i][ch];
                auto& pos = waveform_ring_pos_[i][ch];
                for (uint32_t s = 0; s < per_ch_len; ++s) {
                    ring[pos] = chan[s];
                    pos = (pos + 1) % 1024;
                }
                if (i < analysis.waveform.size()) {
                    for (uint32_t j = 0; j < 1024; ++j)
                        analysis.waveform[i][ch][j] = ring[(pos + j) % 1024];
                }
            }
        }

        // Spread outputs
        if (i < analysis.lane_outputs.size()) {
            for (size_t p = 0; p < cn.output_port_count && p < analysis.lane_outputs[i].size(); ++p) {
                auto& dst = analysis.lane_outputs[i][p];
                if (p < cn.output_lane_refs.size() && cn.output_lane_refs[p]) {
                    const auto& ref = cn.output_lane_refs[p];
                    dst.length = std::min(ref.length(), dst.capacity);
                    std::memcpy(dst.data, ref.data(), dst.length * sizeof(float));
                } else {
                    dst.length = 0;
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
