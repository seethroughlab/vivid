#include "app/examples.h"

#include "platform/platform.h"   // executable_path

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

std::vector<Example> discover_examples_in(const std::string& dir) {
    std::vector<Example> out;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return out;
    for (const auto& ent : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!ent.is_directory(ec)) continue;
        const fs::path proj = ent.path() / "project.json";
        if (!fs::exists(proj, ec)) continue;
        out.push_back({ ent.path().filename().string(), ent.path().string() });
    }
    std::sort(out.begin(), out.end(), [](const Example& a, const Example& b) { return a.name < b.name; });
    return out;
}

std::vector<Example> discover_examples() {
    for (const auto& dir : examples_search_path()) {
        auto found = discover_examples_in(dir);
        if (!found.empty()) return found;   // one bundle wins; no cross-dir merge
    }
    return {};
}

}  // namespace vivid::examples
