#pragma once

#include "operator_api/wgsl_filter.h"
#include <memory>
#include <string>
#include <vector>

namespace vivid {

// ---------------------------------------------------------------------------
// DataDrivenFilterConfig — everything needed to construct a user filter
// ---------------------------------------------------------------------------

struct DataDrivenFilterConfig {
    struct ParamDef {
        std::string name;
        VividParamType type = VIVID_PARAM_FLOAT;
        float default_value = 0.0f;
        float min_value = 0.0f;
        float max_value = 1.0f;
    };

    std::string name;
    std::string shader_path;       // absolute path to working .wgsl file
    std::string source_builtin;    // which built-in it was copied from
    bool time_dependent = false;
    std::vector<ParamDef> params;
};

// ---------------------------------------------------------------------------
// DataDrivenFilter — runtime instance of a user-defined WGSL filter
// ---------------------------------------------------------------------------

class DataDrivenFilter : public WgslFilterBase {
public:
    explicit DataDrivenFilter(std::shared_ptr<DataDrivenFilterConfig> config)
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
            params_.push_back(p);
        }
    }

    void collect_params(std::vector<ParamBase*>& out) override {
        for (auto& p : params_) out.push_back(&p);
    }

    // Default ports from WgslFilterBase (1 texture in + 1 texture out)

private:
    std::shared_ptr<DataDrivenFilterConfig> config_;
    std::vector<std::string> param_names_;   // stable storage for const char* ptrs
    std::vector<ParamBase> params_;
};

} // namespace vivid
