#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

class OperatorRegistry;

struct FileDropMatch {
    std::string type_name;
    std::string label;
    std::string file_param;
    std::string description;
    std::string package_name;
    int32_t priority = 0;
};

class FileDropRegistry {
public:
    void refresh(const OperatorRegistry& registry);
    std::vector<FileDropMatch> matches_for_path(const std::string& path) const;
    const std::vector<FileDropMatch>& all_registered_handlers() const { return handlers_; }

private:
    std::vector<FileDropMatch> handlers_;
    std::unordered_map<std::string, std::vector<FileDropMatch>> by_extension_;
};

} // namespace vivid
