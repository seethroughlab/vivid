#pragma once

#include <filesystem>
#include <string>

// Pure project-path conventions, dependency-free so they can be unit-tested headlessly
// (no App / audio / gpu). A "project" is either a legacy single JSON file (path ends in
// ".json") or a project FOLDER (any other path) holding "project.json" + co-located
// assets. Used by app/project_io.{h,cpp}.
namespace vivid::project_paths {

// True unless `path` ends in ".json" (a legacy single-file project).
inline bool is_folder_project(const std::string& path) {
    return std::filesystem::path(path).extension() != ".json";
}

// The session JSON file for a project path: "<dir>/project.json" for a folder project,
// else the path itself.
inline std::string session_json_path(const std::string& path) {
    return is_folder_project(path)
               ? (std::filesystem::path(path) / "project.json").string()
               : path;
}

}  // namespace vivid::project_paths
