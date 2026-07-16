#include "app/file_actions.h"

#include "app/app.h"
#include "app/window.h"
#include "app/edit_gateway.h"        // ADR-0018: mark_saved() on save/load/new
#include "app/autosave.h"            // ADR-0018: clear the autosave slot on a real save
#include "app/project_io.h"          // folder-aware save/load (+ project-local ops)
#include "platform/file_dialog.h"    // native open/save panels
#include "platform/menu_bar.h"       // set_recent_projects (refresh Open Recent)
#include "gpu/visual_graph.h"        // reset_to_default / set_asset_dir
#include "ui/node_graph.h"           // reset_nodes
#include "audio/vst3_host.h"         // session_* (clear clips on New)

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>

namespace vivid::file_actions {
namespace {

void refresh_recents(App& app) { platform::set_recent_projects(app.project.recent_project_paths); }

void load_path(GLFWwindow* w, Window& win, App& app, const std::string& path) {
    if (path.empty() || !app.session || !app.graph) return;
    int ww = win.win_w, wh = win.win_h; float sxx = win.split_x, dh = win.dock_h;
    auto lr = project_io::load(app, *app.graph, ww, wh, sxx, dh, path);
    // Restore the per-project internal layout (splitter/dock) but NOT the window size —
    // window size is app-level (see app/window_prefs.h), so opening a project won't resize.
    (void)w; (void)ww; (void)wh;
    if (lr.ok) {
        win.split_x = sxx; win.dock_h = dh; refresh_recents(app);
        if (app.edit_gateway) app.edit_gateway->mark_saved();   // ADR-0018: a freshly opened doc is clean
        VLOG_INFO(app, "opened %s", path.c_str());
    } else VLOG_ERR(app, "open failed: %s \xE2\x80\x94 %s", path.c_str(), lr.error.c_str());   // ADR-0019: toasts + logs
}

void save_path(Window& win, App& app, const std::string& path) {
    if (path.empty() || !app.session || !app.graph) return;
    auto sr = project_io::save(app, *app.graph, win.win_w, win.win_h, win.split_x, win.dock_h, path);
    if (sr.ok) {
        refresh_recents(app);
        if (app.edit_gateway) app.edit_gateway->mark_saved();   // ADR-0018: document is now clean
        autosave::clear();                                      // ADR-0018: a clean doc needs no recovery
        VLOG_INFO(app, "saved %s", path.c_str());
    } else VLOG_ERR(app, "save failed: %s \xE2\x80\x94 %s", path.c_str(), sr.error.c_str());   // ADR-0019: toasts + logs
}

}  // namespace

void new_project(App& app) {
    if (!app.session || !app.graph) return;
    const int nt = session::session_track_count(app.session);
    const int ns = session::session_scene_count(app.session);
    for (int t = 0; t < nt; ++t)
        for (int sc = 0; sc < ns; ++sc)
            session::session_set_clip(app.session, t, sc, nullptr, 0, 4.0);
    app.graph->reset_nodes();
    if (app.vgraph) { app.vgraph->reset_to_default(); app.vgraph->set_asset_dir(""); }
    app.project.current_project_path.clear();
    app.project.media_root.clear();
    app.project.missing_media.clear();
    if (app.edit_gateway) app.edit_gateway->mark_saved();   // ADR-0018: a new doc starts clean
}

void open(GLFWwindow* w, Window& win, App& app) { load_path(w, win, app, platform::open_project_dialog()); }

void open_recent(GLFWwindow* w, Window& win, App& app, const std::string& path) { load_path(w, win, app, path); }

void save(GLFWwindow* /*w*/, Window& win, App& app) {
    const std::string p = app.project.current_project_path.empty()
        ? platform::save_project_dialog("project.vivid.json")
        : app.project.current_project_path;
    save_path(win, app, p);
}

void save_as(GLFWwindow* /*w*/, Window& win, App& app) {
    save_path(win, app, platform::save_project_dialog("project.vivid.json"));
}

}  // namespace vivid::file_actions
