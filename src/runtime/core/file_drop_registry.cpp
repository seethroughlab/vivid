#include "runtime/core/file_drop_registry.h"
#include "runtime/operators/operator_registry.h"

#include <algorithm>
#include <cctype>

namespace vivid {

namespace {
std::string normalized_extension_from_path(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}
} // namespace

void FileDropRegistry::refresh(const OperatorRegistry& registry) {
    handlers_.clear();
    by_extension_.clear();

    for (const auto& reg : registry.file_drop_handlers()) {
        FileDropMatch match;
        match.type_name = reg.type_name;
        match.label = reg.label;
        match.file_param = reg.file_param;
        match.description = reg.description;
        match.package_name = reg.package_name;
        match.priority = reg.priority;
        handlers_.push_back(match);

        for (const auto& ext : reg.extensions)
            by_extension_[ext].push_back(match);
    }

    for (auto& [_, matches] : by_extension_) {
        std::sort(matches.begin(), matches.end(), [](const FileDropMatch& a, const FileDropMatch& b) {
            if (a.priority != b.priority) return a.priority > b.priority;
            if (a.label != b.label) return a.label < b.label;
            return a.type_name < b.type_name;
        });
    }
}

std::vector<FileDropMatch> FileDropRegistry::matches_for_path(const std::string& path) const {
    std::string ext = normalized_extension_from_path(path);
    auto it = by_extension_.find(ext);
    if (it == by_extension_.end()) return {};
    return it->second;
}

} // namespace vivid
