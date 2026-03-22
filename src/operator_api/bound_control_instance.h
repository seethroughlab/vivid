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
// BoundControlInstance — runtime-polymorphic bound operator
//
// Supports both ControlOperatorBase (scalar process()) and AudioOperatorBase
// (buffer process_audio(), single-sample per control frame). Dispatches at
// runtime based on the concrete type.
//
// Usage:
//   #include "operator_api/bound_control_instance.h"
//
//   auto env = std::make_unique<Envelope>();
//   BoundControlInstance slot(std::move(env));
//   slot.set_param("attack", 0.1f);
//   slot.process(parent_ctx);
//   float val = slot.output("value");
// ---------------------------------------------------------------------------

class BoundControlInstance {
public:
    // Type-erased owned operator pointer (supports custom deleters).
    using OwnedOp = std::unique_ptr<OperatorBase,
                                    std::function<void(OperatorBase*)>>;

    // Primary: takes ownership of an already-constructed operator (default delete).
    // Accepts unique_ptr to any OperatorBase subclass (ControlOperatorBase, AudioOperatorBase, etc.)
    template<typename T, typename D,
             typename = std::enable_if_t<std::is_base_of_v<OperatorBase, T>>>
    explicit BoundControlInstance(std::unique_ptr<T, D> op)
        : op_(op.release(), std::default_delete<OperatorBase>{})
    {
        init();
    }

    // With factory: enables reset() to recreate the operator (default delete)
    BoundControlInstance(std::unique_ptr<OperatorBase> op,
                            std::function<std::unique_ptr<OperatorBase>()> factory)
        : op_(op.release(), std::default_delete<OperatorBase>{})
        , factory_([f = std::move(factory)]() -> OwnedOp {
            auto ptr = f();
            return OwnedOp(ptr.release(), std::default_delete<OperatorBase>{});
          })
    {
        init();
    }

    // Custom destroy: for registry-created instances with paired destroy functions
    BoundControlInstance(OperatorBase* raw,
                            std::function<void(OperatorBase*)> destroy_fn)
        : op_(raw, std::move(destroy_fn))
    {
        init();
    }

    // Custom destroy + factory: factory returns raw, same destroy applies
    BoundControlInstance(OperatorBase* raw,
                            std::function<void(OperatorBase*)> destroy_fn,
                            std::function<OperatorBase*()> create_fn)
        : op_(raw, destroy_fn)
        , factory_([create = std::move(create_fn), d = destroy_fn]() -> OwnedOp {
            return OwnedOp(create(), d);
          })
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

    bool has_param(const char* name) const {
        return param_name_to_index_.count(name) > 0;
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

    bool has_input(const char* name) const {
        return input_name_to_index_.count(name) > 0;
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

    bool has_output(const char* name) const {
        return output_name_to_index_.count(name) > 0;
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

        if (is_audio_op_) {
            // AudioOperatorBase: process single sample via process_audio()
            ensure_audio_buffers_();

            VividAudioContext audio_ctx{};
            audio_ctx.time         = parent_ctx->time;
            audio_ctx.delta_time   = parent_ctx->delta_time;
            audio_ctx.frame        = parent_ctx->frame;
            audio_ctx.param_values = param_values_.data();
            audio_ctx.input_buffers  = audio_in_ptrs_.data();
            audio_ctx.output_buffers = audio_out_ptrs_.data();
            audio_ctx.buffer_size    = 1;
            // Set sample_rate so 1/sample_rate ≈ delta_time, preserving timing semantics
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
            audio_ctx.role_binding_count   = 0;
            audio_ctx.role_binding_configs = nullptr;

            static_cast<AudioOperatorBase*>(op_.get())->process_audio(&audio_ctx);

            // Extract output from single-sample buffers
            for (size_t i = 0; i < output_values_.size(); ++i) {
                output_values_[i] = audio_out_bufs_[i][0];
            }
        } else {
            // ControlOperatorBase: standard VividProcessContext dispatch
            VividProcessContext child_ctx{};
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

            static_cast<ControlOperatorBase*>(op_.get())->process(&child_ctx);
        }

        // Copy output spread lengths back
        for (size_t i = 0; i < output_spreads_.size(); ++i) {
            uint32_t len = c_output_spreads_[i].length;
            uint32_t cap = c_output_spreads_[i].capacity;
            if (len > cap) {
                std::fprintf(stderr, "[vivid] BoundControlInstance: output spread %zu wrote %u "
                             "elements but capacity was %u, clamping\n", i, len, cap);
                len = cap;
            }
            output_spreads_[i].resize(len);
        }
    }

    // -- Template application -----------------------------------------------

    void apply_template(const std::unordered_map<std::string, float>& params) {
        template_params_ = params;
        for (const auto& [name, value] : params) {
            auto it = param_name_to_index_.find(name);
            if (it != param_name_to_index_.end()) {
                param_values_[it->second] = value;
            }
        }
    }

    // -- Reset --------------------------------------------------------------

    void reset() {
        if (factory_) {
            // Recreate operator from factory (old op_ destroyed via its deleter)
            op_ = factory_();
            reinit();
            // Re-apply template params
            for (const auto& [name, value] : template_params_) {
                auto it = param_name_to_index_.find(name);
                if (it != param_name_to_index_.end()) {
                    param_values_[it->second] = value;
                }
            }
        } else {
            // No factory — zero all values and re-init from existing op's defaults
            for (size_t i = 0; i < param_values_.size(); ++i)
                param_values_[i] = param_ptrs_[i]->default_value;
            for (auto& v : input_values_) v = 0.0f;
            for (auto& v : output_values_) v = 0.0f;
            for (auto& sp : input_spreads_) sp.clear();
            for (auto& sp : output_spreads_) sp.clear();
        }
    }

    // -- Direct access to underlying operator instance ----------------------

    OperatorBase&       op()       { return *op_; }
    const OperatorBase& op() const { return *op_; }

    bool is_audio_operator() const { return is_audio_op_; }

    // -- Introspection ------------------------------------------------------

    uint32_t param_count()  const { return static_cast<uint32_t>(param_ptrs_.size()); }
    uint32_t input_count()  const { return static_cast<uint32_t>(input_values_.size()); }
    uint32_t output_count() const { return static_cast<uint32_t>(output_values_.size()); }

private:
    void init() {
        // Detect operator domain
        is_audio_op_ = dynamic_cast<AudioOperatorBase*>(op_.get()) != nullptr;

        // Collect param metadata
        op_->collect_params(param_ptrs_);
        param_values_.resize(param_ptrs_.size());
        for (size_t i = 0; i < param_ptrs_.size(); ++i) {
            param_values_[i] = param_ptrs_[i]->default_value;
            param_name_to_index_[param_ptrs_[i]->name] = static_cast<uint32_t>(i);
        }

        // Collect port metadata and split into input/output arrays
        std::vector<VividPortDescriptor> ports;
        op_->collect_ports(ports);

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

    void reinit() {
        param_ptrs_.clear();
        param_values_.clear();
        param_name_to_index_.clear();
        input_values_.clear();
        output_values_.clear();
        input_name_to_index_.clear();
        output_name_to_index_.clear();
        input_spreads_.clear();
        output_spreads_.clear();
        c_input_spreads_.clear();
        c_output_spreads_.clear();
        init();
    }

    OwnedOp op_;
    std::function<OwnedOp()> factory_;
    std::unordered_map<std::string, float> template_params_;
    bool is_audio_op_ = false;

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

    // Audio buffer storage (only used for AudioOperatorBase instances)
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
