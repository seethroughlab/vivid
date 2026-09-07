#include "app/mcp_bridge.h"

#include "platform/platform.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace vivid::app {

namespace {
// A directory only counts as the bridge if vivid_mcp.py is actually in it — an empty or partial
// Resources/mcp should read as "not shipped", not as a path we hand the user.
bool has_bridge(const fs::path& d) {
    std::error_code ec;
    return fs::exists(d / "vivid_mcp.py", ec);
}
}  // namespace

std::string mcp_bridge_dir() {
    std::vector<fs::path> dirs;
    if (const char* env = std::getenv("VIVID_MCP_DIR"))
        dirs.emplace_back(env);
    const std::string exe = platform::executable_path();
    if (!exe.empty()) {
        const fs::path exe_dir = fs::path(exe).parent_path();
        dirs.push_back((exe_dir / ".." / "Resources" / "mcp").lexically_normal());  // macOS .app
        dirs.push_back((exe_dir / "mcp").lexically_normal());                       // non-bundle
    }
    for (const fs::path& d : dirs)
        if (has_bridge(d)) return d.string();
    return {};
}

std::string mcp_setup_command() {
    const std::string dir = mcp_bridge_dir();
    if (dir.empty()) return {};
    // `--script`, NOT `--directory`: `uv run --directory <dir> vivid_mcp.py` creates a `.venv` and
    // `__pycache__` INSIDE the directory it runs in — i.e. inside the app bundle, which is
    // unwritable under /Applications and would break the code signature where it isn't. Script mode
    // reads the PEP-723 header in vivid_mcp.py and uses uv's own cache instead, touching nothing in
    // the bundle. Quoted because an .app can live under a path with spaces.
    return "claude mcp add vivid -- uv run --script \"" + dir + "/vivid_mcp.py\"";
}

}  // namespace vivid::app
