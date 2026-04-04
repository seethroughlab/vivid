#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_registry_internal.h"

#include <algorithm>

namespace vivid {

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
    return operator_registry_internal::diagnostics_for_dir(abi_mismatch_by_path_, directory);
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
    return operator_registry_internal::diagnostics_for_dir(loader_failure_by_path_, directory);
}

bool OperatorRegistry::has_loader_failure_diagnostics() const {
    return !loader_failure_by_path_.empty();
}

void OperatorRegistry::record_loader_failure(const std::string& plugin_path,
                                             const std::string& plugin_name,
                                             const OperatorLoader::LastError& error) {
    LoaderFailureDiagnostic diag;
    diag.plugin_path = plugin_path;
    diag.plugin_name = plugin_name;
    diag.package_name = operator_registry_internal::guess_package_name_from_plugin_path(plugin_path);
    diag.code = error.code.empty() ? "load_failed" : error.code;
    diag.message = error.message.empty() ? "operator load failed" : error.message;
    loader_failure_by_path_[plugin_path] = std::move(diag);
    auto prov_it = expected_operators_.find(plugin_name);
    if (prov_it != expected_operators_.end()) {
        prov_it->second.load_failed = true;
        prov_it->second.failure_detail = diag.message;
    }
}

} // namespace vivid
