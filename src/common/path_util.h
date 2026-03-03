#pragma once

#include <filesystem>
#include <string>

namespace vivid {

// Resolve a potentially-relative file path against a base directory.
// If path is relative and base_dir is non-empty, joins them.
// Canonicalizes only if the resolved path exists on disk.
// Returns absolute paths and empty strings unchanged.
inline std::string resolve_file_path(const std::string& path,
                                     const std::filesystem::path& base_dir) {
    if (path.empty()) return path;
    std::filesystem::path p(path);
    if (!p.is_relative() || base_dir.empty()) return path;
    auto resolved = base_dir / p;
    if (std::filesystem::exists(resolved))
        return std::filesystem::canonical(resolved).string();
    return resolved.lexically_normal().string();
}

// Convert an absolute path to relative (against base_dir) for persistence.
// Returns the path unchanged if:
//   - base_dir is empty (unsaved graph)
//   - path is already relative
//   - path is empty
//   - result would have more than 3 ".." segments
//   - paths are on different root/volume (Windows drive letters, macOS /Volumes)
inline std::string make_relative_path(const std::string& path,
                                      const std::filesystem::path& base_dir) {
    if (path.empty() || base_dir.empty()) return path;
    std::filesystem::path p(path);
    if (p.is_relative()) return path;

    // Different root/volume check
    if (p.root_path() != base_dir.root_path()) return path;

    auto rel = std::filesystem::proximate(p, base_dir);
    std::string rel_str = rel.string();

    // Count ".." segments — if too many, keep absolute
    int dotdot_count = 0;
    for (const auto& component : rel) {
        if (component == "..") ++dotdot_count;
    }
    if (dotdot_count > 3) return path;

    return rel_str;
}

} // namespace vivid
