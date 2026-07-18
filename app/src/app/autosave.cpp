#include "app/autosave.h"

#include "app/app.h"
#include "persist.h"                 // save_session (the same serializer a real save uses)
#include "platform/platform.h"       // user_data_dir
#include "ui/node_graph.h"
#include "ui/audio_node_graph.h"      // App::audio_graph view getters (ADR-0023 6b)

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace vivid::autosave {
namespace fs = std::filesystem;
using nlohmann::json;

std::string dir() {
    // The path only — do NOT create it (check()/read paths must not litter an empty dir on launch;
    // write() creates it just-in-time). user_data_dir() itself is created by the platform layer.
    return (fs::path(platform::user_data_dir()) / "autosave").string();
}

static std::string session_path() { return (fs::path(dir()) / "session.json").string(); }
static std::string meta_path()    { return (fs::path(dir()) / "meta.json").string(); }

void write(App& app, int win_w, int win_h, float split_x, float dock_h, long long now_unix) {
    if (!app.session || !app.graph || !app.audio_graph) return;
    std::error_code ec; fs::create_directories(dir(), ec);   // create just-in-time on the first real autosave
    if (!save_session(session_path(), app.session, *app.graph, win_w, win_h, split_x, dock_h,
                      app.audio_graph->zoom(), app.audio_graph->pan_x(), app.audio_graph->pan_y()))
        return;   // best-effort; a failed autosave just means the last good one stands
    json m = { {"project", app.project.current_project_path}, {"saved_at", now_unix} };
    std::ofstream(meta_path(), std::ios::trunc) << m.dump(2);
}

void clear() {
    std::error_code ec;
    fs::remove(session_path(), ec);
    fs::remove(meta_path(), ec);
}

Recovery check() {
    Recovery r;
    std::error_code ec;
    const std::string sp = session_path();
    if (!fs::exists(sp, ec)) return r;
    r.session_path = sp;

    // Read the associated project path (tolerant of a missing/corrupt meta).
    std::ifstream mf(meta_path());
    if (mf) {
        json m = json::parse(mf, nullptr, false);
        if (m.is_object() && m.contains("project") && m["project"].is_string())
            r.project_path = m["project"].get<std::string>();
    }

    // Available unless the real project file is at least as new as the autosave (the user saved after
    // the last autosave, so there's nothing unsaved to recover). Untitled (no project) is always
    // recoverable.
    if (!r.project_path.empty()) {
        const fs::path proj = r.project_path;
        // A folder project stores its doc in project.json; compare against that.
        const fs::path proj_file = fs::is_directory(proj, ec) ? (proj / "project.json") : proj;
        if (fs::exists(proj_file, ec)) {
            const auto proj_t = fs::last_write_time(proj_file, ec);
            const auto auto_t = fs::last_write_time(sp, ec);
            if (!ec && proj_t >= auto_t) return r;   // project is up to date → r.available stays false
        }
    }
    r.available = true;
    return r;
}

}  // namespace vivid::autosave
