#pragma once

#include "operator_api/operator.h"
#include <unordered_map>
#include <string>
#include <cassert>
#include <cstdio>

namespace vivid {

// Copy metronome fields between any two context structs that carry them.
template <typename Dst, typename Src>
inline void copy_metronome_fields(Dst& dst, const Src& src) {
    dst.metronome_bpm            = src.metronome_bpm;
    dst.metronome_beats_per_bar  = src.metronome_beats_per_bar;
    dst.metronome_beats_elapsed  = src.metronome_beats_elapsed;
    dst.metronome_beat_phase     = src.metronome_beat_phase;
    dst.metronome_bar_phase      = src.metronome_bar_phase;
    dst.metronome_beat_ms        = src.metronome_beat_ms;
}

// ---------------------------------------------------------------------------
// ChildOp<T> — embed an operator as a persistent member variable
//
// Usage:
//   #include "operator_api/child_op.h"
//   #include "control/lfo/lfo.h"
//
//   struct MyOp : vivid::OperatorBase, vivid::FrameProcessable {
//       vivid::ChildOp<LFO> lfo;
//       void process_frame(const VividFrameContext* ctx) override {
//           lfo.set_param("frequency", 2.0f);
//           lfo.process(ctx);
//           float mod = lfo.output("value");
//       }
//   };
// ---------------------------------------------------------------------------

template<typename T>
class ChildOp {
public:
    ChildOp() { init(); }

    // -- Param access -------------------------------------------------------

    void set_param(uint32_t index, float value) {
        assert(index < param_values_.size());
        param_values_[index] = value;
    }

    void set_param(const char* name, float value) {
        auto it = param_name_to_index_.find(name);
        assert(it != param_name_to_index_.end());
        set_param(it->second, value);
    }

    float param(uint32_t index) const {
        assert(index < param_values_.size());
        return param_values_[index];
    }

    float param(const char* name) const {
        auto it = param_name_to_index_.find(name);
        assert(it != param_name_to_index_.end());
        return param(it->second);
    }

    // -- Input access -------------------------------------------------------

    void set_input(uint32_t index, float value) {
        assert(index < input_values_.size());
        input_values_[index] = value;
    }

    void set_input(const char* name, float value) {
        auto it = input_name_to_index_.find(name);
        assert(it != input_name_to_index_.end());
        set_input(it->second, value);
    }

    // -- Spread input access ------------------------------------------------

    void set_input_lane_data(uint32_t index, const float* data, uint32_t length) {
        assert(index < input_lanes_.size());
        auto& sp = input_lanes_[index];
        sp.resize(length);
        for (uint32_t i = 0; i < length; ++i)
            sp[i] = data[i];
    }

    void set_input_lane_data(const char* name, const float* data, uint32_t length) {
        auto it = input_name_to_index_.find(name);
        assert(it != input_name_to_index_.end());
        set_input_lane_data(it->second, data, length);
    }

    // -- Output access ------------------------------------------------------

    float output(uint32_t index) const {
        assert(index < output_values_.size());
        return output_values_[index];
    }

    float output(const char* name) const {
        auto it = output_name_to_index_.find(name);
        assert(it != output_name_to_index_.end());
        return output(it->second);
    }

    // -- Spread output access -----------------------------------------------

    const float* output_lane_data(uint32_t index) const {
        assert(index < output_lanes_.size());
        return output_lanes_[index].data();
    }

    uint32_t output_lane_length(uint32_t index) const {
        assert(index < output_lanes_.size());
        return static_cast<uint32_t>(output_lanes_[index].size());
    }

    const float* output_lane_data(const char* name) const {
        auto it = output_name_to_index_.find(name);
        assert(it != output_name_to_index_.end());
        return output_lane_data(it->second);
    }

    uint32_t output_lane_length(const char* name) const {
        auto it = output_name_to_index_.find(name);
        assert(it != output_name_to_index_.end());
        return output_lane_length(it->second);
    }

    // -- Process ------------------------------------------------------------

    void process(const VividFrameContext* parent_ctx) {
        sync_params_();
        sync_lanes_();

        if constexpr (std::is_base_of_v<AudioProcessable, T>) {
            // Audio child op: process a single sample per control frame
            // and extract the output into output_values_.
            ensure_audio_buffers_();

            VividAudioContext audio_ctx{};
            audio_ctx.time         = parent_ctx->time;
            audio_ctx.delta_time   = parent_ctx->delta_time;
            audio_ctx.frame        = parent_ctx->frame;
            audio_ctx.param_values = param_values_.data();
            audio_ctx.input_buffers  = audio_in_ptrs_.data();
            audio_ctx.output_buffers = audio_out_ptrs_.data();
            audio_ctx.buffer_size    = 1;  // single sample per control frame
            // Set sample_rate so 1/sample_rate ≈ delta_time, preserving timing semantics
            audio_ctx.sample_rate    = (parent_ctx->delta_time > 0.0)
                ? static_cast<uint32_t>(1.0 / parent_ctx->delta_time)
                : 48000;
            audio_ctx.input_channel_counts  = nullptr;
            audio_ctx.output_channel_counts = nullptr;
            audio_ctx.input_lanes  = c_input_lanes_.empty() ? nullptr : c_input_lanes_.data();
            audio_ctx.output_lanes = c_output_lanes_.empty() ? nullptr : c_output_lanes_.data();
            audio_ctx.custom_inputs       = nullptr;
            audio_ctx.custom_input_count  = 0;
            audio_ctx.custom_outputs      = nullptr;
            audio_ctx.custom_output_count = 0;
            audio_ctx.input_string_values = nullptr;
            audio_ctx.file_param_values   = nullptr;
            audio_ctx.file_param_count    = 0;
            audio_ctx.shared_handles      = nullptr;
            copy_metronome_fields(audio_ctx, *parent_ctx);

            op_.process_audio(&audio_ctx);

            // Extract output from single-sample buffers
            for (size_t i = 0; i < output_values_.size(); ++i) {
                output_values_[i] = audio_out_bufs_[i][0];
            }
        } else {
            // Control child op: standard VividFrameContext dispatch
            VividFrameContext child_ctx{};
            child_ctx.time         = parent_ctx->time;
            child_ctx.delta_time   = parent_ctx->delta_time;
            child_ctx.frame        = parent_ctx->frame;
            child_ctx.param_values = param_values_.data();
            child_ctx.input_values = input_values_.empty() ? nullptr : input_values_.data();
            child_ctx.output_values = output_values_.empty() ? nullptr : output_values_.data();
            child_ctx.input_lanes  = c_input_lanes_.empty() ? nullptr : c_input_lanes_.data();
            child_ctx.output_lanes = c_output_lanes_.empty() ? nullptr : c_output_lanes_.data();
            child_ctx.file_param_values = nullptr;
            child_ctx.file_param_count  = 0;
            child_ctx.preferred_tex_width  = 0;
            child_ctx.preferred_tex_height = 0;
            copy_metronome_fields(child_ctx, *parent_ctx);

            op_.process_frame(&child_ctx);
        }

        readback_lanes_();
    }

    // -- Audio-cadence process -----------------------------------------------
    // Call this instead of process() when the parent runs at audio cadence.
    // Forwards the parent's VividAudioContext to the child with correct
    // sample_rate, shared_handles, and lane metadata.

    void process_audio(const VividAudioContext* parent_ctx) {
        sync_params_();
        sync_lanes_();

        if constexpr (std::is_base_of_v<AudioProcessable, T>) {
            // Child supports audio: forward audio context
            ensure_audio_buffers_();

            // Copy input values into single-sample input buffers
            for (size_t i = 0; i < input_values_.size(); ++i)
                audio_in_bufs_[i][0] = input_values_[i];

            VividAudioContext child_ctx{};
            child_ctx.time              = parent_ctx->time;
            child_ctx.delta_time        = parent_ctx->delta_time;
            child_ctx.frame             = parent_ctx->frame;
            child_ctx.param_values      = param_values_.data();
            child_ctx.input_buffers     = audio_in_ptrs_.data();
            child_ctx.output_buffers    = audio_out_ptrs_.data();
            child_ctx.buffer_size       = 1;  // single sample per parent callback
            child_ctx.sample_rate       = parent_ctx->sample_rate;
            child_ctx.input_channel_counts  = nullptr;
            child_ctx.output_channel_counts = nullptr;
            child_ctx.input_lanes     = c_input_lanes_.empty() ? nullptr : c_input_lanes_.data();
            child_ctx.output_lanes    = c_output_lanes_.empty() ? nullptr : c_output_lanes_.data();
            child_ctx.custom_inputs       = nullptr;
            child_ctx.custom_input_count  = 0;
            child_ctx.custom_outputs      = nullptr;
            child_ctx.custom_output_count = 0;
            child_ctx.input_string_values = nullptr;
            child_ctx.file_param_values   = nullptr;
            child_ctx.file_param_count    = 0;
            child_ctx.shared_handles      = parent_ctx->shared_handles;
            child_ctx.lane_count          = parent_ctx->lane_count;
            child_ctx.lane_index          = parent_ctx->lane_index;
            child_ctx.lane_set_id         = parent_ctx->lane_set_id;
            child_ctx.lane_id             = parent_ctx->lane_id;
            copy_metronome_fields(child_ctx, *parent_ctx);

            op_.process_audio(&child_ctx);

            // Extract output from single-sample buffers
            for (size_t i = 0; i < output_values_.size(); ++i)
                output_values_[i] = audio_out_bufs_[i][0];
        } else if constexpr (std::is_base_of_v<FrameProcessable, T>) {
            // Child is frame-only: build a frame context from audio data
            VividFrameContext child_ctx{};
            child_ctx.time         = parent_ctx->time;
            child_ctx.delta_time   = parent_ctx->delta_time;
            child_ctx.frame        = parent_ctx->frame;
            child_ctx.param_values = param_values_.data();
            child_ctx.input_values = input_values_.empty() ? nullptr : input_values_.data();
            child_ctx.output_values = output_values_.empty() ? nullptr : output_values_.data();
            child_ctx.input_lanes  = c_input_lanes_.empty() ? nullptr : c_input_lanes_.data();
            child_ctx.output_lanes = c_output_lanes_.empty() ? nullptr : c_output_lanes_.data();
            child_ctx.file_param_values = nullptr;
            child_ctx.file_param_count  = 0;
            child_ctx.preferred_tex_width  = 0;
            child_ctx.preferred_tex_height = 0;
            copy_metronome_fields(child_ctx, *parent_ctx);

            op_.process_frame(&child_ctx);
        }

        readback_lanes_();
    }

    // -- Direct access to underlying operator instance ----------------------

    T&       op()       { return op_; }
    const T& op() const { return op_; }

    // -- Introspection ------------------------------------------------------

    uint32_t param_count()  const { return static_cast<uint32_t>(param_ptrs_.size()); }
    uint32_t input_count()  const { return static_cast<uint32_t>(input_values_.size()); }
    uint32_t output_count() const { return static_cast<uint32_t>(output_values_.size()); }

private:
    void init() {
        // Collect param metadata
        op_.collect_params(param_ptrs_);
        param_values_.resize(param_ptrs_.size());
        for (size_t i = 0; i < param_ptrs_.size(); ++i) {
            param_values_[i] = param_ptrs_[i]->default_value;
            param_name_to_index_[param_ptrs_[i]->name] = static_cast<uint32_t>(i);
        }

        // Collect port metadata and split into input/output arrays
        std::vector<VividPortDescriptor> ports;
        op_.collect_ports(ports);

        uint32_t in_idx = 0, out_idx = 0;
        for (auto& port : ports) {
            if (port.direction == VIVID_PORT_INPUT) {
                input_name_to_index_[port.name] = in_idx;
                in_idx++;
            } else {
                output_name_to_index_[port.name] = out_idx;
                out_idx++;
            }
        }

        input_values_.resize(in_idx, 0.0f);
        output_values_.resize(out_idx, 0.0f);
        input_lanes_.resize(in_idx);
        output_lanes_.resize(out_idx);
        c_input_lanes_.resize(in_idx);
        c_output_lanes_.resize(out_idx);
    }

    T op_;

    // Param storage
    std::vector<ParamBase*> param_ptrs_;
    std::vector<float>      param_values_;
    std::unordered_map<std::string, uint32_t> param_name_to_index_;

    // Port storage
    std::vector<float> input_values_;
    std::vector<float> output_values_;
    std::unordered_map<std::string, uint32_t> input_name_to_index_;
    std::unordered_map<std::string, uint32_t> output_name_to_index_;

    // Spread storage
    std::vector<std::vector<float>> input_lanes_;
    std::vector<std::vector<float>> output_lanes_;
    std::vector<VividLanePort> c_input_lanes_;
    std::vector<VividLanePort> c_output_lanes_;

    // Audio buffer storage (only used for AudioProcessable children)
    std::vector<std::vector<float>> audio_in_bufs_;
    std::vector<std::vector<float>> audio_out_bufs_;
    std::vector<float*> audio_in_ptrs_;
    std::vector<float*> audio_out_ptrs_;

    void sync_params_() {
        for (size_t i = 0; i < param_ptrs_.size(); ++i) {
            if (param_ptrs_[i]->type != VIVID_PARAM_FILE &&
                param_ptrs_[i]->type != VIVID_PARAM_TEXT) {
                param_ptrs_[i]->value = param_values_[i];
            }
        }
    }

    void sync_lanes_() {
        for (size_t i = 0; i < input_lanes_.size(); ++i) {
            c_input_lanes_[i].data     = input_lanes_[i].empty() ? nullptr : input_lanes_[i].data();
            c_input_lanes_[i].length   = static_cast<uint32_t>(input_lanes_[i].size());
            c_input_lanes_[i].capacity = c_input_lanes_[i].length;
        }
        for (size_t i = 0; i < output_lanes_.size(); ++i) {
            if (output_lanes_[i].capacity() < 256)
                output_lanes_[i].reserve(256);
            c_output_lanes_[i].data     = output_lanes_[i].data();
            c_output_lanes_[i].length   = static_cast<uint32_t>(output_lanes_[i].size());
            c_output_lanes_[i].capacity = static_cast<uint32_t>(output_lanes_[i].capacity());
        }
    }

    void readback_lanes_() {
        for (size_t i = 0; i < output_lanes_.size(); ++i) {
            uint32_t len = c_output_lanes_[i].length;
            uint32_t cap = c_output_lanes_[i].capacity;
            if (len > cap) {
                std::fprintf(stderr, "[vivid] ChildOp: output lane array %zu wrote %u elements "
                             "but capacity was %u, clamping\n", i, len, cap);
                len = cap;
            }
            output_lanes_[i].resize(len);
        }
    }

    void ensure_audio_buffers_() {
        if (audio_in_bufs_.size() == input_values_.size()) return;
        audio_in_bufs_.resize(input_values_.size(), std::vector<float>(1, 0.0f));
        audio_out_bufs_.resize(output_values_.size(), std::vector<float>(1, 0.0f));
        audio_in_ptrs_.resize(input_values_.size());
        audio_out_ptrs_.resize(output_values_.size());
        for (size_t i = 0; i < audio_in_bufs_.size(); ++i)
            audio_in_ptrs_[i] = audio_in_bufs_[i].data();
        for (size_t i = 0; i < audio_out_bufs_.size(); ++i)
            audio_out_ptrs_[i] = audio_out_bufs_[i].data();
    }
};

} // namespace vivid
