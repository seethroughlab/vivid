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

void OperatorRegistry::clear_diagnostics_for_dir(const std::string& directory) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(fs::path(directory), ec);
    if (ec) dir = fs::path(directory).lexically_normal();
    const std::string dir_s = dir.string();
    const std::string dir_prefix = dir_s.empty() ? dir_s : (dir_s + "/");

    auto erase_matching = [&](auto& map) {
        for (auto it = map.begin(); it != map.end(); ) {
            fs::path plugin = fs::weakly_canonical(fs::path(it->first), ec);
            if (ec) {
                ec.clear();
                plugin = fs::path(it->first).lexically_normal();
            }
            const std::string plugin_s = plugin.string();
            const bool in_dir = plugin_s == dir_s || plugin_s.rfind(dir_prefix, 0) == 0;
            if (in_dir)
                it = map.erase(it);
            else
                ++it;
        }
    };
    erase_matching(abi_mismatch_by_path_);
    erase_matching(loader_failure_by_path_);
}

} // namespace vivid
