#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

class SourceIndex {
public:
    void set_checkout_root(std::string root);
    void set_bundled_root(std::string root);

    nlohmann::json list_roots() const;
    nlohmann::json search(const std::string& query,
                          const std::vector<std::string>& roots = {},
                          std::size_t limit = 20,
                          const std::vector<std::string>& file_types = {},
                          const std::vector<std::string>& path_globs = {}) const;
    nlohmann::json read_file(const std::string& path, std::size_t max_bytes = 200000) const;
    nlohmann::json read_span(const std::string& path, int start_line, int end_line) const;
    nlohmann::json find_symbol(const std::string& name,
                               const std::vector<std::string>& roots = {},
                               std::size_t limit = 20) const;
    nlohmann::json find_references(const std::string& name,
                                   const std::vector<std::string>& roots = {},
                                   std::size_t limit = 50) const;

private:
    struct ActiveRoot {
        std::string name;
        std::string path;
        std::string origin;
    };

    struct IndexedFile {
        std::string root_name;
        std::string origin;
        std::string rel_path;
        std::string abs_path;
        std::vector<std::string> lines;
    };

    std::string checkout_root_;
    std::string bundled_root_;

    mutable bool indexed_ = false;
    mutable std::vector<IndexedFile> files_;
    mutable std::unordered_map<std::string, std::size_t> file_index_by_rel_path_;

    void invalidate();
    std::vector<ActiveRoot> active_roots() const;
    void ensure_indexed() const;
};

}  // namespace vivid
