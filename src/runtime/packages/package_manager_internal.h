#pragma once

#include "runtime/packages/package_manager.h"

#include <array>
#include <string>
#include <unordered_set>
#include <vector>

namespace vivid::package_manager_internal {

struct ScopeRoot {
    std::string scope;
    std::string root;
    int precedence = 0;
};

struct PackageCandidate {
    PackageInfo info;
    std::string source_scope;
    std::string scope_root;
    int precedence = 0;
    bool invalid_same_scope = false;
};

std::string quote(const std::string& s);
std::string trim_copy(const std::string& s);
std::vector<std::string> split_path_list(const std::string& s);
std::string try_normalize_dir(const std::string& p);
std::string discover_workspace_root();
void append_scope_root(std::vector<ScopeRoot>& roots,
                       std::unordered_set<std::string>& seen,
                       const std::string& scope,
                       const std::string& path,
                       int precedence);
std::vector<ScopeRoot> discover_scope_roots();
bool parse_semver_triplet(const std::string& raw,
                          std::array<int, 3>& out,
                          bool& is_prerelease);
bool compare_semver(const std::string& a, const std::string& b, int& cmp);
bool eval_constraint_cmp(int cmp, const std::string& op);
bool is_core_version_compatible(const std::string& core_version,
                                const std::string& vivid_core_range,
                                bool& constraint_valid);
std::string diagnose_non_package_dir(const std::string& dir);

} // namespace vivid::package_manager_internal
