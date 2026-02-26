#include "runtime/operator_registry.h"
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace vivid {

static constexpr size_t kDylibSuffixLen = 6;  // strlen(".dylib")

bool OperatorRegistry::scan(const char* directory) {
    DIR* dir = opendir(directory);
    if (!dir) {
        std::fprintf(stderr, "[vivid] Registry: failed to open directory: %s\n", directory);
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = std::strlen(name);
        if (len < kDylibSuffixLen + 1 || std::strcmp(name + len - kDylibSuffixLen, ".dylib") != 0)
            continue;
        // Skip system/library dylibs (lib*.dylib); operators are name.dylib
        if (std::strncmp(name, "lib", 3) == 0)
            continue;

        std::string path = std::string(directory) + "/" + name;
        auto loader = std::make_unique<OperatorLoader>();
        if (!loader->load(path.c_str()))
            continue;

        const VividOperatorDescriptor* desc = loader->descriptor();
        std::string type_name = desc->name;
        std::fprintf(stderr, "[vivid] Registry: loaded %s from %s\n", type_name.c_str(), name);

        // Map cmake target name (filename stem) → descriptor type name
        std::string target_name(name, len - kDylibSuffixLen);  // strip ".dylib"
        target_to_type_[target_name] = type_name;

        loaders_[type_name] = std::move(loader);
    }

    closedir(dir);
    return true;
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
    if (it == loaders_.end())
        return nullptr;
    return it->second.get();
}

std::vector<std::string> OperatorRegistry::type_names() const {
    std::vector<std::string> names;
    names.reserve(loaders_.size());
    for (const auto& [name, _] : loaders_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

const std::string* OperatorRegistry::type_name_for_target(const std::string& target) const {
    auto it = target_to_type_.find(target);
    if (it == target_to_type_.end())
        return nullptr;
    return &it->second;
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

} // namespace vivid
