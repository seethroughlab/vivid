#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_registry_internal.h"
#include "operator_api/data_driven_filter.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <algorithm>
#include <filesystem>

namespace vivid {

void OperatorRegistry::register_shader_operator(std::shared_ptr<WgslOperatorConfig> config,
                                                bool mark_user,
                                                const std::string& package_name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string name = config ? config->name : "";
    if (name.empty()) return;
    if (loaders_.count(name)) {
        std::fprintf(stderr, "[vivid] warning: re-registering operator type '%s'\n", name.c_str());
    }
    auto loader = std::make_unique<OperatorLoader>();
    loader->init_wgsl_operator(config);
    shader_operator_configs_[name] = std::move(config);
    shader_operator_sources_[name] = shader_operator_configs_[name]->shader_path;
    if (mark_user) user_shader_operator_types_.insert(name);
    else user_shader_operator_types_.erase(name);
    if (!package_name.empty()) type_to_package_[name] = package_name;
    else type_to_package_.erase(name);
    std::fprintf(stderr, "[vivid] Registry: registered shader operator %s\n", name.c_str());
    loaders_[name] = std::move(loader);
}

void OperatorRegistry::unregister_shader_operator(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto lit = loaders_.find(name);
    if (lit != loaders_.end()) {
        retired_package_loaders_.push_back(std::move(lit->second));
        loaders_.erase(lit);
    }
    shader_operator_configs_.erase(name);
    shader_operator_sources_.erase(name);
    user_shader_operator_types_.erase(name);
    type_to_package_.erase(name);
}

void OperatorRegistry::clear_shader_operators_in_dir(const std::string& directory) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(fs::path(directory), ec);
    if (ec) dir = fs::path(directory).lexically_normal();
    const std::string dir_s = dir.string();
    const std::string dir_prefix = dir_s.empty() ? dir_s : (dir_s + "/");
    std::vector<std::string> to_remove;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto& [type_name, source] : shader_operator_sources_) {
            fs::path source_path = fs::weakly_canonical(fs::path(source), ec);
            if (ec) {
                ec.clear();
                source_path = fs::path(source).lexically_normal();
            }
            const std::string source_s = source_path.string();
            if (source_s == dir_s || source_s.rfind(dir_prefix, 0) == 0)
                to_remove.push_back(type_name);
        }
    }
    for (const auto& type_name : to_remove)
        unregister_shader_operator(type_name);
}

bool OperatorRegistry::is_shader_operator(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return shader_operator_configs_.count(name) > 0;
}

bool OperatorRegistry::is_user_shader_operator(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return user_shader_operator_types_.count(name) > 0;
}

const WgslOperatorConfig* OperatorRegistry::shader_operator_config(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = shader_operator_configs_.find(name);
    return it == shader_operator_configs_.end() ? nullptr : it->second.get();
}

const std::string* OperatorRegistry::shader_operator_source(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = shader_operator_sources_.find(name);
    if (it == shader_operator_sources_.end()) return nullptr;
    return &it->second;
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

int OperatorRegistry::rescan() {
    int newly = 0;
    for (const auto& dir : scanned_dirs_) {
        operator_registry_internal::scan_plugin_dir(
            dir.c_str(),
            [&](const std::string& path, const char* name, size_t stem_len) {
                // Identify already-known plugins by cmake target name (the
                // dylib stem). target_to_type_ is populated for both
                // immediate-load and deferred-probe paths, so this catches
                // every plugin the runtime already knows about.
                std::string target(name, stem_len);
                if (target_to_type_.count(target)) return;
                if (register_loaded_operator(path)) {
                    newly++;
                }
            });
    }
    if (newly > 0) {
        std::fprintf(stderr,
                     "[vivid] Registry: rescan registered %d new operator(s)\n",
                     newly);
    }
    return newly;
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

void OperatorRegistry::register_package(const std::string& package_name,
                                        const std::string& build_dir) {
    operator_registry_internal::scan_plugin_dir(build_dir.c_str(), [&](const std::string& path, const char* name, size_t stem_len) {
        std::string target(name, stem_len);
        auto it = target_to_type_.find(target);
        if (it != target_to_type_.end()) {
            type_to_package_[it->second] = package_name;
        } else {
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
                ext = operator_registry_internal::normalized_extension(ext);
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

std::vector<OperatorMapEntry> OperatorRegistry::operator_map() const {
    std::vector<OperatorMapEntry> out;

    for (const auto& [type_name, de] : deferred_) {
        OperatorMapEntry e;
        e.type_name = type_name;
        e.dylib_path = de.dylib_path;
        e.status = "deferred";
        e.registration_mode = de.registration_mode;
        auto pkg_it = type_to_package_.find(type_name);
        if (pkg_it != type_to_package_.end())
            e.package_name = pkg_it->second;
        out.push_back(std::move(e));
    }

    for (const auto& [type_name, loader] : loaders_) {
        bool found = false;
        for (const auto& existing : out) {
            if (existing.type_name == type_name) { found = true; break; }
        }
        if (found) continue;
        OperatorMapEntry e;
        e.type_name = type_name;
        e.status = "loaded";
        e.registration_mode = loader ? loader->registration_mode() : "unknown";
        auto pkg_it = type_to_package_.find(type_name);
        if (pkg_it != type_to_package_.end())
            e.package_name = pkg_it->second;
        out.push_back(std::move(e));
    }

    for (const auto& [path, diag] : abi_mismatch_by_path_) {
        OperatorMapEntry e;
        e.type_name = diag.plugin_name;
        e.dylib_path = diag.plugin_path;
        e.package_name = diag.package_name;
        e.status = "abi_mismatch";
        e.registration_mode = "unknown";
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
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = expected_operators_.find(type_name);
    if (it != expected_operators_.end()) return &it->second;
    return nullptr;
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
