#include "app/examples.h"

#include "platform/platform.h"   // executable_path, user_data_dir

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace vivid::examples {

std::vector<std::string> examples_search_path() {
    std::vector<std::string> dirs;
    if (const char* env = std::getenv("VIVID_EXAMPLES_DIR"))
        dirs.emplace_back(env);
    const std::string exe = platform::executable_path();
    if (!exe.empty()) {
        const fs::path exe_dir = fs::path(exe).parent_path();
        dirs.emplace_back((exe_dir / ".." / "Resources" / "examples").lexically_normal().string());
        dirs.emplace_back((exe_dir / "examples").lexically_normal().string());
    }
    return dirs;
}

namespace {
bool has_project(const fs::path& d) {
    std::error_code ec;
    return fs::exists(d / "project.json", ec);
}
}  // namespace

std::vector<Example> discover_examples_in(const std::string& dir) {
    std::vector<Example> out;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return out;
    for (const auto& ent : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!ent.is_directory(ec)) continue;
        if (has_project(ent.path())) {
            // A folder project directly under `dir` — a top-level (ungrouped) example.
            out.push_back({ ent.path().filename().string(), ent.path().string(), "" });
        } else {
            // No project.json here: treat this dir as a GROUP whose child folder-projects are its
            // examples (examples/<group>/<name>/project.json). One level deep only.
            std::error_code ec2;
            for (const auto& child : fs::directory_iterator(ent.path(), ec2)) {
                if (ec2) break;
                if (child.is_directory(ec2) && has_project(child.path()))
                    out.push_back({ child.path().filename().string(), child.path().string(),
                                    ent.path().filename().string() });
            }
        }
    }
    // Sort by (group, name): ungrouped (group=="") first, then each group's items, deterministic.
    std::sort(out.begin(), out.end(), [](const Example& a, const Example& b) {
        return a.group != b.group ? a.group < b.group : a.name < b.name;
    });
    return out;
}

std::vector<Example> discover_examples() {
    for (const auto& dir : examples_search_path()) {
        auto found = discover_examples_in(dir);
        if (!found.empty()) return found;   // one bundle wins; no cross-dir merge
    }
    return {};
}

std::string stage_openable_example(const std::string& project_dir) {
    std::error_code ec;
    const fs::path src(project_dir);
    if (!fs::exists(src / "vivid-package.json", ec))
        return project_dir;   // no project-local package — safe to open in place (read-only OK)

    const std::string data = platform::user_data_dir();
    if (data.empty()) return project_dir;   // no writable home; best-effort open in place
    const fs::path dst = fs::path(data) / "example-cache" / src.filename();
    fs::remove_all(dst, ec);                                  // faithful re-copy each open
    fs::create_directories(dst.parent_path(), ec);
    fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) return {};                                        // copy failed — signal to the caller
    return dst.string();
}

}  // namespace vivid::examples
