#include "app/file_actions.h"

#include "app/app.h"
#include "app/window.h"
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
    if (lr.ok) { win.split_x = sxx; win.dock_h = dh; refresh_recents(app); }
    std::fprintf(stderr, "[vivid] open %s: %s\n", path.c_str(), lr.ok ? "ok" : lr.error.c_str());
}

void save_path(Window& win, App& app, const std::string& path) {
    if (path.empty() || !app.session || !app.graph) return;
    auto sr = project_io::save(app, *app.graph, win.win_w, win.win_h, win.split_x, win.dock_h, path);
    if (sr.ok) refresh_recents(app);
    std::fprintf(stderr, "[vivid] save %s: %s\n", path.c_str(), sr.ok ? "ok" : sr.error.c_str());
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
