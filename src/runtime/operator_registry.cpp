#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/platform.h"
#include "runtime/wgsl_header_parser.h"
#include "operator_api/data_driven_filter.h"
#include <dlfcn.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace vivid {

// Iterate plugin files in a directory, calling fn(path, filename, stem_len) for each
// matching file (correct suffix, not lib*-prefixed).
template<typename Fn>
static bool scan_plugin_dir(const char* directory, Fn&& fn) {
    DIR* dir = opendir(directory);
    if (!dir) {
        std::fprintf(stderr, "[vivid] Registry: failed to open directory: %s\n", directory);
        return false;
    }

    size_t suffix_len = std::strlen(kPluginSuffix);

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = std::strlen(name);
        if (len < suffix_len + 1 || std::strcmp(name + len - suffix_len, kPluginSuffix) != 0)
            continue;
        // Skip system/library shared objects (lib*); operators are name.dylib/.so/.dll
        if (std::strncmp(name, "lib", 3) == 0)
            continue;

        std::string path = std::string(directory) + "/" + name;
        fn(path, name, len - suffix_len);
    }

    closedir(dir);
    return true;
}

bool OperatorRegistry::scan(const char* directory) {
    return scan_plugin_dir(directory, [&](const std::string& path, const char* name, size_t stem_len) {
        auto loader = std::make_unique<OperatorLoader>();
        if (!loader->load(path.c_str()))
            return;

        const VividOperatorDescriptor* desc = loader->descriptor();
        if (!desc || !desc->name) return;

        std::string type_name = desc->name;
        std::fprintf(stderr, "[vivid] Registry: loaded %s from %s\n", type_name.c_str(), name);

        // Map cmake target name (filename stem) → descriptor type name
        std::string target_name(name, stem_len);
        target_to_type_[target_name] = type_name;

        loaders_[type_name] = std::move(loader);
    });
}

// Deep-copy a VividOperatorDescriptor into a DeferredEntry with fully owned storage
static DeferredEntry deep_copy_descriptor(const VividOperatorDescriptor* src,
                                           const std::string& dylib_path) {
    DeferredEntry entry;
    entry.dylib_path = dylib_path;

    // Copy param descriptors with owned strings
    entry.params.resize(src->param_count);
    entry.param_names.resize(src->param_count);
    entry.default_strings.resize(src->param_count);
    entry.choice_labels.resize(src->param_count);
    entry.choice_label_ptrs.resize(src->param_count);
    for (uint32_t i = 0; i < src->param_count; ++i) {
        const auto& sp = src->params[i];
        auto& dp = entry.params[i];
        entry.param_names[i] = sp.name ? sp.name : "";
        dp.name = entry.param_names[i].c_str();
        dp.type = sp.type;
        dp.default_value = sp.default_value;
        dp.min_value = sp.min_value;
        dp.max_value = sp.max_value;
        dp.choice_count = sp.choice_count;

        // Deep-copy choice labels
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

        // Deep-copy default_string (for file params)
        if (sp.default_string) {
            entry.default_strings[i] = sp.default_string;
            dp.default_string = entry.default_strings[i].c_str();
        } else {
            dp.default_string = nullptr;
        }
    }

    // Copy port descriptors with owned strings
    entry.ports.resize(src->port_count);
    entry.port_names.resize(src->port_count);
    for (uint32_t i = 0; i < src->port_count; ++i) {
        const auto& sp = src->ports[i];
        auto& dp = entry.ports[i];
        entry.port_names[i] = sp.name ? sp.name : "";
        dp.name = entry.port_names[i].c_str();
        dp.type = sp.type;
        dp.direction = sp.direction;
    }

    // Build the owned descriptor
    std::string name_str = src->name ? src->name : "";
    entry.desc.name = nullptr;  // set after emplace (points to stable map key)
    entry.desc.domain = src->domain;
    entry.desc.param_count = src->param_count;
    entry.desc.params = entry.params.data();
    entry.desc.port_count = src->port_count;
    entry.desc.ports = entry.ports.data();
    entry.desc.time_dependent = src->time_dependent;

    return entry;
}

bool OperatorRegistry::scan_deferred(const char* directory) {
    return scan_plugin_dir(directory, [&](const std::string& path, const char* name, size_t /*stem_len*/) {
        // Probe only: open, read descriptor, close
        void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            std::fprintf(stderr, "[vivid] probe dlopen failed: %s\n", dlerror());
            return;
        }

        auto desc_fn = reinterpret_cast<VividDescriptorFn>(dlsym(handle, "vivid_descriptor"));
        if (!desc_fn) {
            std::fprintf(stderr, "[vivid] probe: missing vivid_descriptor in %s\n", name);
            dlclose(handle);
            return;
        }

        const VividOperatorDescriptor* desc = desc_fn();
        if (!desc || !desc->name) {
            dlclose(handle);
            return;
        }

        std::string type_name = desc->name;

        // Skip if already fully loaded (e.g. registered as builtin)
        if (loaders_.count(type_name)) {
            dlclose(handle);
            return;
        }

        // Deep-copy descriptor into owned storage
        DeferredEntry de = deep_copy_descriptor(desc, path);

        dlclose(handle);

        auto [it, inserted] = deferred_.emplace(type_name, std::move(de));
        // Point desc.name at the map key (stable after emplace)
        it->second.desc.name = it->first.c_str();

        // Map cmake target name → descriptor type name
        register_target_mapping(path, type_name);

        std::fprintf(stderr, "[vivid] Registry: probed %s from %s\n", type_name.c_str(), name);
    });
}

bool OperatorRegistry::scan_wgsl_presets(const std::string& directory) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        // Not an error — filters/ directory is optional
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = std::strlen(name);
        if (len < 6 || std::strcmp(name + len - 5, ".wgsl") != 0)
            continue;

        std::string path = directory + "/" + name;

        // Read file
        std::ifstream ifs(path);
        if (!ifs) continue;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string contents = ss.str();

        // Parse header
        std::string error;
        auto header = parse_wgsl_header(contents, error);
        if (!header) {
            std::fprintf(stderr, "[vivid] Skipping %s: %s\n", name, error.c_str());
            continue;
        }

        // Skip if already fully loaded (e.g. user filter with same name)
        if (loaders_.count(header->name))
            continue;
        // .wgsl presets override stale deferred dylib entries
        if (deferred_.count(header->name))
            deferred_.erase(header->name);

        // Build DataDrivenFilterConfig and store in wgsl_configs_
        auto config = std::make_shared<DataDrivenFilterConfig>();
        config->name = header->name;
        config->shader_path = path;
        config->time_dependent = header->time_dependent;
        config->inputs_specified = header->inputs_specified;
        for (const auto& inp : header->inputs)
            config->inputs.push_back({inp.name});
        for (const auto& hp : header->params) {
            DataDrivenFilterConfig::ParamDef pd;
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
            config->params.push_back(std::move(pd));
        }

        wgsl_configs_[header->name] = config;
        std::fprintf(stderr, "[vivid] Registry: loaded preset %s from %s\n",
                     header->name.c_str(), name);
    }

    closedir(dir);

    // Register a single WGSLFilter type with a minimal factory descriptor.
    // Actual instances get per-instance descriptors from the scheduler.
    if (!wgsl_configs_.empty() && !loaders_.count("WGSLFilter")) {
        auto factory = std::make_shared<DataDrivenFilterConfig>();
        factory->name = "WGSLFilter";
        auto loader = std::make_unique<OperatorLoader>();
        loader->init_data_driven(std::move(factory));
        loaders_["WGSLFilter"] = std::move(loader);
        std::fprintf(stderr, "[vivid] Registry: registered WGSLFilter (%zu presets)\n",
                     wgsl_configs_.size());
    }

    return true;
}

void OperatorRegistry::register_target_mapping(const std::string& dylib_path,
                                                const std::string& type_name) {
    std::string filename = dylib_path;
    auto slash = filename.rfind('/');
    if (slash != std::string::npos) filename = filename.substr(slash + 1);
    size_t slen = std::strlen(kPluginSuffix);
    if (filename.size() > slen) filename = filename.substr(0, filename.size() - slen);
    target_to_type_[filename] = type_name;
}

bool OperatorRegistry::load_for_graph(const Graph& graph) {
    for (const auto& ndef : graph.nodes()) {
        if (loaders_.count(ndef.type)) continue;      // already loaded
        if (wgsl_configs_.count(ndef.type)) continue;  // handled by scheduler
        auto dit = deferred_.find(ndef.type);
        if (dit == deferred_.end()) continue;          // builtin or unknown

        auto loader = std::make_unique<OperatorLoader>();
        if (!loader->load(dit->second.dylib_path.c_str())) {
            std::fprintf(stderr, "[vivid] Registry: failed to load %s\n", ndef.type.c_str());
            return false;
        }

        register_target_mapping(dit->second.dylib_path, ndef.type);
        loaders_[ndef.type] = std::move(loader);
        deferred_.erase(dit);
        std::fprintf(stderr, "[vivid] Registry: loaded %s (on demand)\n", ndef.type.c_str());
    }
    return true;
}

OperatorLoader* OperatorRegistry::find_loaded(const std::string& type_name) {
    auto it = loaders_.find(type_name);
    return (it != loaders_.end()) ? it->second.get() : nullptr;
}

const VividOperatorDescriptor* OperatorRegistry::probe_descriptor(const std::string& type_name) const {
    auto dit = deferred_.find(type_name);
    if (dit == deferred_.end()) return nullptr;
    return &dit->second.desc;
}

void OperatorRegistry::register_builtin(const std::string& type_name,
                                        VividDescriptorFn desc_fn, VividCreateFn create_fn,
                                        VividDestroyFn destroy_fn, VividProcessFn process_fn) {
    auto loader = std::make_unique<OperatorLoader>();
    loader->init_builtin(desc_fn, create_fn, destroy_fn, process_fn);
    std::fprintf(stderr, "[vivid] Registry: registered built-in %s\n", type_name.c_str());
    loaders_[type_name] = std::move(loader);
}

OperatorLoader* OperatorRegistry::find(const std::string& type_name) {
    auto it = loaders_.find(type_name);
    if (it != loaders_.end()) return it->second.get();

    // Try deferred loading
    auto dit = deferred_.find(type_name);
    if (dit == deferred_.end()) return nullptr;

    auto loader = std::make_unique<OperatorLoader>();
    if (!loader->load(dit->second.dylib_path.c_str())) return nullptr;

    register_target_mapping(dit->second.dylib_path, type_name);
    auto* ptr = loader.get();
    loaders_[type_name] = std::move(loader);
    deferred_.erase(dit);
    std::fprintf(stderr, "[vivid] Registry: loaded %s (lazy)\n", type_name.c_str());
    return ptr;
}

void OperatorRegistry::register_user_filter(const std::string& name,
                                            std::shared_ptr<DataDrivenFilterConfig> config) {
    auto loader = std::make_unique<OperatorLoader>();
    loader->init_data_driven(std::move(config));
    std::fprintf(stderr, "[vivid] Registry: registered user filter %s\n", name.c_str());
    loaders_[name] = std::move(loader);
    user_filter_types_.insert(name);
}

void OperatorRegistry::unregister_user_filter(const std::string& name) {
    loaders_.erase(name);
    user_filter_types_.erase(name);
}

bool OperatorRegistry::is_user_filter(const std::string& name) const {
    return user_filter_types_.count(name) > 0;
}

void OperatorRegistry::register_user_operator(const std::string& name, const std::string& source_path) {
    user_operator_sources_[name] = source_path;
}

bool OperatorRegistry::is_user_operator(const std::string& name) const {
    return user_operator_sources_.count(name) > 0;
}

const std::string* OperatorRegistry::user_operator_source(const std::string& name) const {
    auto it = user_operator_sources_.find(name);
    if (it == user_operator_sources_.end()) return nullptr;
    return &it->second;
}

bool OperatorRegistry::register_loaded_operator(const std::string& dylib_path) {
    auto loader = std::make_unique<OperatorLoader>();
    if (!loader->load(dylib_path.c_str()))
        return false;

    const VividOperatorDescriptor* desc = loader->descriptor();
    if (!desc || !desc->name) {
        std::fprintf(stderr, "[vivid] Registry: null descriptor from %s\n", dylib_path.c_str());
        return false;
    }

    std::string type_name = desc->name;
    std::fprintf(stderr, "[vivid] Registry: loaded new operator %s from %s\n",
                 type_name.c_str(), dylib_path.c_str());

    register_target_mapping(dylib_path, type_name);
    loaders_[type_name] = std::move(loader);
    return true;
}

std::vector<std::string> OperatorRegistry::type_names() const {
    std::vector<std::string> names;
    names.reserve(loaders_.size() + deferred_.size());
    for (const auto& [name, _] : loaders_) names.push_back(name);
    for (const auto& [name, _] : deferred_) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

const std::string* OperatorRegistry::type_name_for_target(const std::string& target) const {
    auto it = target_to_type_.find(target);
    if (it == target_to_type_.end())
        return nullptr;
    return &it->second;
}

std::string OperatorRegistry::type_to_target(const std::string& type_name) const {
    for (const auto& [target, type] : target_to_type_) {
        if (type == type_name) return target;
    }
    return {};
}

const std::shared_ptr<DataDrivenFilterConfig>* OperatorRegistry::wgsl_config(
        const std::string& name) const {
    auto it = wgsl_configs_.find(name);
    if (it == wgsl_configs_.end()) return nullptr;
    return &it->second;
}

std::vector<std::string> OperatorRegistry::wgsl_preset_names() const {
    std::vector<std::string> names;
    names.reserve(wgsl_configs_.size());
    for (const auto& [name, _] : wgsl_configs_) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

bool OperatorRegistry::is_wgsl_preset(const std::string& name) const {
    return wgsl_configs_.count(name) > 0;
}

bool OperatorRegistry::reload_operator(const std::string& type_name, const std::string& new_dylib_path) {
    auto it = loaders_.find(type_name);
    if (it == loaders_.end()) {
        std::fprintf(stderr, "[vivid] Registry: unknown type '%s' for reload\n", type_name.c_str());
        return false;
    }

    // OperatorLoader::load() calls unload() (dlclose) then dlopen on new path
    if (!it->second->load(new_dylib_path.c_str())) {
        std::fprintf(stderr, "[vivid] Registry: failed to reload '%s' from %s\n",
            type_name.c_str(), new_dylib_path.c_str());
        return false;
    }

    return true;
}

void OperatorRegistry::register_package(const std::string& package_name,
                                         const std::string& build_dir) {
    // Walk the build directory and associate each operator type with this package
    scan_plugin_dir(build_dir.c_str(), [&](const std::string& path, const char* name, size_t stem_len) {
        // The target name (filename stem) maps to a type via target_to_type_
        std::string target(name, stem_len);
        auto it = target_to_type_.find(target);
        if (it != target_to_type_.end()) {
            type_to_package_[it->second] = package_name;
        } else {
            // Also check deferred entries directly
            for (const auto& [type, entry] : deferred_) {
                if (entry.dylib_path == path) {
                    type_to_package_[type] = package_name;
                    break;
                }
            }
        }
    });
}

void OperatorRegistry::unregister_package_operator(const std::string& type_name) {
    loaders_.erase(type_name);
    deferred_.erase(type_name);
    type_to_package_.erase(type_name);
}

const std::string* OperatorRegistry::package_for_type(const std::string& type_name) const {
    auto it = type_to_package_.find(type_name);
    if (it == type_to_package_.end()) return nullptr;
    return &it->second;
}

bool OperatorRegistry::is_package_operator(const std::string& type_name) const {
    return type_to_package_.count(type_name) > 0;
}

} // namespace vivid
