#pragma once

#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_loader.h"
#include "ui/graph/graph_snapshot.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <memory>

inline vivid::ui::ParamVisibilityCondition resolve_param_visibility(
        const VividParamDescriptor& pd,
        const std::vector<vivid::ui::ParamInfo>& params)
{
    vivid::ui::ParamVisibilityCondition cond;
    if (pd.visible_when_op == VIVID_PARAM_VIS_ALWAYS || !pd.visible_when_param ||
        !*pd.visible_when_param) {
        return cond;
    }
    if (pd.visible_when_op != VIVID_PARAM_VIS_EQ &&
        pd.visible_when_op != VIVID_PARAM_VIS_NE) {
        std::fprintf(stderr,
                     "[vivid] invalid visible_when op %u on param '%s'; showing param\n",
                     pd.visible_when_op, pd.name ? pd.name : "(unnamed)");
        return cond;
    }
    if (!pd.visible_when_values || pd.visible_when_value_count == 0) {
        std::fprintf(stderr,
                     "[vivid] empty visible_when values on param '%s'; showing param\n",
                     pd.name ? pd.name : "(unnamed)");
        return cond;
    }

    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i].name == pd.visible_when_param) {
            cond.param_index = static_cast<int32_t>(i);
            break;
        }
    }
    if (cond.param_index < 0) {
        std::fprintf(stderr,
                     "[vivid] visible_when controller '%s' not found for param '%s'; showing param\n",
                     pd.visible_when_param, pd.name ? pd.name : "(unnamed)");
        return {};
    }

    cond.op = pd.visible_when_op;
    cond.values.assign(pd.visible_when_values,
                       pd.visible_when_values + pd.visible_when_value_count);
    return cond;
}

class OperatorInfoCache {
public:
    std::shared_ptr<const vivid::ui::OperatorInfo> get(
            const std::string& type_name, vivid::OperatorRegistry& registry,
            vivid::OperatorLoader* fallback_loader = nullptr) {
        auto it = cache_.find(type_name);
        if (it != cache_.end()) {
            // Lazy upgrade: if inspector or editor info was missing because the
            // operator wasn't fully loaded when first cached, re-check now.
            if (!it->second->has_custom_inspector || !it->second->has_editor) {
                auto* loader = registry.find_loaded(type_name);
                if (!loader && fallback_loader) loader = fallback_loader;
                if (loader) {
                    const bool needs_inspector_upgrade =
                        !it->second->has_custom_inspector && loader->has_draw_inspector();
                    const bool needs_editor_upgrade =
                        !it->second->has_editor && loader->has_editor();
                    if (needs_inspector_upgrade || needs_editor_upgrade) {
                        auto upgraded = std::make_shared<vivid::ui::OperatorInfo>(*it->second);
                        if (needs_inspector_upgrade) {
                            upgraded->has_custom_inspector = true;
                            upgraded->inspector_mode = loader->inspector_mode();
                        }
                        if (needs_editor_upgrade) {
                            upgraded->has_editor = true;
                        }
                        cache_[type_name] = upgraded;
                        return upgraded;
                    }
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
        // Fall back to a per-instance loader when one is explicitly supplied.
        if (!desc && fallback_loader) {
            loader = fallback_loader;
            desc = fallback_loader->descriptor();
        }
        if (!desc) return nullptr;

        auto info = std::make_shared<vivid::ui::OperatorInfo>();
        info->name = desc->name ? desc->name : "";
        info->is_gpu = (desc->has_process_gpu != 0);
        info->params.resize(desc->param_count);
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            auto& pi = info->params[i];
            const auto& pd = desc->params[i];
            pi.name = pd.name ? pd.name : "";
            pi.type = pd.type;
            pi.default_value = pd.default_value;
            pi.default_string = pd.default_string ? pd.default_string : "";
            pi.min_value = pd.min_value;
            pi.max_value = pd.max_value;
            pi.choice_count = pd.choice_count;
            pi.group               = pd.group ? pd.group : "";
            pi.display_hint        = pd.display_hint;
            pi.layout_columns      = pd.layout_columns;
            pi.layout_column_index = pd.layout_column_index;
            pi.widget_id           = pd.widget_id ? pd.widget_id : "";
            pi.widget_span         = pd.widget_span;
            pi.semantic_tag        = pd.semantic_tag ? pd.semantic_tag : "";
            pi.semantic_shape      = pd.semantic_shape ? pd.semantic_shape : "";
            pi.semantic_unit       = pd.semantic_unit ? pd.semantic_unit : "";
            pi.semantic_intent     = pd.semantic_intent ? pd.semantic_intent : "";
            pi.description         = pd.description ? pd.description : "";
            pi.asset_kind          = pd.asset_kind ? pd.asset_kind : "";
            pi.visible_when_param  = pd.visible_when_param ? pd.visible_when_param : "";
            pi.visible_when_op     = pd.visible_when_op;
            if (pd.visible_when_values && pd.visible_when_value_count > 0) {
                pi.visible_when_values.assign(
                    pd.visible_when_values,
                    pd.visible_when_values + pd.visible_when_value_count);
            }
            if (pd.choice_labels && pd.choice_count > 0) {
                pi.choice_labels.reserve(pd.choice_count);
                for (uint32_t ci = 0; ci < pd.choice_count; ++ci)
                    pi.choice_labels.push_back(pd.choice_labels[ci] ? pd.choice_labels[ci] : "");
            }
            pi.repeat_group     = pd.repeat_group ? pd.repeat_group : "";
            pi.repeat_group_idx = pd.repeat_group_idx;
        }
        // Resolve visible_when conditions (needs all params built for name->index lookup)
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            info->params[i].visibility = resolve_param_visibility(desc->params[i], info->params);
        }

        info->ports.resize(desc->port_count);
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            auto& pi = info->ports[i];
            pi.name = desc->ports[i].name ? desc->ports[i].name : "";
            pi.type = desc->ports[i].type;
            pi.direction = desc->ports[i].direction;
            pi.repeat_group     = desc->ports[i].repeat_group ? desc->ports[i].repeat_group : "";
            pi.repeat_group_idx = desc->ports[i].repeat_group_idx;
            pi.description      = desc->ports[i].description ? desc->ports[i].description : "";
        }

        // Only check shader/user status for fully-loaded operators
        if (loader) {
            if (loader->is_shader_operator()) {
                info->has_shader = true;
            } else if (!operators_dir_.empty()) {
                std::string stem = type_name;
                for (auto& c : stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                std::string wgsl_path = operators_dir_ + "/gpu/" + stem + "/" + stem + ".wgsl";
                info->has_shader = std::filesystem::exists(wgsl_path);
            }
            info->is_user = registry.is_user_shader_operator(type_name) || registry.is_user_operator(type_name);
            info->has_custom_inspector = loader->has_draw_inspector();
            info->inspector_mode = loader->has_draw_inspector()
                                   ? loader->inspector_mode() : 0;
            info->has_editor = loader->has_editor();
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
