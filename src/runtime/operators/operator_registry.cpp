#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/platform/platform.h"
#include "runtime/gpu/wgsl_header_parser.h"
#include "operator_api/operator.h"
#include "operator_api/data_driven_filter.h"

#include <nlohmann/json.hpp>
#include <dlfcn.h>
#include <dirent.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <filesystem>

namespace vivid {

OperatorRegistry::~OperatorRegistry() {
    // Controlled teardown order to avoid use-after-free at process exit.
    // 1. Release deep-copied descriptor data while libraries are still loaded.
    deferred_.clear();
    // 2. Destroy loaders (triggers dlclose on their handles).
    loaders_.clear();
    // 3. Clear retired package loaders.
    retired_package_loaders_.clear();
    // Do NOT dlclose deferred_probe_handles_ — they are process-lifetime
    // handles kept alive intentionally to avoid plugin teardown hangs.
}

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

static std::string normalized_extension(std::string ext) {
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

template<typename DiagnosticT>
static std::vector<DiagnosticT> diagnostics_for_dir(
        const std::unordered_map<std::string, DiagnosticT>& diagnostics_by_path,
        const std::string& directory) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(fs::path(directory), ec);
    if (ec) dir = fs::path(directory).lexically_normal();
    const std::string dir_s = dir.string();
    const std::string dir_slash = dir_s.empty() ? dir_s : (dir_s + "/");

    std::vector<DiagnosticT> out;
    for (const auto& [path, diag] : diagnostics_by_path) {
        fs::path path_norm = fs::weakly_canonical(fs::path(path), ec);
        if (ec) {
            ec.clear();
            path_norm = fs::path(path).lexically_normal();
        }
        const std::string path_s = path_norm.string();
        if (path_s == dir_s || path_s.rfind(dir_slash, 0) == 0) out.push_back(diag);
    }
    std::sort(out.begin(), out.end(), [](const DiagnosticT& a, const DiagnosticT& b) {
        return a.plugin_path < b.plugin_path;
    });
    return out;
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
        if (!loader->load(path.c_str())) {
            record_loader_failure(path, name, loader->last_error());
            return;
        }

        const VividOperatorDescriptor* desc = loader->descriptor();
        if (!desc || !desc->name) return;
        loader_failure_by_path_.erase(path);

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
static std::optional<DeferredEntry> deep_copy_descriptor(
        const VividOperatorDescriptor* src,
        const VividFileDropHandlerDescriptor* file_drop_src,
        uint32_t file_drop_count,
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
    entry.descriptions.resize(param_count);
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
        if (sp.description) {
            entry.descriptions[i] = sp.description;
            dp.description = entry.descriptions[i].c_str();
        } else {
            dp.description = nullptr;
        }
    }

    // Copy port descriptors with owned strings
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
        dp.type_name = entry.port_type_names[i].empty() ? nullptr
                                                        : entry.port_type_names[i].c_str();
        entry.port_stable_type_ids[i] = sp.stable_type_id ? sp.stable_type_id : "";
        dp.stable_type_id = entry.port_stable_type_ids[i].empty() ? nullptr
                                                                  : entry.port_stable_type_ids[i].c_str();
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

    // Build the owned descriptor
    entry.desc.name = nullptr;  // set after emplace (points to stable map key)
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
                ext = normalized_extension(ext);
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

bool OperatorRegistry::scan_deferred(const char* directory) {
    const bool trace_probe = std::getenv("VIVID_REGISTRY_TRACE") != nullptr;
    const uint32_t runtime_abi = runtime_abi_override();
    return scan_plugin_dir(directory, [&](const std::string& path, const char* name, size_t /*stem_len*/) {
        // Probe only: open, read descriptor, close
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
            diag.package_name = guess_package_name_from_plugin_path(path);
            diag.plugin_abi = abi;
            diag.runtime_abi = runtime_abi;
            abi_mismatch_by_path_[path] = std::move(diag);
            // Enrich expected-operator provenance if known
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

        // Skip if already fully loaded (e.g. registered as builtin)
        if (loaders_.count(type_name)) {
            deferred_probe_handles_.push_back({path, handle});
            return;
        }

        // Deep-copy descriptor into owned storage
        uint32_t file_drop_count = 0;
        const VividFileDropHandlerDescriptor* file_drop_desc =
            file_drop_fn ? file_drop_fn(&file_drop_count) : nullptr;
        auto de_opt = deep_copy_descriptor(desc, file_drop_desc, file_drop_count, path);
        // Keep probe handles alive for process lifetime. Some plugins execute
        // problematic teardown paths during dlclose() and can stall startup.
        deferred_probe_handles_.push_back({path, handle});
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
    // Actual instances get per-instance descriptors from the runtime.
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

    // Promote expected-operator provenance from target stem → type name
    if (filename != type_name) {
        auto it = expected_operators_.find(filename);
        if (it != expected_operators_.end()) {
            expected_operators_[type_name] = it->second;
            expected_operators_.erase(it);
        }
    }
}

bool OperatorRegistry::load_for_graph(const Graph& graph) {
    for (const auto& ndef : graph.nodes()) {
        const std::string resolved = resolve_alias_once(aliases_, ndef.type);
        if (loaders_.count(resolved)) continue;      // already loaded
        if (wgsl_configs_.count(resolved)) continue;  // handled by runtime
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
                                        VividDestroyFn destroy_fn, VividProcessFrameFn process_fn) {
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
    if (!loader->load(dit->second.dylib_path.c_str())) {
        const std::filesystem::path p(dit->second.dylib_path);
        record_loader_failure(dit->second.dylib_path, p.filename().string(), loader->last_error());
        return nullptr;
    }

    const std::string loaded_path = dit->second.dylib_path;
    register_target_mapping(loaded_path, resolved);
    auto* ptr = loader.get();
    loaders_[resolved] = std::move(loader);
    deferred_.erase(dit);
    abi_mismatch_by_path_.erase(loaded_path);
    loader_failure_by_path_.erase(loaded_path);
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
    if (!loader->load(dylib_path.c_str())) {
        const std::filesystem::path p(dylib_path);
        record_loader_failure(dylib_path, p.filename().string(), loader->last_error());
        return false;
    }

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
    loader_failure_by_path_.erase(dylib_path);
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

    // OperatorLoader::load() swaps atomically: on failure the previous loader remains live.
    if (!it->second->load(new_dylib_path.c_str())) {
        const std::filesystem::path p(new_dylib_path);
        record_loader_failure(new_dylib_path, p.filename().string(), it->second->last_error());
        std::fprintf(stderr,
            "[vivid] hot-reload failed for '%s' — previous loader kept active\n",
            type_name.c_str());
        return false;
    }

    loader_failure_by_path_.erase(new_dylib_path);
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
    auto lit = loaders_.find(type_name);
    if (lit != loaders_.end()) {
        retired_package_loaders_.push_back(std::move(lit->second));
        loaders_.erase(lit);
    }
    deferred_.erase(type_name);
    type_to_package_.erase(type_name);
}

void OperatorRegistry::clear_retired_package_loaders() {
    retired_package_loaders_.clear();
}

void OperatorRegistry::clear_deferred_probe_handles_for_dir(const std::string& directory) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(fs::path(directory), ec);
    if (ec) dir = fs::path(directory).lexically_normal();
    const std::string dir_s = dir.string();
    const std::string dir_prefix = dir_s.empty() ? dir_s : (dir_s + "/");

    auto it = deferred_probe_handles_.begin();
    while (it != deferred_probe_handles_.end()) {
        fs::path plugin = fs::weakly_canonical(fs::path(it->plugin_path), ec);
        if (ec) {
            ec.clear();
            plugin = fs::path(it->plugin_path).lexically_normal();
        }
        const std::string plugin_s = plugin.string();
        const bool in_dir = plugin_s == dir_s || plugin_s.rfind(dir_prefix, 0) == 0;
        if (!in_dir) {
            ++it;
            continue;
        }
        if (it->handle) dlclose(it->handle);
        it = deferred_probe_handles_.erase(it);
    }
}

const std::string* OperatorRegistry::package_for_type(const std::string& type_name) const {
    auto it = type_to_package_.find(type_name);
    if (it == type_to_package_.end()) return nullptr;
    return &it->second;
}

bool OperatorRegistry::is_package_operator(const std::string& type_name) const {
    return type_to_package_.count(type_name) > 0;
}

std::vector<FileDropRegistration> OperatorRegistry::file_drop_handlers() const {
    auto build_for = [&](const std::string& type_name,
                         const VividOperatorDescriptor* desc,
                         const VividFileDropHandlerDescriptor* handlers,
                         uint32_t handler_count,
                         std::vector<FileDropRegistration>& out) {
        if (!desc || !handlers || handler_count == 0) return;
        for (uint32_t i = 0; i < handler_count; ++i) {
            const auto& h = handlers[i];
            if (!h.extensions || h.extension_count == 0 || !h.file_param || !*h.file_param)
                continue;

            bool file_param_valid = false;
            for (uint32_t pi = 0; pi < desc->param_count; ++pi) {
                const auto& pd = desc->params[pi];
                if (!pd.name || std::strcmp(pd.name, h.file_param) != 0) continue;
                file_param_valid = (pd.type == VIVID_PARAM_FILE || pd.type == VIVID_PARAM_TEXT);
                break;
            }
            if (!file_param_valid) continue;

            FileDropRegistration reg;
            reg.type_name = type_name;
            reg.label = (h.label && *h.label) ? h.label : type_name;
            reg.file_param = h.file_param;
            reg.description = h.description ? h.description : "";
            reg.priority = h.priority;
            if (const auto* pkg = package_for_type(type_name))
                reg.package_name = *pkg;
            reg.extensions.reserve(h.extension_count);
            for (uint32_t ei = 0; ei < h.extension_count; ++ei) {
                std::string ext = h.extensions[ei] ? h.extensions[ei] : "";
                ext = normalized_extension(ext);
                if (!ext.empty())
                    reg.extensions.push_back(std::move(ext));
            }
            if (!reg.extensions.empty())
                out.push_back(std::move(reg));
        }
    };

    std::vector<FileDropRegistration> out;
    for (const auto& [type_name, loader] : loaders_) {
        uint32_t count = 0;
        const auto* handlers = loader ? loader->file_drop_handlers(&count) : nullptr;
        build_for(type_name, loader ? loader->descriptor() : nullptr, handlers, count, out);
    }
    for (const auto& [type_name, deferred] : deferred_) {
        build_for(type_name, &deferred.desc,
                  deferred.file_drop_handlers.data(),
                  static_cast<uint32_t>(deferred.file_drop_handlers.size()),
                  out);
    }
    std::sort(out.begin(), out.end(), [](const FileDropRegistration& a, const FileDropRegistration& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.label != b.label) return a.label < b.label;
        return a.type_name < b.type_name;
    });
    return out;
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
    return diagnostics_for_dir(abi_mismatch_by_path_, directory);
}

bool OperatorRegistry::has_abi_mismatch_diagnostics() const {
    return !abi_mismatch_by_path_.empty();
}

std::vector<LoaderFailureDiagnostic> OperatorRegistry::loader_failure_diagnostics() const {
    std::vector<LoaderFailureDiagnostic> out;
    out.reserve(loader_failure_by_path_.size());
    for (const auto& [_, diag] : loader_failure_by_path_) out.push_back(diag);
    std::sort(out.begin(), out.end(), [](const LoaderFailureDiagnostic& a, const LoaderFailureDiagnostic& b) {
        return a.plugin_path < b.plugin_path;
    });
    return out;
}

std::vector<LoaderFailureDiagnostic> OperatorRegistry::loader_failure_diagnostics_for_dir(
        const std::string& directory) const {
    return diagnostics_for_dir(loader_failure_by_path_, directory);
}

bool OperatorRegistry::has_loader_failure_diagnostics() const {
    return !loader_failure_by_path_.empty();
}

std::vector<OperatorMapEntry> OperatorRegistry::operator_map() const {
    std::vector<OperatorMapEntry> out;

    // Deferred (probed but not fully loaded) — includes package operators
    for (const auto& [type_name, de] : deferred_) {
        OperatorMapEntry e;
        e.type_name = type_name;
        e.dylib_path = de.dylib_path;
        e.status = "deferred";
        auto pkg_it = type_to_package_.find(type_name);
        if (pkg_it != type_to_package_.end())
            e.package_name = pkg_it->second;
        out.push_back(std::move(e));
    }

    // Fully loaded (built-in or lazy-loaded from deferred)
    for (const auto& [type_name, loader] : loaders_) {
        // Skip if already covered by deferred
        bool found = false;
        for (const auto& existing : out) {
            if (existing.type_name == type_name) { found = true; break; }
        }
        if (found) continue;
        OperatorMapEntry e;
        e.type_name = type_name;
        e.status = "loaded";
        auto pkg_it = type_to_package_.find(type_name);
        if (pkg_it != type_to_package_.end())
            e.package_name = pkg_it->second;
        out.push_back(std::move(e));
    }

    // ABI mismatches
    for (const auto& [path, diag] : abi_mismatch_by_path_) {
        OperatorMapEntry e;
        e.type_name = diag.plugin_name;
        e.dylib_path = diag.plugin_path;
        e.package_name = diag.package_name;
        e.status = "abi_mismatch";
        e.abi_version = diag.plugin_abi;
        out.push_back(std::move(e));
    }

    std::sort(out.begin(), out.end(), [](const OperatorMapEntry& a, const OperatorMapEntry& b) {
        return a.type_name < b.type_name;
    });
    return out;
}

void OperatorRegistry::register_expected_operator(const std::string& type_name,
                                                  OperatorProvenance provenance) {
    expected_operators_[type_name] = std::move(provenance);
}

const OperatorProvenance* OperatorRegistry::operator_provenance(const std::string& type_name) const {
    auto it = expected_operators_.find(type_name);
    if (it != expected_operators_.end()) return &it->second;
    return nullptr;
}

void OperatorRegistry::record_loader_failure(const std::string& plugin_path,
                                             const std::string& plugin_name,
                                             const OperatorLoader::LastError& error) {
    LoaderFailureDiagnostic diag;
    diag.plugin_path = plugin_path;
    diag.plugin_name = plugin_name;
    diag.package_name = guess_package_name_from_plugin_path(plugin_path);
    diag.code = error.code.empty() ? "load_failed" : error.code;
    diag.message = error.message.empty() ? "operator load failed" : error.message;
    loader_failure_by_path_[plugin_path] = std::move(diag);
    // Enrich expected-operator provenance if known
    auto prov_it = expected_operators_.find(plugin_name);
    if (prov_it != expected_operators_.end()) {
        prov_it->second.load_failed = true;
        prov_it->second.failure_detail = diag.message;
    }
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

            // Float params
            auto params_it = preset_val.find("params");
            if (params_it != preset_val.end() && params_it->is_object()) {
                for (const auto& [pk, pv] : params_it->items()) {
                    if (pv.is_number())
                        op.params[pk] = static_cast<float>(pv.get<double>());
                }
            }

            // String params (optional)
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
