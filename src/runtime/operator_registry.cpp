#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/platform.h"
#include "runtime/wgsl_header_parser.h"
#include "operator_api/data_driven_filter.h"
#include <yyjson.h>
#include <dlfcn.h>
#include <dirent.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <filesystem>

namespace vivid {

static std::string resolve_alias_once(const std::unordered_map<std::string, std::string>& aliases,
                                      const std::string& type_name) {
    std::string cur = type_name;
    std::unordered_set<std::string> seen;
    while (true) {
        auto it = aliases.find(cur);
        if (it == aliases.end()) break;
        if (!seen.insert(cur).second) break;
        cur = it->second;
    }
    return cur;
}

static std::unordered_set<std::string> parse_skip_plugins_env() {
    std::unordered_set<std::string> out;
    const char* env = std::getenv("VIVID_SKIP_PLUGINS");
    if (!env || !*env) return out;

    std::string s(env);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t next = s.find(',', pos);
        std::string item = s.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        // Trim simple surrounding spaces.
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (!item.empty()) out.insert(std::move(item));
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return out;
}

static uint32_t runtime_abi_override() {
    // Testing only — do not set in production
    const char* env = std::getenv("VIVID_MOCK_RUNTIME_ABI");
    if (!env || !*env) return VIVID_OPERATOR_ABI_VERSION;
    char* end = nullptr;
    unsigned long v = std::strtoul(env, &end, 10);
    if (end == env) return VIVID_OPERATOR_ABI_VERSION;
    return static_cast<uint32_t>(v);
}

static std::string guess_package_name_from_plugin_path(const std::string& plugin_path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = fs::path(plugin_path).lexically_normal();
    if (p.empty()) return {};
    fs::path parent = p.parent_path();
    if (parent.empty()) return {};
    if (parent.filename() == "build") {
        fs::path pkg = parent.parent_path();
        if (!pkg.empty()) return pkg.filename().string();
    }
    return {};
}

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
    const auto skipped = parse_skip_plugins_env();

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = std::strlen(name);
        if (len < suffix_len + 1 || std::strcmp(name + len - suffix_len, kPluginSuffix) != 0)
            continue;
        // Skip system/library shared objects (lib*); operators are name.dylib/.so/.dll
        if (std::strncmp(name, "lib", 3) == 0)
            continue;
        std::string stem(name, len - suffix_len);
        if (skipped.count(name) || skipped.count(stem)) {
            std::fprintf(stderr, "[vivid] Registry: skipping plugin %s (VIVID_SKIP_PLUGINS)\n", name);
            continue;
        }

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

        if (loaders_.count(type_name)) {
            std::fprintf(stderr,
                "[vivid] warning: operator type '%s' already registered; "
                "overwriting with %s. Check for duplicate kName across operators.\n",
                type_name.c_str(), name);
        }
        loaders_[type_name] = std::move(loader);
    });
}

// Deep-copy a VividOperatorDescriptor into a DeferredEntry with fully owned storage.
// Returns std::nullopt if the descriptor is malformed (null params/ports with non-zero counts).
static std::optional<DeferredEntry> deep_copy_descriptor(const VividOperatorDescriptor* src,
                                                          const std::string& dylib_path) {
    DeferredEntry entry;
    entry.dylib_path = dylib_path;

    // Sanity-check and cap param_count to prevent runaway allocation.
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

    // Sanity-check and cap port_count.
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

    // Copy param descriptors with owned strings
    entry.params.resize(param_count);
    entry.param_names.resize(param_count);
    entry.default_strings.resize(param_count);
    entry.semantic_tags.resize(param_count);
    entry.semantic_shapes.resize(param_count);
    entry.semantic_units.resize(param_count);
    entry.semantic_intents.resize(param_count);
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
    }

    // Copy port descriptors with owned strings
    entry.ports.resize(port_count);
    entry.port_names.resize(port_count);
    for (uint32_t i = 0; i < port_count; ++i) {
        const auto& sp = src->ports[i];
        auto& dp = entry.ports[i];
        entry.port_names[i] = sp.name ? sp.name : "";
        dp.name = entry.port_names[i].c_str();
        dp.type = sp.type;
        dp.direction = sp.direction;
    }

    // Build the owned descriptor
    entry.desc.name = nullptr;  // set after emplace (points to stable map key)
    entry.desc.domain = src->domain;
    entry.desc.param_count = param_count;
    entry.desc.params = entry.params.empty() ? nullptr : entry.params.data();
    entry.desc.port_count = port_count;
    entry.desc.ports = entry.ports.empty() ? nullptr : entry.ports.data();
    entry.desc.time_dependent = src->time_dependent;

    return entry;
}

bool OperatorRegistry::scan_deferred(const char* directory) {
    const bool trace_probe = std::getenv("VIVID_REGISTRY_TRACE") != nullptr;
    const uint32_t runtime_abi = runtime_abi_override();
    return scan_plugin_dir(directory, [&](const std::string& path, const char* name, size_t /*stem_len*/) {
        // Probe only: open, read descriptor, close
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
        auto abi_fn = reinterpret_cast<VividAbiVersionFn>(dlsym(handle, "vivid_abi_version"));
        if (!abi_fn) {
            std::fprintf(stderr, "[vivid] probe: skipping %s (missing vivid_abi_version; stale/incompatible plugin)\n", name);
            deferred_probe_handles_.push_back(handle);
            return;
        }
        const uint32_t abi = abi_fn();
        if (abi != runtime_abi) {
            std::fprintf(stderr,
                         "[vivid] probe: skipping %s (ABI %u != runtime ABI %u)\n",
                         name, abi, runtime_abi);
            AbiMismatchDiagnostic diag;
            diag.plugin_path = path;
            diag.plugin_name = name;
            diag.package_name = guess_package_name_from_plugin_path(path);
            diag.plugin_abi = abi;
            diag.runtime_abi = runtime_abi;
            abi_mismatch_by_path_[path] = std::move(diag);
            deferred_probe_handles_.push_back(handle);
            return;
        }
        if (!desc_fn) {
            std::fprintf(stderr, "[vivid] probe: missing vivid_descriptor in %s\n", name);
            deferred_probe_handles_.push_back(handle);
            return;
        }

        const VividOperatorDescriptor* desc = desc_fn();
        if (!desc || !desc->name) {
            deferred_probe_handles_.push_back(handle);
            return;
        }
        abi_mismatch_by_path_.erase(path);

        std::string type_name = desc->name;

        // Skip if already fully loaded (e.g. registered as builtin)
        if (loaders_.count(type_name)) {
            deferred_probe_handles_.push_back(handle);
            return;
        }

        // Deep-copy descriptor into owned storage
        auto de_opt = deep_copy_descriptor(desc, path);
        // Keep probe handles alive for process lifetime. Some plugins execute
        // problematic teardown paths during dlclose() and can stall startup.
        deferred_probe_handles_.push_back(handle);
        if (!de_opt) return;  // malformed descriptor — skip operator

        auto [it, inserted] = deferred_.emplace(type_name, std::move(*de_opt));
        if (!inserted) {
            std::fprintf(stderr,
                "[vivid] warning: deferred operator type '%s' already registered; "
                "ignoring %s. Check for duplicate kName across operators.\n",
                type_name.c_str(), name);
            return;
        }
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
        const std::string resolved = resolve_alias_once(aliases_, ndef.type);
        if (loaders_.count(resolved)) continue;      // already loaded
        if (wgsl_configs_.count(resolved)) continue;  // handled by scheduler
        auto dit = deferred_.find(resolved);
        if (dit == deferred_.end()) continue;          // builtin or unknown

        auto loader = std::make_unique<OperatorLoader>();
        if (!loader->load(dit->second.dylib_path.c_str())) {
            std::fprintf(stderr, "[vivid] Registry: failed to load %s\n", resolved.c_str());
            return false;
        }

        register_target_mapping(dit->second.dylib_path, resolved);
        loaders_[resolved] = std::move(loader);
        deferred_.erase(dit);
        std::fprintf(stderr, "[vivid] Registry: loaded %s (on demand)\n", resolved.c_str());
    }
    return true;
}

OperatorLoader* OperatorRegistry::find_loaded(const std::string& type_name) {
    const std::string resolved = resolve_alias_once(aliases_, type_name);
    auto it = loaders_.find(resolved);
    return (it != loaders_.end()) ? it->second.get() : nullptr;
}

const VividOperatorDescriptor* OperatorRegistry::probe_descriptor(const std::string& type_name) const {
    const std::string resolved = resolve_alias_once(aliases_, type_name);
    auto lit = loaders_.find(resolved);
    if (lit != loaders_.end() && lit->second) {
        return lit->second->descriptor();
    }
    auto dit = deferred_.find(resolved);
    if (dit == deferred_.end()) return nullptr;
    return &dit->second.desc;
}

void OperatorRegistry::register_builtin(const std::string& type_name,
                                        VividDescriptorFn desc_fn, VividCreateFn create_fn,
                                        VividDestroyFn destroy_fn, VividProcessFn process_fn) {
    if (loaders_.count(type_name)) {
        std::fprintf(stderr, "[vivid] warning: re-registering operator type '%s'\n", type_name.c_str());
    }
    auto loader = std::make_unique<OperatorLoader>();
    loader->init_builtin(desc_fn, create_fn, destroy_fn, process_fn);
    std::fprintf(stderr, "[vivid] Registry: registered built-in %s\n", type_name.c_str());
    loaders_[type_name] = std::move(loader);
}

void OperatorRegistry::register_alias(const std::string& alias_name,
                                      const std::string& canonical_type_name) {
    if (alias_name.empty() || canonical_type_name.empty() || alias_name == canonical_type_name) return;
    aliases_[alias_name] = canonical_type_name;
}

OperatorLoader* OperatorRegistry::find(const std::string& type_name) {
    const std::string resolved = resolve_alias_once(aliases_, type_name);
    auto it = loaders_.find(resolved);
    if (it != loaders_.end()) return it->second.get();

    // Try deferred loading
    auto dit = deferred_.find(resolved);
    if (dit == deferred_.end()) return nullptr;

    auto loader = std::make_unique<OperatorLoader>();
    if (!loader->load(dit->second.dylib_path.c_str())) return nullptr;

    const std::string loaded_path = dit->second.dylib_path;
    register_target_mapping(loaded_path, resolved);
    auto* ptr = loader.get();
    loaders_[resolved] = std::move(loader);
    deferred_.erase(dit);
    abi_mismatch_by_path_.erase(loaded_path);
    std::fprintf(stderr, "[vivid] Registry: loaded %s (lazy)\n", resolved.c_str());
    return ptr;
}

void OperatorRegistry::register_user_filter(const std::string& name,
                                            std::shared_ptr<DataDrivenFilterConfig> config) {
    if (loaders_.count(name)) {
        std::fprintf(stderr, "[vivid] warning: re-registering operator type '%s'\n", name.c_str());
    }
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
    abi_mismatch_by_path_.erase(dylib_path);
    return true;
}

std::vector<std::string> OperatorRegistry::type_names() const {
    std::vector<std::string> names;
    names.reserve(loaders_.size() + deferred_.size() + aliases_.size());
    for (const auto& [name, _] : loaders_) names.push_back(name);
    for (const auto& [name, _] : deferred_) names.push_back(name);
    for (const auto& [name, _] : aliases_) names.push_back(name);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
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
        std::fprintf(stderr,
            "[vivid] hot-reload failed for '%s' — loader is now unloaded; "
            "operator disabled until next successful reload\n",
            type_name.c_str());
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

std::vector<AbiMismatchDiagnostic> OperatorRegistry::abi_mismatch_diagnostics() const {
    std::vector<AbiMismatchDiagnostic> out;
    out.reserve(abi_mismatch_by_path_.size());
    for (const auto& [_, diag] : abi_mismatch_by_path_) out.push_back(diag);
    std::sort(out.begin(), out.end(), [](const AbiMismatchDiagnostic& a, const AbiMismatchDiagnostic& b) {
        return a.plugin_path < b.plugin_path;
    });
    return out;
}

std::vector<AbiMismatchDiagnostic> OperatorRegistry::abi_mismatch_diagnostics_for_dir(
        const std::string& directory) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(fs::path(directory), ec);
    if (ec) dir = fs::path(directory).lexically_normal();
    const std::string dir_s = dir.string();
    const std::string dir_slash = dir_s.empty() ? dir_s : (dir_s + "/");

    std::vector<AbiMismatchDiagnostic> out;
    for (const auto& [path, diag] : abi_mismatch_by_path_) {
        fs::path path_norm = fs::weakly_canonical(fs::path(path), ec);
        if (ec) {
            ec.clear();
            path_norm = fs::path(path).lexically_normal();
        }
        const std::string path_s = path_norm.string();
        if (path_s == dir_s || path_s.rfind(dir_slash, 0) == 0) out.push_back(diag);
    }
    std::sort(out.begin(), out.end(), [](const AbiMismatchDiagnostic& a, const AbiMismatchDiagnostic& b) {
        return a.plugin_path < b.plugin_path;
    });
    return out;
}

bool OperatorRegistry::has_abi_mismatch_diagnostics() const {
    return !abi_mismatch_by_path_.empty();
}

// --- Factory presets ---

bool OperatorRegistry::scan_factory_presets(const std::string& directory) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        // Not an error — factory_presets/ may not exist yet
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = std::strlen(name);
        if (len < 6 || std::strcmp(name + len - 5, ".json") != 0)
            continue;

        // Stem = cmake target name
        std::string target(name, len - 5);

        // Resolve target → operator type name
        auto tit = target_to_type_.find(target);
        if (tit == target_to_type_.end()) {
            std::fprintf(stderr, "[vivid] Factory presets: unknown target '%s', skipping\n",
                         target.c_str());
            continue;
        }
        const std::string& type_name = tit->second;

        // Read file
        std::string path = directory + "/" + name;
        std::ifstream ifs(path);
        if (!ifs) continue;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string contents = ss.str();

        // Parse JSON
        yyjson_doc* doc = yyjson_read(contents.c_str(), contents.size(), 0);
        if (!doc) {
            std::fprintf(stderr, "[vivid] Factory presets: failed to parse %s\n", name);
            continue;
        }

        yyjson_val* root = yyjson_doc_get_root(doc);
        yyjson_val* presets_arr = yyjson_obj_get(root, "presets");
        if (!presets_arr || !yyjson_is_arr(presets_arr)) {
            yyjson_doc_free(doc);
            std::fprintf(stderr, "[vivid] Factory presets: missing 'presets' array in %s\n", name);
            continue;
        }

        std::vector<OperatorPreset> presets;
        size_t idx, max;
        yyjson_val* preset_val;
        yyjson_arr_foreach(presets_arr, idx, max, preset_val) {
            yyjson_val* pname = yyjson_obj_get(preset_val, "name");
            if (!pname || !yyjson_is_str(pname)) continue;

            OperatorPreset op;
            op.name = yyjson_get_str(pname);

            // Float params
            yyjson_val* params_obj = yyjson_obj_get(preset_val, "params");
            if (params_obj && yyjson_is_obj(params_obj)) {
                size_t pi, pmax;
                yyjson_val *pk, *pv;
                yyjson_obj_foreach(params_obj, pi, pmax, pk, pv) {
                    if (yyjson_is_num(pv))
                        op.params[yyjson_get_str(pk)] = static_cast<float>(yyjson_get_num(pv));
                }
            }

            // String params (optional)
            yyjson_val* sparams_obj = yyjson_obj_get(preset_val, "string_params");
            if (sparams_obj && yyjson_is_obj(sparams_obj)) {
                size_t si, smax;
                yyjson_val *sk, *sv;
                yyjson_obj_foreach(sparams_obj, si, smax, sk, sv) {
                    if (yyjson_is_str(sv))
                        op.string_params[yyjson_get_str(sk)] = yyjson_get_str(sv);
                }
            }

            presets.push_back(std::move(op));
        }

        yyjson_doc_free(doc);

        if (!presets.empty()) {
            factory_presets_[type_name] = std::move(presets);
            std::fprintf(stderr, "[vivid] Registry: loaded %zu factory presets for %s\n",
                         factory_presets_[type_name].size(), type_name.c_str());
        }
    }

    closedir(dir);
    return true;
}

const std::vector<OperatorPreset>* OperatorRegistry::factory_presets(
        const std::string& type_name) const {
    auto it = factory_presets_.find(type_name);
    if (it == factory_presets_.end()) return nullptr;
    return &it->second;
}

std::vector<std::string> OperatorRegistry::factory_preset_names(
        const std::string& type_name) const {
    auto it = factory_presets_.find(type_name);
    if (it == factory_presets_.end()) return {};
    std::vector<std::string> names;
    names.reserve(it->second.size());
    for (const auto& p : it->second)
        names.push_back(p.name);
    return names;
}

} // namespace vivid
