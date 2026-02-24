#include "runtime/operator_registry.h"
#include <dirent.h>
#include <cstring>
#include <cstdio>

namespace vivid {

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
        if (len < 7 || std::strcmp(name + len - 6, ".dylib") != 0)
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
        loaders_[type_name] = std::move(loader);
    }

    closedir(dir);
    return true;
}

OperatorLoader* OperatorRegistry::find(const std::string& type_name) {
    auto it = loaders_.find(type_name);
    if (it == loaders_.end())
        return nullptr;
    return it->second.get();
}

} // namespace vivid
