#include <miniaudio.h>

#include "runtime/audio_executor.h"
#include "runtime/cadence_bridge.h"
#include "runtime/crash_guard.h"
#include "runtime/shared_handle_registry.h"
#include "operator_api/type_id.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace vivid {

static float unset_signal_output_sentinel() {
    return std::numeric_limits<float>::quiet_NaN();
}

AudioExecutor::AudioExecutor() = default;

AudioExecutor::~AudioExecutor() {
    shutdown();
}

bool AudioExecutor::build(CompiledGraph& cg) {
    sink_node_idx_ = -1;
    auto_dup_groups_.clear();
    node_to_dup_group_.clear();

    if (cg.audio_order.empty()) return false;

    // Detect sink node (audio_out)
    for (uint32_t idx : cg.audio_order) {
        if (cg.nodes[idx].type_name == "audio_out") {
            sink_node_idx_ = static_cast<int>(idx);
            break;
        }
    }

    // Set up auto-dup groups for mono operators in multi-channel chains
    for (uint32_t idx : cg.audio_order) {
        auto& cn = cg.nodes[idx];
        if (!cn.is_mono_autodup || !cn.loader) continue;

        // Find max incoming wire channel count
        uint8_t ch_count = 1;
        for (const auto& e : cg.edges) {
            if (e.to_node == idx && e.transport == EdgeTransport::Direct && !e.targets_param) {
                uint8_t src_ch = 1;
                if (e.from_port < cg.nodes[e.from_node].output_channel_counts.size())
                    src_ch = cg.nodes[e.from_node].output_channel_counts[e.from_port];
                if (src_ch > ch_count) ch_count = src_ch;
            }
        }
        if (ch_count <= 1) continue;

        AutoDupGroup group;
        group.node_idx = idx;
        group.channel_count = ch_count;

        // Create additional instances (channel 0 uses primary instance)
        group.instances.resize(ch_count);
        group.instances[0] = cn.instance;
        for (uint8_t c = 1; c < ch_count; ++c)
            group.instances[c] = cn.loader->create_instance();

        // Allocate per-channel mono buffers
        group.per_ch_inputs.resize(ch_count);
        group.per_ch_outputs.resize(ch_count);
        group.per_ch_in_ptrs.resize(ch_count);
        group.per_ch_out_ptrs.resize(ch_count);
        for (uint8_t c = 0; c < ch_count; ++c) {
            group.per_ch_inputs[c].resize(cn.input_port_count,
                std::vector<float>(kBufferSize, 0.0f));
            group.per_ch_outputs[c].resize(cn.output_port_count,
                std::vector<float>(kBufferSize, 0.0f));
            group.per_ch_in_ptrs[c].resize(cn.input_port_count);
            group.per_ch_out_ptrs[c].resize(cn.output_port_count);
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                group.per_ch_in_ptrs[c][p] = group.per_ch_inputs[c][p].data();
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                group.per_ch_out_ptrs[c][p] = group.per_ch_outputs[c][p].data();
        }

        uint32_t group_idx = static_cast<uint32_t>(auto_dup_groups_.size());
        node_to_dup_group_[idx] = group_idx;
        auto_dup_groups_.push_back(std::move(group));
    }

    // Allocate waveform ring buffers
    uint32_t audio_count = static_cast<uint32_t>(cg.audio_order.size());
    waveform_rings_.resize(audio_count);
    waveform_ring_pos_.assign(audio_count, 0);
    for (auto& ring : waveform_rings_) ring.fill(0.0f);

    return true;
}

bool AudioExecutor::start(CadenceBridge& bridge, CompiledGraph& cg, bool use_null_device) {
    bridge_ = &bridge;
    graph_ = &cg;

    if (use_null_device) {
        running_ = true;
        return true;
    }

    // Configure miniaudio
    device_ = new ma_device;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kSampleRate;
    config.periodSizeInFrames = kBufferSize;
    config.dataCallback = ma_data_callback;
    config.pUserData = this;

    if (ma_device_init(nullptr, &config, device_) != MA_SUCCESS) {
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

    // Destroy auto-dup extra instances
    for (auto& group : auto_dup_groups_) {
        if (group.node_idx < graph_->nodes.size()) {
            auto& cn = graph_->nodes[group.node_idx];
            for (size_t c = 1; c < group.instances.size(); ++c) {
                if (group.instances[c] && cn.loader)
                    cn.loader->destroy_instance(group.instances[c]);
            }
        }
    }
    auto_dup_groups_.clear();
    node_to_dup_group_.clear();
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
        std::memset(output, 0, frame_count * 2 * sizeof(float));
        return;
    }

    const auto& snap = bridge_->active_params();
    auto& cg = *graph_;

    // Apply params from snapshot to audio nodes
    for (uint32_t i = 0; i < static_cast<uint32_t>(cg.audio_order.size()); ++i) {
        uint32_t gi = cg.audio_order[i];
        auto& cn = cg.nodes[gi];

        // Copy params
        if (i < snap.node_params.size()) {
            for (size_t p = 0; p < cn.param_values.size() && p < snap.node_params[i].size(); ++p)
                cn.param_values[p] = snap.node_params[i][p];
        }
        // Copy float inputs
        if (i < snap.float_input_values.size()) {
            for (size_t p = 0; p < cn.float_input_values.size() && p < snap.float_input_values[i].size(); ++p)
                cn.float_input_values[p] = snap.float_input_values[i][p];
        }
        // Copy spread inputs from snapshot to CompiledNode
        if (cn.has_spread_ports && i < snap.spread_inputs.size()) {
            for (size_t p = 0; p < cn.input_port_count && p < snap.spread_inputs[i].size(); ++p) {
                const auto& ss = snap.spread_inputs[i][p];
                if (p < cn.input_spreads.size()) {
                    cn.input_spreads[p].assign(ss.data, ss.data + ss.length);
                }
            }
        }
        // Copy string inputs
        if (cn.has_string_input_ports && i < snap.input_string_values.size()) {
            for (size_t p = 0; p < cn.input_port_count && p < snap.input_string_values[i].size(); ++p) {
                if (p < cn.input_string_values.size())
                    cn.input_string_values[p] = snap.input_string_values[i][p];
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

            // Zero input buffers
            for (auto& buf : cn.audio_buffers_in)
                std::memset(buf.data(), 0, buf.size() * sizeof(float));

            // Solo mode check
            if (!snap.solo_active_set.empty() && ni_ord < snap.solo_active_set.size() &&
                !snap.solo_active_set[ni_ord] && ni != static_cast<uint32_t>(sink_node_idx_)) {
                for (auto& buf : cn.audio_buffers_out)
                    std::memset(buf.data(), 0, buf.size() * sizeof(float));
                continue;
            }

            // Route audio buffers from upstream (Direct edges)
            for (uint32_t ei : cg.audio_direct_edges) {
                const auto& e = cg.edges[ei];
                if (e.to_node != ni || e.targets_param) continue;
                if (e.data_type != VIVID_PORT_AUDIO && e.data_type != VIVID_PORT_SIGNAL) continue;

                auto& from_cn = cg.nodes[e.from_node];
                if (e.from_port >= from_cn.audio_buffers_out.size() ||
                    e.to_port >= cn.audio_buffers_in.size()) continue;

                float* src = from_cn.audio_buffers_out[e.from_port].data();
                float* dst = cn.audio_buffers_in[e.to_port].data();
                uint8_t fc = e.from_channels;
                uint8_t tc = e.to_channels;
                float scale = (e.from_min != 0.0f || e.from_max != 1.0f ||
                               e.to_min != 0.0f || e.to_max != 1.0f)
                    ? (e.from_max - e.from_min != 0.0f
                        ? (e.to_max - e.to_min) / (e.from_max - e.from_min) : 1.0f)
                    : 1.0f;

                if (fc == tc) {
                    for (uint8_t c = 0; c < fc; ++c) {
                        float* sc = src + c * chunk;
                        float* dc = dst + c * chunk;
                        for (uint32_t s = 0; s < chunk; ++s)
                            dc[s] += sc[s] * scale;
                    }
                } else if (fc == 1 && tc > 1) {
                    for (uint8_t c = 0; c < tc; ++c) {
                        float* dc = dst + c * chunk;
                        for (uint32_t s = 0; s < chunk; ++s)
                            dc[s] += src[s] * scale;
                    }
                } else if (fc > 1 && tc == 1) {
                    float inv_n = 1.0f / fc;
                    for (uint32_t s = 0; s < chunk; ++s) {
                        float sum = 0.0f;
                        for (uint8_t c = 0; c < fc; ++c)
                            sum += src[c * chunk + s];
                        dst[s] += sum * inv_n * scale;
                    }
                } else {
                    uint8_t min_ch = std::min(fc, tc);
                    for (uint8_t c = 0; c < min_ch; ++c) {
                        float* sc = src + c * chunk;
                        float* dc = dst + c * chunk;
                        for (uint32_t s = 0; s < chunk; ++s)
                            dc[s] += sc[s] * scale;
                    }
                }
            }

            // Set up buffer pointers
            for (uint32_t p = 0; p < cn.input_port_count; ++p)
                cn.audio_in_ptrs[p] = cn.audio_buffers_in[p].data();
            for (uint32_t p = 0; p < cn.output_port_count; ++p)
                cn.audio_out_ptrs[p] = cn.audio_buffers_out[p].data();

            double node_time = static_cast<double>(audio_frame_ + frames_written) / kSampleRate;

            // Reset float outputs to NaN sentinel
            for (auto& fv : cn.float_output_values)
                fv = unset_signal_output_sentinel();

            // Process
            if (!cn.loader || cn.errored) continue;

            // Set up spread ports for context
            for (uint32_t p = 0; p < cn.input_port_count && p < cn.c_in_spreads.size(); ++p) {
                if (p < cn.input_spreads.size()) {
                    cn.c_in_spreads[p].data = cn.input_spreads[p].empty() ? nullptr : cn.input_spreads[p].data();
                    cn.c_in_spreads[p].length = static_cast<uint32_t>(cn.input_spreads[p].size());
                    cn.c_in_spreads[p].capacity = static_cast<uint32_t>(cn.input_spreads[p].size());
                } else {
                    cn.c_in_spreads[p].data = nullptr;
                    cn.c_in_spreads[p].length = 0;
                    cn.c_in_spreads[p].capacity = 0;
                }
            }
            for (uint32_t p = 0; p < cn.output_port_count && p < cn.c_out_spreads.size(); ++p) {
                cn.c_out_spreads[p].data = cn.out_spread_buf[p].data();
                cn.c_out_spreads[p].length = 0;
                cn.c_out_spreads[p].capacity = static_cast<uint32_t>(cn.out_spread_buf[p].size());
            }

            // Build VividAudioContext
            VividAudioContext ctx{};
            ctx.time = node_time;
            ctx.delta_time = static_cast<double>(chunk) / kSampleRate;
            ctx.frame = audio_frame_ + frames_written;
            ctx.param_values = cn.param_values.data();
            ctx.input_buffers = cn.audio_in_ptrs.data();
            ctx.output_buffers = cn.audio_out_ptrs.data();
            ctx.buffer_size = chunk;
            ctx.sample_rate = kSampleRate;
            ctx.input_channel_counts = cn.input_channel_counts.data();
            ctx.output_channel_counts = cn.output_channel_counts.data();
            ctx.input_float_values = cn.float_input_values.empty() ? cn.float_input_scratch : cn.float_input_values.data();
            ctx.output_float_values = cn.float_output_values.empty() ? cn.float_output_scratch : cn.float_output_values.data();
            ctx.input_spreads = cn.c_in_spreads.empty() ? nullptr : cn.c_in_spreads.data();
            ctx.output_spreads = cn.c_out_spreads.empty() ? nullptr : cn.c_out_spreads.data();
            ctx.input_string_values = cn.c_input_string_values.empty() ? nullptr : cn.c_input_string_values.data();
            ctx.shared_handles = vivid::shared_handle_service();
            ctx.file_param_values = cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data();
            ctx.file_param_count = static_cast<uint32_t>(cn.file_param_ptrs.size());

            try {
                cn.loader->process_audio(cn.instance, &ctx);
            } catch (const std::exception& ex) {
                cn.errored = true;
                std::strncpy(cn.audio_error_message, ex.what(), sizeof(cn.audio_error_message) - 1);
                cn.audio_error_message[sizeof(cn.audio_error_message) - 1] = '\0';
                for (auto& buf : cn.audio_buffers_out)
                    std::memset(buf.data(), 0, buf.size() * sizeof(float));
            } catch (...) {
                cn.errored = true;
                std::strncpy(cn.audio_error_message, "unknown exception", sizeof(cn.audio_error_message) - 1);
                for (auto& buf : cn.audio_buffers_out)
                    std::memset(buf.data(), 0, buf.size() * sizeof(float));
            }

            // Read back spread outputs
            for (uint32_t p = 0; p < cn.output_port_count && p < cn.c_out_spreads.size(); ++p) {
                if (cn.c_out_spreads[p].length > 0 && p < cn.output_spreads.size()) {
                    cn.output_spreads[p].assign(
                        cn.out_spread_buf[p].begin(),
                        cn.out_spread_buf[p].begin() + cn.c_out_spreads[p].length);
                }
            }

            // SIGNAL output auto-extraction
            for (const auto& se : cn.signal_output_extractions) {
                if (se.float_ordinal < cn.float_output_values.size()) {
                    float fv = cn.float_output_values[se.float_ordinal];
                    if (std::isnan(fv)) {
                        // Auto-extract last sample from buffer
                        if (se.port_idx < cn.audio_buffers_out.size() && chunk > 0)
                            cn.float_output_values[se.float_ordinal] =
                                cn.audio_buffers_out[se.port_idx][chunk - 1];
                    }
                }
            }
            // Replace remaining NaN with 0
            for (auto& fv : cn.float_output_values)
                if (std::isnan(fv)) fv = 0.0f;

            // Route float/spread/custom outputs to downstream audio nodes
            for (uint32_t ei : cg.audio_direct_edges) {
                const auto& e = cg.edges[ei];
                if (e.from_node != ni || e.targets_param) continue;

                // Float port routing (SIGNAL→SIGNAL between audio nodes)
                if (e.data_type == VIVID_PORT_SIGNAL) {
                    // Find float ordinals
                    uint32_t from_ord = 0;
                    for (uint32_t p = 0; p < e.from_port && p < cn.output_port_types.size(); ++p)
                        if (cn.output_port_types[p] == VIVID_PORT_SIGNAL) from_ord++;
                    auto& to_cn = cg.nodes[e.to_node];
                    uint32_t to_ord = 0;
                    for (uint32_t p = 0; p < e.to_port && p < to_cn.input_port_types.size(); ++p)
                        if (to_cn.input_port_types[p] == VIVID_PORT_SIGNAL) to_ord++;

                    float scale = (e.from_max - e.from_min != 0.0f)
                        ? (e.to_max - e.to_min) / (e.from_max - e.from_min) : 1.0f;
                    if (from_ord < cn.float_output_values.size() &&
                        to_ord < to_cn.float_input_values.size())
                        to_cn.float_input_values[to_ord] = cn.float_output_values[from_ord] * scale;
                } else if (e.data_type == VIVID_PORT_SPREAD) {
                    // Spread routing between audio nodes
                    auto& to_cn = cg.nodes[e.to_node];
                    if (e.from_port < cn.output_spreads.size() &&
                        e.to_port < to_cn.input_spreads.size()) {
                        const auto& src = cn.output_spreads[e.from_port];
                        to_cn.input_spreads[e.to_port].assign(src.begin(), src.end());
                    }
                }
            }
        }

        // Sink extraction — interleave to device output
        float* dst = output + frames_written * 2;
        if (sink_node_idx_ >= 0 && static_cast<uint32_t>(sink_node_idx_) < cg.nodes.size()) {
            auto& sink = cg.nodes[sink_node_idx_];
            // audio_out reads from input buffers
            if (!sink.audio_buffers_in.empty() && sink.audio_buffers_in[0].size() >= chunk) {
                float* L = sink.audio_buffers_in[0].data();
                uint8_t ch = sink.input_channel_counts.empty() ? 1 : sink.input_channel_counts[0];
                if (ch >= 2) {
                    float* R = L + chunk;
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

        // Get buffer to analyze (sink uses input, others use output)
        float* buf = nullptr;
        uint32_t buf_len = 0;
        if (gi == static_cast<uint32_t>(sink_node_idx_) && !cn.audio_buffers_in.empty()) {
            buf = cn.audio_buffers_in[0].data();
            buf_len = std::min(frame_count, static_cast<uint32_t>(cn.audio_buffers_in[0].size()));
        } else if (!cn.audio_buffers_out.empty()) {
            buf = cn.audio_buffers_out[0].data();
            buf_len = std::min(frame_count, static_cast<uint32_t>(cn.audio_buffers_out[0].size()));
        }

        // RMS & peak
        if (buf && buf_len > 0 && i < analysis.rms.size()) {
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

        // Waveform ring
        if (buf && buf_len > 0 && i < waveform_rings_.size()) {
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

        // Float outputs
        if (i < analysis.float_outputs.size()) {
            for (size_t p = 0; p < cn.float_output_values.size() && p < analysis.float_outputs[i].size(); ++p)
                analysis.float_outputs[i][p] = cn.float_output_values[p];
        }

        // Spread outputs
        if (i < analysis.spread_outputs.size()) {
            for (size_t p = 0; p < cn.output_port_count && p < analysis.spread_outputs[i].size(); ++p) {
                if (p < cn.output_spreads.size()) {
                    auto& dst = analysis.spread_outputs[i][p];
                    const auto& src = cn.output_spreads[p];
                    dst.length = std::min(static_cast<uint32_t>(src.size()),
                                          SpreadSnapshot::kMaxLength);
                    for (uint32_t j = 0; j < dst.length; ++j)
                        dst.data[j] = src[j];
                }
            }
        }

        // Error state
        if (i < analysis.errored.size()) {
            analysis.errored[i] = cn.errored;
            if (cn.errored && i < analysis.error_msgs.size()) {
                std::strncpy(analysis.error_msgs[i].data(), cn.audio_error_message,
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
