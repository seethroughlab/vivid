#include "runtime/workspace_manager.h"
#include "runtime/graph_file_io.h"
#include "runtime/platform.h"
#include <cstdio>
#include <cstdlib>

namespace vivid {

std::filesystem::path default_workspace_root() {
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0')
        return std::filesystem::path(home) / "Documents" / "Vivid";
    return std::filesystem::path(vivid::get_config_dir()) / "workspace";
}



bool resolve_scaffold_destination(const std::string& destination,
                                         const std::string& source_dir,
                                         vivid::PackageManager& pm,
                                         const vivid::Settings* settings,
                                         ScaffoldDestination& out,
                                         std::string& error) {
    vivid::OperatorDestination resolved;
    if (!vivid::resolve_operator_destination(destination, source_dir, pm.list(), settings,
                                             resolved, error)) {
        return false;
    }
    out.root = resolved.root;
    out.package_layout = resolved.package_layout;
    out.package_name = resolved.package_name;
    out.warning = resolved.warning;
    return true;
}

bool copy_tree_missing(const std::filesystem::path& src,
                              const std::filesystem::path& dst) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(src, ec)) return false;
    fs::create_directories(dst, ec);
    if (ec) return false;

    for (const auto& entry : fs::recursive_directory_iterator(src, ec)) {
        if (ec) return false;
        auto rel = fs::relative(entry.path(), src, ec);
        if (ec) return false;
        auto out = dst / rel;
        if (entry.is_directory()) {
            fs::create_directories(out, ec);
            if (ec) return false;
            continue;
        }
        if (!entry.is_regular_file()) continue;
        if (fs::exists(out, ec)) continue;  // non-destructive: never overwrite user files
        fs::create_directories(out.parent_path(), ec);
        if (ec) return false;
        fs::copy_file(entry.path(), out, fs::copy_options::none, ec);
        if (ec) return false;
    }
    return true;
}

// Copies src → dst, overwriting files where src is strictly newer.
// Used to propagate bundled asset updates into the workspace without
// clobbering files that the user has modified more recently.
bool copy_tree_overwrite_newer(const std::filesystem::path& src,
                                      const std::filesystem::path& dst) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(src, ec)) return false;
    fs::create_directories(dst, ec);
    if (ec) return false;

    for (const auto& entry : fs::recursive_directory_iterator(src, ec)) {
        if (ec) return false;
        auto rel = fs::relative(entry.path(), src, ec);
        if (ec) return false;
        auto out = dst / rel;
        if (entry.is_directory()) {
            fs::create_directories(out, ec);
            if (ec) return false;
            continue;
        }
        if (!entry.is_regular_file()) continue;
        fs::create_directories(out.parent_path(), ec);
        if (ec) return false;
        if (fs::exists(out, ec)) {
            auto src_time = fs::last_write_time(entry.path(), ec);
            if (ec) continue;
            auto dst_time = fs::last_write_time(out, ec);
            if (ec) {
                // dst exists but can't read time — skip to be safe
                continue;
            }
            if (src_time <= dst_time) continue;  // dst is same age or newer — keep it
        }
        fs::copy_file(entry.path(), out, fs::copy_options::overwrite_existing, ec);
        // Non-fatal: log but continue if one file fails
        if (ec) {
            std::fprintf(stderr, "[vivid] Workspace sync warning: failed to update %s\n",
                         out.string().c_str());
            ec.clear();
        }
    }
    return true;
}

bool ensure_workspace_seeded(const std::filesystem::path& resources_dir,
                                    vivid::Settings& settings,
                                    std::filesystem::path& workspace_root) {
    namespace fs = std::filesystem;
    bool settings_changed = false;

    if (settings.workspace_root.empty()) {
        settings.workspace_root = default_workspace_root().string();
        settings_changed = true;
    }
    workspace_root = expand_tilde_path(settings.workspace_root);
    if (workspace_root.empty()) {
        workspace_root = default_workspace_root();
    }
    std::string normalized_root = workspace_root.lexically_normal().string();
    if (normalized_root != settings.workspace_root) {
        settings.workspace_root = normalized_root;
        settings_changed = true;
    }

    fs::path src_graphs = resources_dir / "graphs";
    fs::path src_assets = resources_dir / "assets";
    fs::path dst_graphs = workspace_root / "graphs";
    fs::path dst_assets = workspace_root / "assets";

    std::error_code ec;

    // Always sync bundle → workspace for files where the bundle copy is
    // strictly newer.  This runs every launch so that dev rebuilds pick up
    // new/updated graphs and assets without requiring a version bump.
    if (fs::is_directory(src_graphs, ec))
        copy_tree_overwrite_newer(src_graphs, dst_graphs);
    if (fs::is_directory(src_assets, ec))
        copy_tree_overwrite_newer(src_assets, dst_assets);

    bool needs_seed =
        settings.workspace_seeded_version != VIVID_CORE_VERSION ||
        !fs::is_directory(dst_graphs, ec) ||
        !fs::is_directory(dst_assets, ec);

    if (!needs_seed) return settings_changed;

    // First-time seed: copy everything that doesn't already exist.
    bool seed_ok = true;
    if (fs::is_directory(src_graphs, ec)) {
        if (!copy_tree_missing(src_graphs, dst_graphs)) {
            std::fprintf(stderr, "[vivid] Workspace seed warning: failed to copy graphs to %s\n",
                         dst_graphs.string().c_str());
            seed_ok = false;
        }
    } else {
        std::fprintf(stderr, "[vivid] Workspace seed warning: missing bundled graphs at %s\n",
                     src_graphs.string().c_str());
        seed_ok = false;
    }
    if (fs::is_directory(src_assets, ec)) {
        if (!copy_tree_missing(src_assets, dst_assets)) {
            std::fprintf(stderr, "[vivid] Workspace seed warning: failed to copy assets to %s\n",
                         dst_assets.string().c_str());
            seed_ok = false;
        }
    } else {
        std::fprintf(stderr, "[vivid] Workspace seed warning: missing bundled assets at %s\n",
                     src_assets.string().c_str());
        seed_ok = false;
    }

    if (seed_ok) {
        settings.workspace_seeded_version = VIVID_CORE_VERSION;
        settings_changed = true;
    }

    return settings_changed;
}

} // namespace vivid
