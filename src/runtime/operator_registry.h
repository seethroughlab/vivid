#ifndef VIVID_RUNTIME_OPERATOR_REGISTRY_H
#define VIVID_RUNTIME_OPERATOR_REGISTRY_H

#include "runtime/operator_loader.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace vivid {

struct DataDrivenFilterConfig;

class OperatorRegistry {
public:
    bool scan(const char* directory);
    void register_builtin(const std::string& type_name,
                          VividDescriptorFn, VividCreateFn, VividDestroyFn, VividProcessFn);
    OperatorLoader* find(const std::string& type_name);

    // User-defined filter management
    void register_user_filter(const std::string& name,
                              std::shared_ptr<DataDrivenFilterConfig> config);
    void unregister_user_filter(const std::string& name);
    bool is_user_filter(const std::string& name) const;

    // User-defined C++ operator management
    void register_user_operator(const std::string& name, const std::string& source_path);
    bool is_user_operator(const std::string& name) const;
    const std::string* user_operator_source(const std::string& name) const;

    // Load a brand-new operator from a dylib (for cloned operators)
    bool register_loaded_operator(const std::string& dylib_path);

    // Introspection
    std::vector<std::string> type_names() const;

    // Hot-reload support
    const std::string* type_name_for_target(const std::string& target) const;
    bool reload_operator(const std::string& type_name, const std::string& new_dylib_path);

private:
    std::unordered_map<std::string, std::unique_ptr<OperatorLoader>> loaders_;
    std::unordered_map<std::string, std::string> target_to_type_;  // cmake target → descriptor name
    std::unordered_set<std::string> user_filter_types_;
    std::unordered_map<std::string, std::string> user_operator_sources_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_OPERATOR_REGISTRY_H
