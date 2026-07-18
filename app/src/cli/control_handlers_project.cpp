#include "cli/control_handlers_internal.h"

#include "persist.h"
#include "app/project_io.h"   // folder-aware save/load + project-local operators
#include "app/app.h"
#include "ui/node_graph.h"
#include "ui/audio_node_graph.h"   // App::audio_graph view (ADR-0023 6b: file save/load round-trips it)
#include "gpu/visual_graph.h"

#include <string>

namespace vivid {

// Session author / persist + project workflow (a thin folder-aware layer over the session JSON):
// save/load, new project, project status, save/load/set-media-root.
void register_project_handlers(Handlers& handlers_) {
    namespace P = vivid::session;
    // ---------------- session author / persist ----------------
    handlers_["save_session"] = [](const ControlCtx& c, const json& b) {
        if (!c.session || !c.graph || !c.app || !c.app->audio_graph) return err(code::kNoSession, "no session");
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "need path");
        auto* ag = c.app->audio_graph;
        return save_session(path, c.session, *c.graph, *c.win_w, *c.win_h, *c.split_x, *c.dock_h,
                            ag->zoom(), ag->pan_x(), ag->pan_y())
                   ? ok() : err(code::kIoError, "write failed");
    };
    handlers_["load_session"] = [](const ControlCtx& c, const json& b) {
        if (!c.session || !c.graph || !c.app || !c.app->audio_graph) return err(code::kNoSession, "no session");
        int ww = *c.win_w, wh = *c.win_h;   // don't resize the window via MCP
        if (b.contains("session")) {        // inline JSON
            return session_from_json(b["session"], c.session, *c.graph, ww, wh, *c.split_x, *c.dock_h)
                       ? ok() : err(code::kBadArg, "load failed");
        }
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "need path or session");
        auto* ag = c.app->audio_graph;
        float az = ag->zoom(), apx = ag->pan_x(), apy = ag->pan_y();
        const bool okr = load_session(path, c.session, *c.graph, ww, wh, *c.split_x, *c.dock_h, az, apx, apy);
        if (okr) ag->set_view(az, apx, apy);   // restore the persisted audio-graph view (ADR-0023 6b)
        return okr ? ok() : err(code::kIoError, "read failed");
    };

    // ---------------- project workflow (thin layer over session JSON) ----------------
    // Fresh start: empty every clip, reset the visuals to the default chain, drop mappings +
    // data nodes, and clear the current-project pointer. Keeps the loaded instruments + tracks.
    handlers_["new_project"] = [](const ControlCtx& c, const json&) {
        if (!c.session || !c.graph) return err(code::kNoSession, "no session");
        const int nt = P::session_track_count(c.session), ns = P::session_scene_count(c.session);
        for (int t = 0; t < nt; ++t)
            for (int sc = 0; sc < ns; ++sc)
                P::session_set_clip(c.session, t, sc, nullptr, 0, 4.0);
        c.graph->reset_nodes();                        // data nodes + mappings
        if (c.vgraph) { c.vgraph->reset_to_default(); c.vgraph->set_asset_dir(""); }  // Plasma->Feedback->Blur->Output
        if (c.app) {
            c.app->project.current_project_path.clear();
            c.app->project.media_root.clear();
            c.app->project.missing_media.clear();
        }
        json r = ok(); r["tracks"] = nt; return r;
    };
    handlers_["get_project_status"] = [](const ControlCtx& c, const json&) {
        if (!c.app) return err(code::kInternal, "no app context");
        json r = ok();
        r["path"] = c.app->project.current_project_path;
        r["media_root"] = c.app->project.media_root;
        r["recent"] = c.app->project.recent_project_paths;
        r["missing_media"] = c.app->project.missing_media;
        r["videos"] = static_cast<int>(c.app->video_paths.size());
        return r;
    };
    handlers_["save_project"] = [](const ControlCtx& c, const json& b) {
        if (!c.app || !c.session || !c.graph) return err(code::kNoSession, "no session");
        const std::string path = b.value("path", c.app->project.current_project_path);
        if (path.empty()) return err(code::kBadArg, "need path for first save");
        auto sr = project_io::save(*c.app, *c.graph, *c.win_w, *c.win_h, *c.split_x, *c.dock_h, path);
        if (!sr.ok) return err(code::kIoError, sr.error);
        json r = ok(); r["path"] = c.app->project.current_project_path; r["session_file"] = sr.session_file; return r;
    };
    handlers_["load_project"] = [](const ControlCtx& c, const json& b) {
        if (!c.app || !c.session || !c.graph) return err(code::kNoSession, "no session");
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "need path");
        int ww = *c.win_w, wh = *c.win_h;
        auto lr = project_io::load(*c.app, *c.graph, ww, wh, *c.split_x, *c.dock_h, path);
        if (!lr.ok) {
            VLOG_ERR(*c.app, "load failed: %s \xE2\x80\x94 %s", path.c_str(), lr.error.c_str());  // ADR-0019: surface in-app
            return err(code::kIoError, lr.error);
        }
        json r = ok(); r["path"] = c.app->project.current_project_path;
        if (lr.had_package) {   // report project-local operator compile/register outcomes
            json pkg = { {"name", lr.package_name}, {"registered", lr.registered} };
            json ops = json::array();
            for (const auto& o : lr.ops) {
                json jo = { {"name", o.name}, {"compiled", o.compiled}, {"registered", o.registered} };
                if (!o.note.empty())  jo["note"] = o.note;
                if (!o.error.empty()) jo["error"] = o.error;
                ops.push_back(jo);
            }
            pkg["operators"] = ops;
            r["project_package"] = pkg;
        }
        return r;
    };
    handlers_["set_media_root"] = [](const ControlCtx& c, const json& b) {
        if (!c.app) return err(code::kInternal, "no app context");
        c.app->set_media_root(b.value("path", std::string()));
        json r = ok(); r["media_root"] = c.app->project.media_root; r["videos"] = static_cast<int>(c.app->video_paths.size());
        r["missing_media"] = c.app->project.missing_media; return r;
    };
}

}  // namespace vivid
