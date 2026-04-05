#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_registry_internal.h"
#include "runtime/platform/platform.h"
#include "runtime/gpu/wgsl_header_parser.h"
#include "operator_api/operator.h"
#include "operator_api/data_driven_filter.h"

#include <nlohmann/json.hpp>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>

namespace vivid {

namespace {

static uint32_t runtime_abi_override() {
    const char* env = std::getenv("VIVID_MOCK_RUNTIME_ABI");
    if (!env || !*env) return VIVID_OPERATOR_ABI_VERSION;
    char* end = nullptr;
    unsigned long v = std::strtoul(env, &end, 10);
    if (end == env) return VIVID_OPERATOR_ABI_VERSION;
    return static_cast<uint32_t>(v);
}

static std::shared_ptr<WgslOperatorConfig> make_wgsl_operator_config(
        const std::string& path,
        const WgslHeader& header) {
    auto config = std::make_shared<WgslOperatorConfig>();
    config->name = header.name;
    config->shader_path = path;
    config->time_dependent = header.time_dependent;
    config->inputs_specified = header.inputs_specified;
    for (const auto& inp : header.inputs)
        config->inputs.push_back({inp.name});
    for (const auto& hp : header.params) {
        WgslOperatorConfig::ParamDef pd;
        pd.name = hp.name;
        pd.type = hp.type;
        pd.default_value = hp.default_value;
        pd.min_value = hp.min_value;
        pd.max_value = hp.max_value;
        pd.label = hp.label;
        pd.choices = hp.choices;
        pd.display_hint = hp.display_hint;
        pd.group = hp.group;
        pd.layout_columns = hp.layout_columns;
        pd.layout_column_index = hp.layout_column_index;
        pd.asset_kind = hp.asset_kind;
        config->params.push_back(std::move(pd));
    }
    return config;
}

static std::optional<DeferredEntry> deep_copy_descriptor(
        const VividOperatorDescriptor* src,
        const VividFileDropHandlerDescriptor* file_drop_src,
        uint32_t file_drop_count,
        const std::string& dylib_path) {
    DeferredEntry entry;
    entry.dylib_path = dylib_path;

    if (src->param_count > 256) {
        std::fprintf(stderr, "[vivid] descriptor for '%s' claims %u params — clamping to 256\n",
                     src->name ? src->name : "(null)", src->param_count);
    }
    const uint32_t param_count = std::min(src->param_count, 256u);
    if (param_count > 0 && !src->params) {
        std::fprintf(stderr,
                     "[vivid] descriptor for '%s' has param_count=%u but null params ptr — skipping\n",
                     src->name ? src->name : "(null)", param_count);
        return std::nullopt;
    }

    if (src->port_count > 64) {
        std::fprintf(stderr, "[vivid] descriptor for '%s' claims %u ports — clamping to 64\n",
                     src->name ? src->name : "(null)", src->port_count);
    }
    const uint32_t port_count = std::min(src->port_count, 64u);
    if (port_count > 0 && !src->ports) {
        std::fprintf(stderr,
                     "[vivid] descriptor for '%s' has port_count=%u but null ports ptr — skipping\n",
                     src->name ? src->name : "(null)", port_count);
        return std::nullopt;
    }

    entry.params.resize(param_count);
    entry.param_names.resize(param_count);
    entry.default_strings.resize(param_count);
    entry.semantic_tags.resize(param_count);
    entry.semantic_shapes.resize(param_count);
    entry.semantic_units.resize(param_count);
    entry.semantic_intents.resize(param_count);
    entry.descriptions.resize(param_count);
    entry.asset_kinds.resize(param_count);
    entry.choice_labels.resize(param_count);
    entry.choice_label_ptrs.resize(param_count);
    for (uint32_t i = 0; i < param_count; ++i) {
        const auto& sp = src->params[i];
        auto& dp = entry.params[i];
        entry.param_names[i] = sp.name ? sp.name : "";
        dp.name = entry.param_names[i].c_str();
        dp.type = sp.type;
        dp.default_value = sp.default_value;
        dp.min_value = sp.min_value;
        dp.max_value = sp.max_value;
        dp.choice_count = sp.choice_count;
        dp.group = sp.group;
        dp.display_hint = sp.display_hint;
        dp.layout_columns = sp.layout_columns;
        dp.layout_column_index = sp.layout_column_index;

        if (sp.choice_labels && sp.choice_count > 0) {
            entry.choice_labels[i].resize(sp.choice_count);
            entry.choice_label_ptrs[i].resize(sp.choice_count);
            for (uint32_t ci = 0; ci < sp.choice_count; ++ci) {
                entry.choice_labels[i][ci] = sp.choice_labels[ci] ? sp.choice_labels[ci] : "";
                entry.choice_label_ptrs[i][ci] = entry.choice_labels[i][ci].c_str();
            }
            dp.choice_labels = entry.choice_label_ptrs[i].data();
        } else {
            dp.choice_labels = nullptr;
        }

        if (sp.default_string) {
            entry.default_strings[i] = sp.default_string;
            dp.default_string = entry.default_strings[i].c_str();
        } else {
            dp.default_string = nullptr;
        }

        if (sp.semantic_tag) {
            entry.semantic_tags[i] = sp.semantic_tag;
            dp.semantic_tag = entry.semantic_tags[i].c_str();
        } else {
            dp.semantic_tag = nullptr;
        }
        if (sp.semantic_shape) {
            entry.semantic_shapes[i] = sp.semantic_shape;
            dp.semantic_shape = entry.semantic_shapes[i].c_str();
        } else {
            dp.semantic_shape = nullptr;
        }
        if (sp.semantic_unit) {
            entry.semantic_units[i] = sp.semantic_unit;
            dp.semantic_unit = entry.semantic_units[i].c_str();
        } else {
            dp.semantic_unit = nullptr;
        }
        if (sp.semantic_intent) {
            entry.semantic_intents[i] = sp.semantic_intent;
            dp.semantic_intent = entry.semantic_intents[i].c_str();
        } else {
            dp.semantic_intent = nullptr;
        }
        if (sp.description) {
            entry.descriptions[i] = sp.description;
            dp.description = entry.descriptions[i].c_str();
        } else {
            dp.description = nullptr;
        }
        if (sp.asset_kind) {
            entry.asset_kinds[i] = sp.asset_kind;
            dp.asset_kind = entry.asset_kinds[i].c_str();
        } else {
            dp.asset_kind = nullptr;
        }
    }

    entry.ports.resize(port_count);
    entry.port_names.resize(port_count);
    entry.port_type_names.resize(port_count);
    entry.port_stable_type_ids.resize(port_count);
    entry.port_semantic_tags.resize(port_count);
    entry.port_semantic_shapes.resize(port_count);
    entry.port_semantic_intents.resize(port_count);
    entry.port_descriptions.resize(port_count);
    for (uint32_t i = 0; i < port_count; ++i) {
        const auto& sp = src->ports[i];
        auto& dp = entry.ports[i];
        entry.port_names[i] = sp.name ? sp.name : "";
        dp.name = entry.port_names[i].c_str();
        dp.type = sp.type;
        dp.direction = sp.direction;
        dp.transport = sp.transport;
        dp.payload_size = sp.payload_size;
        dp.channels = sp.channels;
        dp.default_value = sp.default_value;
        entry.port_type_names[i] = sp.type_name ? sp.type_name : "";
        dp.type_name = entry.port_type_names[i].empty() ? nullptr : entry.port_type_names[i].c_str();
        entry.port_stable_type_ids[i] = sp.stable_type_id ? sp.stable_type_id : "";
        dp.stable_type_id = entry.port_stable_type_ids[i].empty() ? nullptr : entry.port_stable_type_ids[i].c_str();
        if (sp.semantic_tag) {
            entry.port_semantic_tags[i] = sp.semantic_tag;
            dp.semantic_tag = entry.port_semantic_tags[i].c_str();
        } else {
            dp.semantic_tag = nullptr;
        }
        if (sp.semantic_shape) {
            entry.port_semantic_shapes[i] = sp.semantic_shape;
            dp.semantic_shape = entry.port_semantic_shapes[i].c_str();
        } else {
            dp.semantic_shape = nullptr;
        }
        if (sp.semantic_intent) {
            entry.port_semantic_intents[i] = sp.semantic_intent;
            dp.semantic_intent = entry.port_semantic_intents[i].c_str();
        } else {
            dp.semantic_intent = nullptr;
        }
        if (sp.description) {
            entry.port_descriptions[i] = sp.description;
            dp.description = entry.port_descriptions[i].c_str();
        } else {
            dp.description = nullptr;
        }
    }

    entry.desc.name = nullptr;
    entry.desc.param_count = param_count;
    entry.desc.params = entry.params.empty() ? nullptr : entry.params.data();
    entry.desc.port_count = port_count;
    entry.desc.ports = entry.ports.empty() ? nullptr : entry.ports.data();
    entry.desc.time_dependent = src->time_dependent;
    entry.desc.has_process_audio = src->has_process_audio;
    entry.desc.has_process_gpu = src->has_process_gpu;
    entry.desc.has_process_frame = src->has_process_frame;
    entry.desc.lane_behavior = src->lane_behavior;
    entry.desc.strategy_independent = src->strategy_independent;

    entry.file_drop_handlers.resize(file_drop_count);
    entry.file_drop_labels.resize(file_drop_count);
    entry.file_drop_file_params.resize(file_drop_count);
    entry.file_drop_descriptions.resize(file_drop_count);
    entry.file_drop_extensions.resize(file_drop_count);
    entry.file_drop_extension_ptrs.resize(file_drop_count);
    for (uint32_t i = 0; i < file_drop_count; ++i) {
        const auto& src_handler = file_drop_src[i];
        auto& dst_handler = entry.file_drop_handlers[i];
        entry.file_drop_labels[i] = src_handler.label ? src_handler.label : "";
        dst_handler.label = entry.file_drop_labels[i].empty()
                            ? nullptr : entry.file_drop_labels[i].c_str();
        entry.file_drop_file_params[i] = src_handler.file_param ? src_handler.file_param : "";
        dst_handler.file_param = entry.file_drop_file_params[i].empty()
                                 ? nullptr : entry.file_drop_file_params[i].c_str();
        entry.file_drop_descriptions[i] = src_handler.description ? src_handler.description : "";
        dst_handler.description = entry.file_drop_descriptions[i].empty()
                                  ? nullptr : entry.file_drop_descriptions[i].c_str();
        dst_handler.priority = src_handler.priority;
        dst_handler.extension_count = src_handler.extension_count;
        if (src_handler.extensions && src_handler.extension_count > 0) {
            entry.file_drop_extensions[i].reserve(src_handler.extension_count);
            entry.file_drop_extension_ptrs[i].reserve(src_handler.extension_count);
            for (uint32_t ei = 0; ei < src_handler.extension_count; ++ei) {
                std::string ext = src_handler.extensions[ei] ? src_handler.extensions[ei] : "";
                entry.file_drop_extensions[i].push_back(std::move(ext));
            }
            for (auto& ext : entry.file_drop_extensions[i]) {
                ext = operator_registry_internal::normalized_extension(ext);
                entry.file_drop_extension_ptrs[i].push_back(ext.c_str());
            }
            dst_handler.extensions = entry.file_drop_extension_ptrs[i].data();
        } else {
            dst_handler.extensions = nullptr;
            dst_handler.extension_count = 0;
        }
    }

    return entry;
}

} // namespace

bool OperatorRegistry::scan(const char* directory) {
    return operator_registry_internal::scan_plugin_dir(directory, [&](const std::string& path, const char* name, size_t stem_len) {
        auto loader = std::make_unique<OperatorLoader>();
        if (!loader->load(path.c_str())) {
            record_loader_failure(path, name, loader->last_error());
            return;
        }

        const VividOperatorDescriptor* desc = loader->descriptor();
        if (!desc || !desc->name) return;
        loader_failure_by_path_.erase(path);

        std::string type_name = desc->name;
        std::fprintf(stderr, "[vivid] Registry: loaded %s from %s\n", type_name.c_str(), name);

        std::string target_name(name, stem_len);
        target_to_type_[target_name] = type_name;

        if (loaders_.count(type_name)) {
            std::fprintf(stderr,
                "[vivid] warning: operator type '%s' already registered; "
                "overwriting with %s. Check for duplicate kName across operators.\n",
                type_name.c_str(), name);
        }
        loaders_[type_name] = std::move(loader);
    });
}

bool OperatorRegistry::scan_deferred(const char* directory) {
    const bool trace_probe = std::getenv("VIVID_REGISTRY_TRACE") != nullptr;
    const uint32_t runtime_abi = runtime_abi_override();
    return operator_registry_internal::scan_plugin_dir(directory, [&](const std::string& path, const char* name, size_t /*stem_len*/) {
        auto probe_start = std::chrono::steady_clock::now();
        if (trace_probe) {
            std::fprintf(stderr, "[vivid] Registry: probing %s\n", name);
        }
        void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            const char* dl_err = dlerror();
            std::fprintf(stderr, "[vivid] probe dlopen failed: %s\n", dl_err ? dl_err : "unknown error");
            return;
        }

        auto desc_fn = reinterpret_cast<VividDescriptorFn>(dlsym(handle, "vivid_descriptor"));
        auto file_drop_fn = reinterpret_cast<VividFileDropDescriptorFn>(
            dlsym(handle, "vivid_file_drop_descriptor"));
        auto abi_fn = reinterpret_cast<VividAbiVersionFn>(dlsym(handle, "vivid_abi_version"));
        if (!abi_fn) {
            std::fprintf(stderr, "[vivid] probe: skipping %s (missing vivid_abi_version; stale/incompatible plugin)\n", name);
            deferred_probe_handles_.push_back({path, handle});
            return;
        }
        const uint32_t abi = abi_fn();
        if (abi != runtime_abi) {
            std::fprintf(stderr,
                         "[vivid] probe: ABI mismatch %s (ABI %u, expected %u) at %s\n",
                         name, abi, runtime_abi, path.c_str());
            AbiMismatchDiagnostic diag;
            diag.plugin_path = path;
            diag.plugin_name = name;
            diag.package_name = operator_registry_internal::guess_package_name_from_plugin_path(path);
            diag.plugin_abi = abi;
            diag.runtime_abi = runtime_abi;
            abi_mismatch_by_path_[path] = std::move(diag);
            for (auto& [tn, prov] : expected_operators_) {
                if (tn == name) {
                    prov.abi_mismatch = true;
                    prov.failure_detail = "ABI " + std::to_string(abi) +
                        " != runtime ABI " + std::to_string(runtime_abi) +
                        ". Rebuild package '" + prov.package_name + "'.";
                    break;
                }
            }
            deferred_probe_handles_.push_back({path, handle});
            return;
        }
        if (!desc_fn) {
            std::fprintf(stderr, "[vivid] probe: missing vivid_descriptor in %s\n", name);
            deferred_probe_handles_.push_back({path, handle});
            return;
        }

        const VividOperatorDescriptor* desc = desc_fn();
        if (!desc || !desc->name) {
            deferred_probe_handles_.push_back({path, handle});
            return;
        }
        abi_mismatch_by_path_.erase(path);

        std::string type_name = desc->name;
        if (loaders_.count(type_name)) {
            deferred_probe_handles_.push_back({path, handle});
            return;
        }

        uint32_t file_drop_count = 0;
        const VividFileDropHandlerDescriptor* file_drop_desc =
            file_drop_fn ? file_drop_fn(&file_drop_count) : nullptr;
        auto de_opt = deep_copy_descriptor(desc, file_drop_desc, file_drop_count, path);
        deferred_probe_handles_.push_back({path, handle});
        if (!de_opt) return;

        auto [it, inserted] = deferred_.emplace(type_name, std::move(*de_opt));
        if (!inserted) {
            std::fprintf(stderr,
                "[vivid] warning: deferred operator type '%s' already registered; "
                "ignoring %s. Check for duplicate kName across operators.\n",
                type_name.c_str(), name);
            return;
        }
        it->second.desc.name = it->first.c_str();

        register_target_mapping(path, type_name);

        if (trace_probe) {
            auto probe_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - probe_start).count();
            std::fprintf(stderr, "[vivid] Registry: probed %s from %s (%lldms)\n",
                         type_name.c_str(), name, probe_ms);
        } else {
            std::fprintf(stderr, "[vivid] Registry: probed %s from %s\n", type_name.c_str(), name);
        }

        if (progress_cb_) progress_cb_();
    });
}

bool OperatorRegistry::scan_shader_operators(const std::string& directory,
                                             bool mark_user,
                                             const std::string& package_name) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        return false;
    }

    clear_shader_operators_in_dir(directory);

    bool ok = true;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = std::strlen(name);
        if (len < 6 || std::strcmp(name + len - 5, ".wgsl") != 0)
            continue;

        std::string path = directory + "/" + name;

        std::ifstream ifs(path);
        if (!ifs) continue;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string contents = ss.str();

        std::string error;
        auto header = parse_wgsl_header(contents, error);
        if (!header) {
            std::fprintf(stderr, "[vivid] Skipping %s: %s\n", name, error.c_str());
            ok = false;
            continue;
        }

        const auto existing_source = shader_operator_sources_.find(header->name);
        if (existing_source != shader_operator_sources_.end() &&
            existing_source->second == path) {
            unregister_shader_operator(header->name);
        }

        if (loaders_.count(header->name) || deferred_.count(header->name) ||
            aliases_.count(header->name)) {
            std::fprintf(stderr,
                         "[vivid] Shader operator name collision for '%s' at %s\n",
                         header->name.c_str(), path.c_str());
            ok = false;
            continue;
        }

        register_shader_operator(make_wgsl_operator_config(path, *header), mark_user, package_name);
        std::fprintf(stderr, "[vivid] Registry: loaded shader operator %s from %s\n",
                     header->name.c_str(), name);
    }

    closedir(dir);
    return ok;
}

bool OperatorRegistry::scan_factory_presets(const std::string& directory) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = std::strlen(name);
        if (len < 6 || std::strcmp(name + len - 5, ".json") != 0)
            continue;

        std::string target(name, len - 5);
        auto tit = target_to_type_.find(target);
        if (tit == target_to_type_.end()) {
            std::fprintf(stderr, "[vivid] Factory presets: unknown target '%s', skipping\n",
                         target.c_str());
            continue;
        }
        const std::string& type_name = tit->second;

        std::string path = directory + "/" + name;
        std::ifstream ifs(path);
        if (!ifs) continue;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string contents = ss.str();

        nlohmann::json doc;
        try {
            doc = nlohmann::json::parse(contents);
        } catch (const nlohmann::json::parse_error&) {
            std::fprintf(stderr, "[vivid] Factory presets: failed to parse %s\n", name);
            continue;
        }

        auto presets_it = doc.find("presets");
        if (presets_it == doc.end() || !presets_it->is_array()) {
            std::fprintf(stderr, "[vivid] Factory presets: missing 'presets' array in %s\n", name);
            continue;
        }

        std::vector<OperatorPreset> presets;
        for (const auto& preset_val : *presets_it) {
            auto pname_it = preset_val.find("name");
            if (pname_it == preset_val.end() || !pname_it->is_string()) continue;

            OperatorPreset op;
            op.name = pname_it->get<std::string>();

            auto params_it = preset_val.find("params");
            if (params_it != preset_val.end() && params_it->is_object()) {
                for (const auto& [pk, pv] : params_it->items()) {
                    if (pv.is_number())
                        op.params[pk] = static_cast<float>(pv.get<double>());
                }
            }

            auto sparams_it = preset_val.find("string_params");
            if (sparams_it != preset_val.end() && sparams_it->is_object()) {
                for (const auto& [sk, sv] : sparams_it->items()) {
                    if (sv.is_string())
                        op.string_params[sk] = sv.get<std::string>();
                }
            }

            presets.push_back(std::move(op));
        }

        if (!presets.empty()) {
            factory_presets_[type_name] = std::move(presets);
            std::fprintf(stderr, "[vivid] Registry: loaded %zu factory presets for %s\n",
                         factory_presets_[type_name].size(), type_name.c_str());
        }
    }

    closedir(dir);
    return true;
}

} // namespace vivid
