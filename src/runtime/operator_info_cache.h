#pragma once

#include "runtime/operator_registry.h"
#include "runtime/operator_loader.h"
#include "ui/graph_snapshot.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <memory>

class OperatorInfoCache {
public:
    std::shared_ptr<const vivid::ui::OperatorInfo> get(
            const std::string& type_name, vivid::OperatorRegistry& registry,
            vivid::OperatorLoader* fallback_loader = nullptr) {
        auto it = cache_.find(type_name);
        if (it != cache_.end()) {
            // Lazy upgrade: if inspector info was missing because the operator
            // wasn't fully loaded when first cached, re-check now.
            if (!it->second->has_custom_inspector) {
                auto* loader = registry.find_loaded(type_name);
                if (!loader && fallback_loader) loader = fallback_loader;
                if (loader && loader->has_draw_inspector()) {
                    auto upgraded = std::make_shared<vivid::ui::OperatorInfo>(*it->second);
                    upgraded->has_custom_inspector = true;
                    upgraded->inspector_mode = loader->inspector_mode();
                    cache_[type_name] = upgraded;
                    return upgraded;
                }
            }
            return it->second;
        }

        // Try fully-loaded first (without triggering lazy load)
        const VividOperatorDescriptor* desc = nullptr;
        auto* loader = registry.find_loaded(type_name);
        if (loader) {
            desc = loader->descriptor();
        } else {
            desc = registry.probe_descriptor(type_name);  // deferred metadata
        }
        // Fall back to per-instance loader (e.g. WGSLFilter nodes)
        if (!desc && fallback_loader) {
            loader = fallback_loader;
            desc = fallback_loader->descriptor();
        }
        if (!desc) return nullptr;

        auto info = std::make_shared<vivid::ui::OperatorInfo>();
        info->name = desc->name;
        info->is_gpu = (desc->execution_env == VIVID_ENV_GPU);
        info->is_audio_native = (desc->execution_env == VIVID_ENV_AUDIO);
        info->params.resize(desc->param_count);
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            auto& pi = info->params[i];
            const auto& pd = desc->params[i];
            pi.name = pd.name;
            pi.type = pd.type;
            pi.default_value = pd.default_value;
            pi.min_value = pd.min_value;
            pi.max_value = pd.max_value;
            pi.choice_count = pd.choice_count;
            pi.group               = pd.group ? pd.group : "";
            pi.display_hint        = pd.display_hint;
            pi.layout_columns      = pd.layout_columns;
            pi.layout_column_index = pd.layout_column_index;
            pi.semantic_tag        = pd.semantic_tag ? pd.semantic_tag : "";
            pi.semantic_shape      = pd.semantic_shape ? pd.semantic_shape : "";
            pi.semantic_unit       = pd.semantic_unit ? pd.semantic_unit : "";
            pi.semantic_intent     = pd.semantic_intent ? pd.semantic_intent : "";
            if (pd.choice_labels && pd.choice_count > 0) {
                pi.choice_labels.reserve(pd.choice_count);
                for (uint32_t ci = 0; ci < pd.choice_count; ++ci)
                    pi.choice_labels.push_back(pd.choice_labels[ci]);
            }
        }

        info->ports.resize(desc->port_count);
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            auto& pi = info->ports[i];
            pi.name = desc->ports[i].name;
            pi.type = desc->ports[i].type;
            pi.direction = desc->ports[i].direction;
        }

        // Only check shader/user status for fully-loaded operators
        if (loader) {
            if (loader->is_data_driven()) {
                info->has_shader = true;
            } else if (!operators_dir_.empty()) {
                std::string stem = type_name;
                for (auto& c : stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                std::string wgsl_path = operators_dir_ + "/gpu/" + stem + "/" + stem + ".wgsl";
                info->has_shader = std::filesystem::exists(wgsl_path);
            }
            info->is_user = registry.is_user_filter(type_name) || registry.is_user_operator(type_name);
            info->has_custom_inspector = loader->has_draw_inspector();
            info->inspector_mode = loader->has_draw_inspector()
                                   ? loader->inspector_mode() : 0;
        }

        cache_[type_name] = info;
        return info;
    }

    void invalidate(const std::string& type_name) { cache_.erase(type_name); }
    void invalidate_all() { cache_.clear(); }
    void set_operators_dir(const std::string& dir) { operators_dir_ = dir; }

private:
    std::unordered_map<std::string, std::shared_ptr<const vivid::ui::OperatorInfo>> cache_;
    std::string operators_dir_;
};
