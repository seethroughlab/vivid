#include "app/app.h"
#include "app/edit_gateway.h"

#include <cstdio>
#include <filesystem>

namespace vivid {

void App::remember_project_path(const std::string& path) {
    project.remember_project_path(path);
}

// ADR-0017: the view-facing forwarder onto the undo sink (see the declaration in app.h).
void App::note_edit(const char* label, const char* coalesce_key) {
    if (edit_gateway && label) edit_gateway->note_edit(label, coalesce_key ? coalesce_key : "");
}

// Record the project's media root (the base a Video/Image node's relative path resolves against) and
// flag it if it is missing. Video decoding is now per-node (the self-decoding Video op owns its own
// file + decoder), so there is no shared clip list to scan here any more.
void App::set_media_root(const std::string& root) {
    namespace fs = std::filesystem;
    project.media_root = root;
    project.missing_media.clear();
    if (root.empty()) return;
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        project.missing_media.push_back(root);
        std::fprintf(stderr, "[vivid] media root unavailable: %s\n", root.c_str());
    }
}

}  // namespace vivid
