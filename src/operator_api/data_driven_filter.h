#pragma once

#include "operator_api/wgsl_filter.h"
#include <memory>
#include <string>
#include <vector>

namespace vivid {

// ---------------------------------------------------------------------------
// WgslOperatorConfig — everything needed to construct a shader-backed operator
// ---------------------------------------------------------------------------

struct WgslOperatorConfig {
    struct ParamDef {
        std::string name;
        VividParamType type = VIVID_PARAM_FLOAT;
        float default_value = 0.0f;
        float min_value = 0.0f;
        float max_value = 1.0f;
        std::string label;                        // display label (empty = use name)
        std::vector<std::string> choices;         // for int enums
        VividDisplayHint display_hint = VIVID_DISPLAY_DEFAULT;
        std::string group;                        // collapsible group name
        uint8_t layout_columns = 0;
        uint8_t layout_column_index = 0;
    };

    struct InputDef {
        std::string name;
    };

    std::string name;
    std::string shader_path;       // absolute path to working .wgsl file
    std::string source_builtin;    // which built-in it was copied from
    bool time_dependent = false;
    std::vector<ParamDef> params;
    std::vector<InputDef> inputs;
    bool inputs_specified = false;  // false = default 1-in/1-out
};

// ---------------------------------------------------------------------------
// WgslOperator — runtime instance of a shader-backed operator
// ---------------------------------------------------------------------------

class WgslOperator : public WgslFilterBase {
public:
    explicit WgslOperator(std::shared_ptr<WgslOperatorConfig> config)
        : WgslFilterBase("data_driven.wgsl")  // dummy filename, overridden below
        , config_(std::move(config))
    {
        set_shader_path_override(config_->shader_path);

        // Build dynamic param storage with stable string names
        param_names_.reserve(config_->params.size());
        params_.reserve(config_->params.size());
        for (const auto& pd : config_->params) {
            param_names_.push_back(pd.name);
        }
        for (size_t i = 0; i < config_->params.size(); ++i) {
            const auto& pd = config_->params[i];
            ParamBase p{};
            p.name = param_names_[i].c_str();
            p.type = pd.type;
            p.default_value = pd.default_value;
            p.min_value = pd.min_value;
            p.max_value = pd.max_value;
            p.value = pd.default_value;
            p.display_hint = pd.display_hint;
            p.layout_columns = pd.layout_columns;
            p.layout_column_index = pd.layout_column_index;
            params_.push_back(p);
        }

        // Build stable storage for group and choice pointers
        group_strings_.reserve(config_->params.size());
        choice_labels_.resize(config_->params.size());
        choice_ptrs_.resize(config_->params.size());
        for (size_t i = 0; i < config_->params.size(); ++i) {
            const auto& pd = config_->params[i];
            group_strings_.push_back(pd.group);
            if (!pd.group.empty())
                params_[i].group = group_strings_.back().c_str();

            if (!pd.choices.empty()) {
                auto& labels = choice_labels_[i];
                auto& ptrs = choice_ptrs_[i];
                labels = pd.choices;
                ptrs.resize(labels.size());
                for (size_t j = 0; j < labels.size(); ++j)
                    ptrs[j] = labels[j].c_str();
                params_[i].choice_labels = ptrs.data();
                params_[i].choice_count = static_cast<uint32_t>(ptrs.size());
            }
        }

        // Stable storage for input port names
        if (config_->inputs_specified) {
            port_names_.reserve(config_->inputs.size());
            for (const auto& inp : config_->inputs)
                port_names_.push_back(inp.name);
        }
    }

    void collect_params(std::vector<ParamBase*>& out) override {
        for (auto& p : params_) out.push_back(&p);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        if (config_->inputs_specified) {
            for (size_t i = 0; i < port_names_.size(); ++i)
                out.push_back({port_names_[i].c_str(), VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
            out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
        } else {
            // Default: 1 input + 1 output (same as WgslFilterBase)
            WgslFilterBase::collect_ports(out);
        }
    }

private:
    std::shared_ptr<WgslOperatorConfig> config_;
    std::vector<std::string> param_names_;   // stable storage for const char* ptrs
    std::vector<ParamBase> params_;
    std::vector<std::string> group_strings_;
    std::vector<std::vector<std::string>> choice_labels_;
    std::vector<std::vector<const char*>> choice_ptrs_;
    std::vector<std::string> port_names_;    // stable storage for input port names
};

} // namespace vivid
