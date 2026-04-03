#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

class OperatorSourceDocs {
public:
    void set_core_source_root(std::string root);

    void invalidate_core();
    void invalidate_package(const std::string& package_name,
                            const std::string& package_root = "");

    nlohmann::json resolve_core(const std::string& operator_name);
    nlohmann::json resolve_package(const std::string& package_name,
                                   const std::string& package_root,
                                   const std::string& operator_name);

private:
    struct RootIndex {
        std::unordered_map<std::string, std::string> registrations;
        std::unordered_map<std::string, std::vector<std::string>> files_by_name;
        std::vector<std::string> searchable_files;
        bool indexed = false;
    };

    std::string core_source_root_;
    std::unordered_map<std::string, RootIndex> root_indexes_;
    std::unordered_map<std::string, nlohmann::json> cache_;

    nlohmann::json resolve(const std::string& cache_key,
                           const std::string& root,
                           const std::string& operator_name);
};

} // namespace vivid
