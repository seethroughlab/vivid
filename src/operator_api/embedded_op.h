#pragma once

#include "operator_api/operator.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <functional>
#include <cassert>
#include <cstdio>
#include <utility>

namespace vivid {

// ---------------------------------------------------------------------------
// EmbeddedOp — runtime-polymorphic owned operator for embedded composition
//
// Like ChildOp<T> but the operator type is selected at runtime. Use this
// when the embedded operator type isn't known at compile time (e.g., a synth
// slot that can hold an Envelope, LFO, MSEG, or other modulator).
//
// Usage:
//   #include "operator_api/embedded_op.h"
//
//   EmbeddedOp slot(std::make_unique<Envelope>());
//   slot.set_param("attack", 0.1f);
//   slot.process(parent_ctx);
//   float val = slot.output("value");
// ---------------------------------------------------------------------------

class EmbeddedOp {
public:
    // Takes ownership of an already-constructed operator.
    template<typename T, typename D,
             typename = std::enable_if_t<std::is_base_of_v<OperatorBase, T>>>
    explicit EmbeddedOp(std::unique_ptr<T, D> op)
        : op_(op.release(), std::default_delete<OperatorBase>{})
    {
        init();
    }

    // -- Param access -------------------------------------------------------

    void set_param(uint32_t index, float value) {
        assert(index < param_values_.size());
        param_values_[index] = value;
    }

    void set_param(const char* name, float value) {
        auto it = param_name_to_index_.find(name);
        if (it == param_name_to_index_.end()) return;  // silently ignore unknown params
        set_param(it->second, value);
    }

    float param(const char* name) const {
        auto it = param_name_to_index_.find(name);
        assert(it != param_name_to_index_.end());
        return param_values_[it->second];
    }

    bool has_param(const char* name) const {
        return param_name_to_index_.count(name) > 0;
    }

    // -- Input access -------------------------------------------------------

    void set_input(const char* name, float value) {
        auto it = input_name_to_index_.find(name);
        if (it == input_name_to_index_.end()) return;
        assert(it->second < input_values_.size());
        input_values_[it->second] = value;
    }

    bool has_input(const char* name) const {
        return input_name_to_index_.count(name) > 0;
    }

    // -- Output access ------------------------------------------------------

    float output(const char* name) const {
        auto it = output_name_to_index_.find(name);
        assert(it != output_name_to_index_.end());
        return output_values_[it->second];
    }

    bool has_output(const char* name) const {
        return output_name_to_index_.count(name) > 0;
    }

    // -- Process ------------------------------------------------------------

    void process(const VividFrameContext* parent_ctx) {
        // Sync param_values_ into operator's Param<T>.value fields
        for (size_t i = 0; i < param_ptrs_.size(); ++i) {
            if (param_ptrs_[i]->type != VIVID_PARAM_FILE &&
                param_ptrs_[i]->type != VIVID_PARAM_TEXT) {
                param_ptrs_[i]->value = param_values_[i];
            }
        }

        // Rewrite spread port arrays for the C context
        for (size_t i = 0; i < input_spreads_.size(); ++i) {
            c_input_spreads_[i].data     = input_spreads_[i].empty() ? nullptr : input_spreads_[i].data();
            c_input_spreads_[i].length   = static_cast<uint32_t>(input_spreads_[i].size());
            c_input_spreads_[i].capacity = c_input_spreads_[i].length;
        }
        for (size_t i = 0; i < output_spreads_.size(); ++i) {
            if (output_spreads_[i].capacity() < 256)
                output_spreads_[i].reserve(256);
            c_output_spreads_[i].data     = output_spreads_[i].data();
            c_output_spreads_[i].length   = static_cast<uint32_t>(output_spreads_[i].size());
            c_output_spreads_[i].capacity = static_cast<uint32_t>(output_spreads_[i].capacity());
        }

        if (uses_audio_cadence_) {
            ensure_audio_buffers_();

            VividAudioContext audio_ctx{};
            audio_ctx.time         = parent_ctx->time;
            audio_ctx.delta_time   = parent_ctx->delta_time;
            audio_ctx.frame        = parent_ctx->frame;
            audio_ctx.param_values = param_values_.data();
            audio_ctx.input_buffers  = audio_in_ptrs_.data();
            audio_ctx.output_buffers = audio_out_ptrs_.data();
            audio_ctx.buffer_size    = 1;
            audio_ctx.sample_rate    = (parent_ctx->delta_time > 0.0)
                ? static_cast<uint32_t>(1.0 / parent_ctx->delta_time)
                : 48000;
            audio_ctx.input_channel_counts  = nullptr;
            audio_ctx.output_channel_counts = nullptr;
            audio_ctx.input_spreads  = c_input_spreads_.empty() ? nullptr : c_input_spreads_.data();
            audio_ctx.output_spreads = c_output_spreads_.empty() ? nullptr : c_output_spreads_.data();
            audio_ctx.input_float_values  = input_values_.empty() ? nullptr : input_values_.data();
            audio_ctx.output_float_values = output_values_.empty() ? nullptr : output_values_.data();
            audio_ctx.custom_inputs       = nullptr;
            audio_ctx.custom_input_count  = 0;
            audio_ctx.custom_outputs      = nullptr;
            audio_ctx.custom_output_count = 0;
            audio_ctx.input_string_values = nullptr;
            audio_ctx.file_param_values   = nullptr;
            audio_ctx.file_param_count    = 0;
            audio_ctx.shared_handles      = nullptr;

            static_cast<AudioProcessable*>(op_.get())->process_audio(&audio_ctx);

            for (size_t i = 0; i < output_values_.size(); ++i)
                output_values_[i] = audio_out_bufs_[i][0];
        } else {
            VividFrameContext child_ctx{};
            child_ctx.time         = parent_ctx->time;
            child_ctx.delta_time   = parent_ctx->delta_time;
            child_ctx.frame        = parent_ctx->frame;
            child_ctx.param_values = param_values_.data();
            child_ctx.input_values = input_values_.empty() ? nullptr : input_values_.data();
            child_ctx.output_values = output_values_.empty() ? nullptr : output_values_.data();
            child_ctx.input_spreads  = c_input_spreads_.empty() ? nullptr : c_input_spreads_.data();
            child_ctx.output_spreads = c_output_spreads_.empty() ? nullptr : c_output_spreads_.data();
            child_ctx.file_param_values = nullptr;
            child_ctx.file_param_count  = 0;
            child_ctx.preferred_tex_width  = 0;
            child_ctx.preferred_tex_height = 0;

            static_cast<FrameProcessable*>(op_.get())->process_frame(&child_ctx);
        }

        for (size_t i = 0; i < output_spreads_.size(); ++i) {
            uint32_t len = c_output_spreads_[i].length;
            uint32_t cap = c_output_spreads_[i].capacity;
            if (len > cap) len = cap;
            output_spreads_[i].resize(len);
        }
    }

    // -- Introspection ------------------------------------------------------

    OperatorBase&       op()       { return *op_; }
    const OperatorBase& op() const { return *op_; }
    bool uses_audio_cadence() const { return uses_audio_cadence_; }
    uint32_t param_count()  const { return static_cast<uint32_t>(param_ptrs_.size()); }

private:
    using OwnedOp = std::unique_ptr<OperatorBase,
                                    std::function<void(OperatorBase*)>>;

    void init() {
        uses_audio_cadence_ = dynamic_cast<AudioProcessable*>(op_.get()) != nullptr;

        op_->collect_params(param_ptrs_);
        param_values_.resize(param_ptrs_.size());
        for (size_t i = 0; i < param_ptrs_.size(); ++i) {
            param_values_[i] = param_ptrs_[i]->default_value;
            param_name_to_index_[param_ptrs_[i]->name] = static_cast<uint32_t>(i);
        }

        std::vector<VividPortDescriptor> ports;
        op_->collect_ports(ports);
        uint32_t in_idx = 0, out_idx = 0;
        for (auto& port : ports) {
            if (port.direction == VIVID_PORT_INPUT)
                input_name_to_index_[port.name] = in_idx++;
            else
                output_name_to_index_[port.name] = out_idx++;
        }

        input_values_.resize(in_idx, 0.0f);
        output_values_.resize(out_idx, 0.0f);
        input_spreads_.resize(in_idx);
        output_spreads_.resize(out_idx);
        c_input_spreads_.resize(in_idx);
        c_output_spreads_.resize(out_idx);
    }

    OwnedOp op_;
    bool uses_audio_cadence_ = false;

    std::vector<ParamBase*> param_ptrs_;
    std::vector<float>      param_values_;
    std::unordered_map<std::string, uint32_t> param_name_to_index_;

    std::vector<float> input_values_;
    std::vector<float> output_values_;
    std::unordered_map<std::string, uint32_t> input_name_to_index_;
    std::unordered_map<std::string, uint32_t> output_name_to_index_;

    std::vector<std::vector<float>> input_spreads_;
    std::vector<std::vector<float>> output_spreads_;
    std::vector<VividSpreadPort> c_input_spreads_;
    std::vector<VividSpreadPort> c_output_spreads_;

    std::vector<std::vector<float>> audio_in_bufs_;
    std::vector<std::vector<float>> audio_out_bufs_;
    std::vector<float*> audio_in_ptrs_;
    std::vector<float*> audio_out_ptrs_;

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
