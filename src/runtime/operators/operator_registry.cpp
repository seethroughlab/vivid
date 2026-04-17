#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_descriptor_hash.h"
#include "runtime/platform/platform.h"

#include <cstdio>
#include <cstring>

namespace vivid {

std::string OperatorRegistry::descriptor_hash(const std::string& type_name) const {
    const VividOperatorDescriptor* desc = probe_descriptor(type_name);
    if (!desc) return {};
    return operator_descriptor_hash(desc);
}

OperatorRegistry::~OperatorRegistry() {
    deferred_.clear();
    loaders_.clear();
    retired_package_loaders_.clear();
}

void OperatorRegistry::register_target_mapping(const std::string& dylib_path,
                                               const std::string& type_name) {
    std::string filename = dylib_path;
    auto slash = filename.rfind('/');
    if (slash != std::string::npos) filename = filename.substr(slash + 1);
    size_t slen = std::strlen(kPluginSuffix);
    if (filename.size() > slen) filename = filename.substr(0, filename.size() - slen);
    target_to_type_[filename] = type_name;

    if (filename != type_name) {
        auto it = expected_operators_.find(filename);
        if (it != expected_operators_.end()) {
            expected_operators_[type_name] = it->second;
            expected_operators_.erase(it);
        }
    }
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

} // namespace vivid
