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

private:
    std::unordered_map<std::string, std::unique_ptr<OperatorLoader>> loaders_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_OPERATOR_REGISTRY_H
