#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace vivid {

struct ProjectState {
    std::string current_project_path;
    std::vector<std::string> recent_project_paths;
    std::string media_root;
    std::vector<std::string> missing_media;

    void remember_project_path(const std::string& path) {
        if (path.empty()) return;
        current_project_path = path;
        recent_project_paths.erase(std::remove(recent_project_paths.begin(), recent_project_paths.end(), path),
                                   recent_project_paths.end());
        recent_project_paths.insert(recent_project_paths.begin(), path);
        if (recent_project_paths.size() > 8) recent_project_paths.resize(8);
    }
};

}  // namespace vivid
