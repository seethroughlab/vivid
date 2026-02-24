#ifndef VIVID_RUNTIME_OPERATOR_REGISTRY_H
#define VIVID_RUNTIME_OPERATOR_REGISTRY_H

#include "runtime/operator_loader.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace vivid {

class OperatorRegistry {
public:
    bool scan(const char* directory);
    OperatorLoader* find(const std::string& type_name);

    // Hot-reload support
    const std::string* type_name_for_target(const std::string& target) const;
    bool reload_operator(const std::string& type_name, const std::string& new_dylib_path);

private:
    std::unordered_map<std::string, std::unique_ptr<OperatorLoader>> loaders_;
    std::unordered_map<std::string, std::string> target_to_type_;  // cmake target → descriptor name
};

} // namespace vivid

#endif // VIVID_RUNTIME_OPERATOR_REGISTRY_H
