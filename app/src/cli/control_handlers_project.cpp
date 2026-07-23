#include "cli/control_handlers_internal.h"

#include "persist.h"
#include "app/project_io.h"   // folder-aware save/load + project-local operators
#include "app/project_paths.h"   // is_folder_project / session_json_path
#include "app/app.h"
#include "ui/node_graph.h"
#include "ui/audio_node_graph.h"   // App::audio_graph view (ADR-0023 6b: file save/load round-trips it)
#include "gpu/visual_graph.h"
#include "gpu/shader_library.h"        // set_project (ADR-0024 Phase 7: reload_project_files)
#include "gpu/operator_scan.h"         // load_and_register_operator
#include "packages/package_manager.h"  // install_package
#include "packages/package_manifest.h" // parse_package_manifest (list_project_assets)

#include <filesystem>
#include <fstream>
#include <string>

namespace vivid {
namespace {

namespace fs = std::filesystem;

// The project's asset directory: the folder for a folder-project, else a .json's parent dir.
// Empty when the project is unsaved (no path yet).
std::string project_asset_dir(App* app) {
    const std::string& p = app->project.current_project_path;
    if (p.empty()) return std::string();
    return vivid::project_paths::is_folder_project(p)
               ? p
               : fs::path(p).parent_path().string();
}

// Classify an asset file by extension for the project-asset tools.
std::string asset_kind(const fs::path& p) {
    const std::string ext = p.extension().string();
    if (ext == ".glsl" || ext == ".wgsl" || ext == ".frag" || ext == ".vert") return "shader";
    if (ext == ".cpp" || ext == ".cc" || ext == ".mm") return "operator_source";
    if (ext == ".json") return (p.filename() == "vivid-package.json") ? "package_manifest" : "session";
    if (ext == ".mp4" || ext == ".mov" || ext == ".webm") return "video";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") return "image";
    if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3") return "audio";
    return "other";
}

// A coarse structural fingerprint of a session JSON (counts + identity that carry no plugin-state
// blob, so an unsaved-vs-disk diff is not swamped by opaque plugin bytes). Used by diff_project.
json session_structure(const json& j) {
    json s;
    s["scenes"] = j.value("scenes", 3);
    const json& tracks = j.value("tracks", json::array());
    s["track_count"] = static_cast<int>(tracks.size());
    json tnames = json::array();
    int audio_node_total = 0;
    for (const auto& t : tracks) {
        tnames.push_back(t.value("name", std::string()));
        if (t.contains("audio_graph"))
            audio_node_total += static_cast<int>(t["audio_graph"].value("nodes", json::array()).size());
    }
    s["track_names"] = tnames;
    s["audio_node_total"] = audio_node_total;
    const json& g = j.value("graph", json::object());
    json titles = json::array();
    for (const auto& n : g.value("nodes", json::array())) titles.push_back(n.value("title", std::string()));
    s["bridge_node_titles"] = titles;
    s["mapping_count"] = static_cast<int>(g.value("mappings", json::array()).size());
    return s;
}

}  // namespace

// Session author / persist + project workflow (a thin folder-aware layer over the session JSON):
// save/load, new project, project status, save/load/set-media-root.
void register_project_handlers(Handlers& handlers_) {
    namespace P = vivid::session;
    // ---------------- session author / persist ----------------
    handlers_["save_session"] = [](const ControlCtx& c, const json& b) {
        if (!c.session || !c.graph || !c.app || !c.app->audio_graph) return err(code::kNoSession, "no session");
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "need path");
        const auto& av = c.app->audio_graph->view();
        return save_session(path, c.session, *c.graph, *c.win_w, *c.win_h, *c.split_x, *c.dock_h,
                            av.ox, av.oy, av.scale)
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
        float aox = 0.f, aoy = 0.f, ascale = 0.f;   // scale 0 = sentinel: no camera in the file
        const bool okr = load_session(path, c.session, *c.graph, ww, wh, *c.split_x, *c.dock_h, aox, aoy, ascale);
        if (okr && ascale > 0.f) ag->set_view({ aox, aoy, ascale });   // restore the persisted camera (ADR-0023)
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
        if (c.vgraph) { c.vgraph->reset_to_default(); c.vgraph->set_asset_dir(""); }  // clean canvas: just Output
        if (c.app) {
            c.app->shader_library.set_project(c.app->op_registry, "");   // drop project-scoped operators
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
        json r = ok(); r["media_root"] = c.app->project.media_root;
        r["missing_media"] = c.app->project.missing_media; return r;
    };
    // (set_video_source was retired: video is now per-node — set the Video node's `file` param.)

    // ===================== ADR-0024 Phase 7: project workflow =====================
    // Read-only-ish tooling over the current project's on-disk folder — so an agent can inspect,
    // resolve, and refresh a project's assets without guessing paths. All compose project_io /
    // the folder conventions; none touch perception.

    // validate_project — structural health of the loaded project: is it saved, does its session file
    // exist on disk, does it carry a project-local operator package, and is any referenced media
    // missing. `valid` is false only for a hard problem (missing session file / missing media).
    handlers_["validate_project"] = [](const ControlCtx& c, const json&) {
        if (!c.app || !c.session) return err(code::kNoSession, "no session");
        const std::string path = c.app->project.current_project_path;
        json issues = json::array();
        json r = ok();
        r["path"] = path;
        r["saved"] = !path.empty();
        if (path.empty()) {
            issues.push_back({ {"level", "info"}, {"issue", "project is unsaved (in-memory only)"} });
            r["valid"] = true;   // an unsaved project is a valid state, just not on disk yet
        } else {
            const bool folder = vivid::project_paths::is_folder_project(path);
            r["is_folder_project"] = folder;
            const std::string dir = project_asset_dir(c.app);
            const std::string jpath = vivid::project_paths::session_json_path(path);
            std::error_code ec;
            const bool session_exists = fs::exists(jpath, ec);
            r["session_file"] = jpath;
            r["session_file_exists"] = session_exists;
            if (!session_exists)
                issues.push_back({ {"level", "error"}, {"issue", "session file missing on disk"}, {"path", jpath} });
            const bool has_pkg = folder && fs::exists(fs::path(dir) / "vivid-package.json", ec);
            r["has_package"] = has_pkg;
            const bool has_shaders = folder && fs::is_directory(fs::path(dir) / "shaders", ec);
            r["has_shaders_dir"] = has_shaders;
            r["valid"] = session_exists;
        }
        for (const auto& m : c.app->project.missing_media)
            issues.push_back({ {"level", "error"}, {"issue", "missing media"}, {"path", m} });
        if (!c.app->project.missing_media.empty()) r["valid"] = false;
        r["issues"] = issues;
        r["summary"] = r.value("valid", false)
                           ? std::string("project is valid (") + std::to_string(issues.size()) + " note(s))"
                           : std::string("project has ") + std::to_string(issues.size()) + " issue(s)";
        return r;
    };

    // list_project_assets — enumerate the current project folder's co-located assets: the session
    // file, the operator package (manifest + declared operator sources), any shaders/ operators, and
    // loose media. Empty (with a note) for an unsaved or single-file project.
    handlers_["list_project_assets"] = [](const ControlCtx& c, const json&) {
        if (!c.app) return err(code::kInternal, "no app context");
        json r = ok();
        const std::string path = c.app->project.current_project_path;
        r["path"] = path;
        if (path.empty()) { r["assets"] = json::array(); r["note"] = "project is unsaved — no on-disk assets"; return r; }
        if (!vivid::project_paths::is_folder_project(path)) {
            r["assets"] = json::array({ { {"kind", "session"}, {"path", path}, {"name", fs::path(path).filename().string()} } });
            r["note"] = "single-file project — assets are not co-located";
            return r;
        }
        const std::string dir = path;
        std::error_code ec;
        json assets = json::array();
        auto add = [&](const fs::path& p, const std::string& kind) {
            assets.push_back({ {"kind", kind}, {"name", fs::relative(p, dir, ec).string()},
                               {"path", p.string()} });
        };
        // The package manifest is authoritative for operator sources (a source may be authored but
        // not yet a loose enumerable; the manifest names exactly what the project ships).
        json package;
        const fs::path manifest = fs::path(dir) / "vivid-package.json";
        if (fs::exists(manifest, ec)) {
            PackageManifest pm = parse_package_manifest(dir);
            package["present"] = true;
            package["ok"] = pm.ok;
            if (!pm.ok) package["error"] = pm.error;
            package["name"] = pm.name;
            json ops = json::array();
            for (const auto& op : pm.operators)
                ops.push_back({ {"name", op.name}, {"source", op.source}, {"kind", op.kind} });
            package["operators"] = ops;
            add(manifest, "package_manifest");
        } else {
            package["present"] = false;
        }
        r["package"] = package;
        // Loose files in the project dir + its shaders/ subdir.
        for (const auto& base : { fs::path(dir), fs::path(dir) / "shaders" }) {
            if (!fs::is_directory(base, ec)) continue;
            for (const auto& e : fs::directory_iterator(base, ec)) {
                if (!e.is_regular_file(ec)) continue;
                const std::string kind = asset_kind(e.path());
                if (kind == "package_manifest") continue;   // already added above
                add(e.path(), kind);
            }
        }
        r["assets"] = assets;
        r["count"] = static_cast<int>(assets.size());
        return r;
    };

    // resolve_asset — resolve a project-relative asset reference (e.g. "shaders/foo.glsl" or a bare
    // filename) to an absolute path, reporting whether it exists and its kind. Rejects escapes above
    // the project dir. Mirrors how a node's relative `asset` resolves against the project folder.
    handlers_["resolve_asset"] = [](const ControlCtx& c, const json& b) {
        if (!c.app) return err(code::kInternal, "no app context");
        const std::string ref = b.value("asset", b.value("name", std::string()));
        if (ref.empty()) return err(code::kBadArg, "resolve_asset needs \"asset\" (a project-relative path or filename)");
        const std::string dir = project_asset_dir(c.app);
        if (dir.empty()) return err(code::kBadArg, "project is unsaved — nothing to resolve against");
        std::error_code ec;
        const fs::path abs = fs::weakly_canonical(fs::path(dir) / ref, ec);
        const fs::path root = fs::weakly_canonical(fs::path(dir), ec);
        // Containment check: the resolved path must stay inside the project dir.
        const std::string abss = abs.string(), roots = root.string();
        if (abss.rfind(roots, 0) != 0)
            return err(code::kBadArg, "asset resolves outside the project directory");
        json r = ok();
        r["asset"] = ref;
        r["path"] = abs.string();
        r["exists"] = fs::exists(abs, ec);
        r["kind"] = asset_kind(abs);
        return r;
    };

    // reload_project_files — pick up on-disk edits to the project's authored assets without reverting
    // the live session: re-scan the shaders/ dir (registers newly-added shader operators; existing
    // ones already hot-reload) and, if the project ships a package, recompile it and register any
    // operator not yet live. Existing compiled operators are hot-swapped via reload_operator_package.
    handlers_["reload_project_files"] = [](const ControlCtx& c, const json&) {
        if (!c.app) return err(code::kInternal, "no app context");
        const std::string path = c.app->project.current_project_path;
        if (path.empty() || !vivid::project_paths::is_folder_project(path))
            return err(code::kBadArg, "reload_project_files needs a saved folder project");
        const std::string dir = path;
        std::error_code ec;
        json r = ok();
        r["path"] = dir;
        // Shaders: re-scan the project's shaders (registers new files; hot-reload handles edits live).
        const int shader_ops = c.app->shader_library.set_project(c.app->op_registry, dir);
        r["shader_operators"] = shader_ops;
        // Package: recompile + register any newly-authored operator.
        json ops = json::array();
        int registered = 0;
        if (fs::exists(fs::path(dir) / "vivid-package.json", ec)) {
            PackageInstallResult ir = install_package(dir, dir);   // compile into the project folder
            if (!ir.ok) return err(code::kBadArg, "package: " + ir.error);
            r["package"] = ir.name;
            for (const auto& ci : ir.compiles) {
                json jo = { {"name", ci.op_name}, {"compiled", ci.success} };
                if (!ci.success) { jo["error"] = ci.error_output; }
                else {
                    const std::string reg = load_and_register_operator(ci.dylib_path, c.app->op_registry, c.app->op_loaders);
                    jo["registered"] = !reg.empty();
                    if (!reg.empty()) { jo["op"] = reg; ++registered; }
                    else jo["note"] = "compiled but already registered — use reload_operator_package to hot-swap";
                }
                ops.push_back(jo);
            }
        }
        r["operators"] = ops;
        r["newly_registered"] = registered;
        r["summary"] = "reloaded project files: " + std::to_string(shader_ops) + " shader op(s), " +
                       std::to_string(registered) + " new package op(s)";
        return r;
    };

    // diff_project — structural delta between the live (in-memory) session and its last saved state on
    // disk. Reports per-section counts on each side + a `differs` flag, deliberately excluding opaque
    // plugin-state blobs so the diff reflects authored structure, not serialized plugin bytes.
    handlers_["diff_project"] = [](const ControlCtx& c, const json&) {
        if (!c.session || !c.graph || !c.app) return err(code::kNoSession, "no session");
        const std::string path = c.app->project.current_project_path;
        if (path.empty()) return err(code::kBadArg, "project is unsaved — nothing on disk to diff against");
        const std::string jpath = vivid::project_paths::session_json_path(path);
        std::error_code ec;
        if (!fs::exists(jpath, ec)) return err(code::kNotFound, "saved session file not found: " + jpath);
        std::ifstream f(jpath);
        json disk;
        try { f >> disk; } catch (const std::exception& e) { return err(code::kIoError, std::string("parse saved session: ") + e.what()); }
        const json live_full = session_to_json(c.session, *c.graph, *c.win_w, *c.win_h, *c.split_x, *c.dock_h,
                                                /*include_plugin_state=*/false);
        const json live = session_structure(live_full);
        const json base = session_structure(disk);
        json changes = json::array();
        for (const auto& key : { "scenes", "track_count", "audio_node_total", "mapping_count" })
            if (live.value(key, -1) != base.value(key, -1))
                changes.push_back({ {"field", key}, {"disk", base.value(key, 0)}, {"live", live.value(key, 0)} });
        if (live.value("track_names", json::array()) != base.value("track_names", json::array()))
            changes.push_back({ {"field", "track_names"}, {"disk", base.value("track_names", json::array())},
                                {"live", live.value("track_names", json::array())} });
        if (live.value("bridge_node_titles", json::array()) != base.value("bridge_node_titles", json::array()))
            changes.push_back({ {"field", "bridge_node_titles"} });
        json r = ok();
        r["path"] = jpath;
        r["disk"] = base;
        r["live"] = live;
        r["changes"] = changes;
        r["differs"] = !changes.empty();
        r["summary"] = changes.empty() ? "live session matches the saved project (structurally)"
                                        : std::to_string(changes.size()) + " structural difference(s) vs saved project";
        return r;
    };
}

}  // namespace vivid
