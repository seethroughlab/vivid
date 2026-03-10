#pragma once

#include "operator_api/operator.h"
#include <unordered_map>
#include <string>
#include <cassert>
#include <cstdio>

namespace vivid {

// ---------------------------------------------------------------------------
// ChildOp<T> — embed an operator as a persistent member variable
//
// Usage:
//   #include "operator_api/child_op.h"
//   #include "control/lfo/lfo.h"
//
//   struct MyOp : vivid::OperatorBase {
//       vivid::ChildOp<LFO> lfo;
//       void process(const VividProcessContext* ctx) override {
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

    void set_input_spread(uint32_t index, const float* data, uint32_t length) {
        assert(index < input_spreads_.size());
        auto& sp = input_spreads_[index];
        sp.resize(length);
        for (uint32_t i = 0; i < length; ++i)
            sp[i] = data[i];
    }

    void set_input_spread(const char* name, const float* data, uint32_t length) {
        auto it = input_name_to_index_.find(name);
        assert(it != input_name_to_index_.end());
        set_input_spread(it->second, data, length);
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

    const float* output_spread_data(uint32_t index) const {
        assert(index < output_spreads_.size());
        return output_spreads_[index].data();
    }

    uint32_t output_spread_length(uint32_t index) const {
        assert(index < output_spreads_.size());
        return static_cast<uint32_t>(output_spreads_[index].size());
    }

    const float* output_spread_data(const char* name) const {
        auto it = output_name_to_index_.find(name);
        assert(it != output_name_to_index_.end());
        return output_spread_data(it->second);
    }

    uint32_t output_spread_length(const char* name) const {
        auto it = output_name_to_index_.find(name);
        assert(it != output_name_to_index_.end());
        return output_spread_length(it->second);
    }

    // -- Process ------------------------------------------------------------

    void process(const VividProcessContext* parent_ctx) {
        // Sync param_values_ into child's Param<T>.value fields
        // (replicates what VIVID_REGISTER's vivid_process() does)
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
            // Pre-allocate output spread capacity
            if (output_spreads_[i].capacity() < 256)
                output_spreads_[i].reserve(256);
            c_output_spreads_[i].data     = output_spreads_[i].data();
            c_output_spreads_[i].length   = static_cast<uint32_t>(output_spreads_[i].size());
            c_output_spreads_[i].capacity = static_cast<uint32_t>(output_spreads_[i].capacity());
        }

        // Build child process context, inheriting time/frame from parent
        VividProcessContext child_ctx{};
        child_ctx.time         = parent_ctx->time;
        child_ctx.delta_time   = parent_ctx->delta_time;
        child_ctx.frame        = parent_ctx->frame;
        child_ctx.param_values = param_values_.data();
        child_ctx.input_values = input_values_.empty() ? nullptr : input_values_.data();
        child_ctx.output_values = output_values_.empty() ? nullptr : output_values_.data();
        child_ctx.input_spreads  = c_input_spreads_.empty() ? nullptr : c_input_spreads_.data();
        child_ctx.output_spreads = c_output_spreads_.empty() ? nullptr : c_output_spreads_.data();
        // File params are not supported for child operators — they are set via
        // main_thread_update, which ChildOp does not call.
        child_ctx.file_param_values = nullptr;
        child_ctx.file_param_count  = 0;
        child_ctx.preferred_tex_width  = 0;
        child_ctx.preferred_tex_height = 0;

        op_.process(&child_ctx);

        // Copy output spread lengths back (operator may have resized via length field)
        for (size_t i = 0; i < output_spreads_.size(); ++i) {
            uint32_t len = c_output_spreads_[i].length;
            uint32_t cap = c_output_spreads_[i].capacity;
            if (len > cap) {
                std::fprintf(stderr, "[vivid] ChildOp: output spread %zu wrote %u elements "
                             "but capacity was %u, clamping\n", i, len, cap);
                len = cap;
            }
            output_spreads_[i].resize(len);
        }
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
        input_spreads_.resize(in_idx);
        output_spreads_.resize(out_idx);
        c_input_spreads_.resize(in_idx);
        c_output_spreads_.resize(out_idx);
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
    std::vector<std::vector<float>> input_spreads_;
    std::vector<std::vector<float>> output_spreads_;
    std::vector<VividSpreadPort> c_input_spreads_;
    std::vector<VividSpreadPort> c_output_spreads_;
};

} // namespace vivid
