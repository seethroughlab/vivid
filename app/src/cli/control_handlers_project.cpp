#include "cli/control_handlers_internal.h"
#include "cli/project_recovery.h"   // ADR-0040 P3: pure beginner-recovery diagnostics (plugin/package)

#include "persist.h"
#include "app/project_io.h"   // folder-aware save/load + project-local operators
#include "app/project_paths.h"   // is_folder_project / session_json_path
#include "app/app.h"
#include "audio/plugin_catalog.h"       // check_tutorial_prereqs: installed plugin readiness
#include "ui/node_graph.h"
#include "ui/audio_node_graph.h"   // App::audio_graph view (ADR-0023 6b: file save/load round-trips it)
#include "gpu/visual_graph.h"
#include "gpu/shader_library.h"        // set_project (ADR-0024 Phase 7: reload_project_files)
#include "gpu/operator_scan.h"         // load_and_register_operator
#include "packages/package_manager.h"  // install_package
#include "packages/package_manifest.h" // parse_package_manifest (list_project_assets)

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
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

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
    return lower_copy(haystack).find(lower_copy(needle)) != std::string::npos;
}

bool tutorial_is_first_project(const std::string& tutorial) {
    return tutorial.empty() ||
           tutorial == "mcp_native_first_project" ||
           tutorial == "first_project" ||
           tutorial == "golden_path_a";
}

json tutorial_action(const std::string& title, const std::string& detail) {
    return { {"title", title}, {"detail", detail} };
}

bool valid_shader_op_name(const std::string& name) {
    if (name.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
    for (char ch : name)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) return false;
    return true;
}

std::string shader_filename_for(const std::string& name) {
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if (std::isupper(ch)) {
            if (i > 0 && !out.empty() && out.back() != '_') out.push_back('_');
            out.push_back(static_cast<char>(std::tolower(ch)));
        } else if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        } else if (ch == '_' && !out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    if (out.empty()) out = "project_shader";
    return out + ".wgsl";
}

std::string project_shader_template(const std::string& name) {
    std::ostringstream s;
    s << "/*{\n"
      << "  \"version\": 1,\n"
      << "  \"name\": \"" << name << "\",\n"
      << "  \"summary\": \"Project-local audiovisual shader.\",\n"
      << "  \"keywords\": [\"project\", \"tutorial\", \"generator\"],\n"
      << "  \"inputs\": [],\n"
      << "  \"params\": [\n"
      << "    {\"name\": \"warp\", \"type\": \"float\", \"default\": 0.35, \"min\": 0, \"max\": 1,\n"
      << "     \"semantic_intent\": \"domain warp amount\"},\n"
      << "    {\"name\": \"hue\", \"type\": \"float\", \"default\": 0.58, \"min\": 0, \"max\": 1,\n"
      << "     \"semantic_tag\": \"phase_01\", \"semantic_intent\": \"color hue\"},\n"
      << "    {\"name\": \"density\", \"type\": \"float\", \"default\": 0.42, \"min\": 0, \"max\": 1,\n"
      << "     \"semantic_intent\": \"pattern density\"},\n"
      << "    {\"name\": \"glow\", \"type\": \"float\", \"default\": 0.55, \"min\": 0, \"max\": 1,\n"
      << "     \"semantic_intent\": \"brightness\"}\n"
      << "  ]\n"
      << "}*/\n"
      << "fn ring(uv: vec2f, center: vec2f, radius: f32, width: f32) -> f32 {\n"
      << "    let d = abs(distance(uv, center) - radius);\n"
      << "    return 1.0 - smoothstep(0.0, width, d);\n"
      << "}\n\n"
      << "@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {\n"
      << "    let uv = inp.uv;\n"
      << "    var p = uv * 2.0 - vec2f(1.0);\n"
      << "    p.x = p.x * (u.res.x / max(1.0, u.res.y));\n\n"
      << "    let twist = sin((p.x + p.y) * (3.0 + u.density * 10.0) + u.time * 1.8);\n"
      << "    let q = p + vec2f(cos(p.y * 4.0 + u.time), sin(p.x * 4.0 - u.time)) * (0.04 + u.warp * 0.18);\n\n"
      << "    let beam = 0.5 + 0.5 * sin((q.x * 5.0 + q.y * 2.0) + twist * 1.4 + u.time * 2.0);\n"
      << "    let pulse = ring(uv, vec2f(0.5), 0.16 + u.density * 0.24, 0.08);\n"
      << "    let vignette = smoothstep(1.3, 0.15, length(p));\n\n"
      << "    let a = 0.5 + 0.5 * cos(vec3f(0.0, 2.1, 4.2) + u.hue * 6.2831853);\n"
      << "    let b = vec3f(0.08, 0.12, 0.18);\n"
      << "    var color = mix(b, a, beam * 0.65 + pulse * 0.55);\n"
      << "    color = color * vignette * (0.65 + u.glow * 1.2);\n"
      << "    return vec4f(color, 1.0);\n"
      << "}\n";
    return s.str();
}

json surge_xt_tutorial_prereqs(App* app) {
    namespace P = vivid::session;
    constexpr const char* kTutorial = "mcp_native_first_project";
    constexpr const char* kSurgePath = "/Library/Audio/Plug-Ins/CLAP/Surge XT.clap";
    constexpr const char* kSurgeDownload = "https://surge-synthesizer.github.io/";
    constexpr const char* kPluginList = "examples/tutorials/free-plugin-starter-list.md";

    json checks = json::array();
    json missing = json::array();
    json next_actions = json::array();
    bool ready = true;

    checks.push_back({
        {"name", "vivid_control_server"},
        {"status", "pass"},
        {"summary", "Vivid control server is reachable"}
    });

    checks.push_back({
        {"name", "project_shader_operator_workflow"},
        {"status", "pass"},
        {"method", "scaffold_project_shader_operator"},
        {"summary", "Project-local shaders register as metadata-named operators"}
    });

    std::error_code ec;
    const bool surge_bundle_exists = fs::exists(kSurgePath, ec);
    if (surge_bundle_exists) {
        checks.push_back({
            {"name", "surge_xt_clap_bundle"},
            {"status", "pass"},
            {"path", kSurgePath},
            {"summary", "Surge XT CLAP bundle is installed at the tutorial path"}
        });
    } else {
        ready = false;
        missing.push_back("surge_xt_clap_bundle");
        checks.push_back({
            {"name", "surge_xt_clap_bundle"},
            {"status", "fail"},
            {"path", kSurgePath},
            {"summary", "Surge XT is required for this tutorial and was not found"},
            {"suggestion", "Install Surge XT, then relaunch Vivid so plugin discovery refreshes"}
        });
        next_actions.push_back(tutorial_action(
            "Install Surge XT",
            std::string("Download from ") + kSurgeDownload + " or run `brew install --cask surge-xt`."
        ));
    }

    json catalog_matches = json::array();
    for (int i = 0, n = P::plugin_count(); i < n; ++i) {
        const auto& p = P::plugin_at(i);
        if (!contains_ci(p.name, "surge") && !contains_ci(p.path, "surge")) continue;
        catalog_matches.push_back({
            {"name", p.name},
            {"path", p.path},
            {"format", P::plugin_format_name(p.format)},
            {"class", P::plugin_class_name(p.cls)},
            {"vendor", p.vendor},
            {"probed", p.probed}
        });
    }

    json catalog_check = {
        {"name", "surge_xt_plugin_catalog"},
        {"matches", catalog_matches},
        {"summary", catalog_matches.empty()
                        ? "Vivid's plugin catalog does not currently list Surge XT"
                        : "Vivid's plugin catalog lists Surge-related plugins"}
    };
    if (catalog_matches.empty()) {
        catalog_check["status"] = surge_bundle_exists ? "warn" : "fail";
        catalog_check["suggestion"] = "Relaunch Vivid after installing Surge XT; direct CLAP path loading can still work once the bundle exists.";
        if (!surge_bundle_exists) missing.push_back("surge_xt_plugin_catalog");
        next_actions.push_back(tutorial_action(
            "Refresh Vivid plugin discovery",
            "Quit and relaunch Vivid after installing Surge XT. Then rerun the tutorial preflight."
        ));
    } else {
        catalog_check["status"] = "pass";
    }
    checks.push_back(std::move(catalog_check));

    json r = ok();
    r["tutorial"] = kTutorial;
    r["ready"] = ready;
    r["checks"] = checks;
    r["missing"] = missing;
    r["next_actions"] = next_actions;
    r["free_plugin_list"] = kPluginList;
    r["summary"] = ready
                       ? "tutorial prerequisites are ready"
                       : "tutorial prerequisites need attention before generating the project";
    return r;
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

// Does a saved plugin identity (a catalog label like "Surge XT" or a .clap/.vst3 path) resolve on
// this machine? Loadable if the file exists on disk OR the plugin catalog lists a matching path/name.
// Conservative: an empty identity, or a match by either path or name, counts as resolved — we only
// flag a plugin we are confident is unavailable, so a healthy load reports nothing. This is the
// machine-specific resolver the pure recovery::analyze_saved_project runs against.
bool plugin_identity_resolves(const std::string& id) {
    namespace P = vivid::session;
    if (id.empty()) return true;
    std::error_code ec;
    if (vivid::recovery::ident_looks_like_path(id) && fs::exists(id, ec)) return true;
    for (int i = 0, n = P::plugin_count(); i < n; ++i) {
        const auto& p = P::plugin_at(i);
        if (!p.path.empty() && p.path == id) return true;
        if (!p.name.empty() && (contains_ci(id, p.name) || contains_ci(p.name, id))) return true;
    }
    return false;
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
            project_io::retire_project_operators(*c.app);               // drop project-scoped C++ ops
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
    // Productized tutorial preflight: named onboarding checklists live in Vivid/MCP rather than
    // inside one-off builder scripts. A reachable handler means the app/control-server check passed;
    // tutorial-specific checks report readiness, missing pieces, and next actions without mutating
    // the project.
    handlers_["check_tutorial_prereqs"] = [](const ControlCtx& c, const json& b) {
        const std::string tutorial = b.value("tutorial", b.value("name", std::string("mcp_native_first_project")));
        if (!tutorial_is_first_project(tutorial)) {
            json r = err(code::kBadArg, "unknown tutorial preflight: '" + tutorial + "'");
            r["supported"] = json::array({ "mcp_native_first_project" });
            return r;
        }
        return surge_xt_tutorial_prereqs(c.app);
    };
    handlers_["scaffold_project_shader_operator"] = [](const ControlCtx& c, const json& b) {
        if (!c.app) return err(code::kInternal, "no app context");
        const std::string project_dir = project_asset_dir(c.app);
        if (project_dir.empty() || !vivid::project_paths::is_folder_project(c.app->project.current_project_path))
            return err(code::kBadArg, "scaffold_project_shader_operator needs a saved folder project");
        const std::string name = b.value("name", std::string());
        if (!valid_shader_op_name(name)) return err(code::kBadArg, "shader operator name must be a C identifier");
        if (c.app->op_registry.has(name)) return err(code::kBadArg, "an operator named '" + name + "' already exists");

        const std::string filename = b.value("filename", shader_filename_for(name));
        if (filename.empty() || fs::path(filename).filename().string() != filename)
            return err(code::kBadArg, "shader filename must be a file name, not a path");
        fs::path rel = fs::path("shaders") / filename;
        fs::path out = fs::path(project_dir) / rel;
        std::error_code ec;
        const fs::path root = fs::weakly_canonical(fs::path(project_dir), ec);
        const fs::path abs_parent = fs::weakly_canonical(out.parent_path(), ec);
        const std::string roots = root.string(), parents = abs_parent.string();
        if (parents.rfind(roots, 0) != 0) return err(code::kBadArg, "shader filename resolves outside the project");
        if (fs::exists(out, ec) && !b.value("overwrite", false))
            return err(code::kBadArg, "shader file already exists: " + out.string());

        fs::create_directories(out.parent_path(), ec);
        if (ec) return err(code::kIoError, "could not create shader directory: " + ec.message());
        const std::string source = b.value("source", project_shader_template(name));
        {
            std::ofstream f(out, std::ios::binary);
            if (!f) return err(code::kIoError, "could not write " + out.string());
            f << source;
        }

        const int registered = c.app->shader_library.set_project(c.app->op_registry, project_dir);
        json r = ok();
        r["op"] = name;
        r["path"] = out.string();
        r["project_relative"] = rel.string();
        r["registered"] = c.app->op_registry.has(name);
        r["project_shader_operators"] = registered;
        if (!r["registered"].get<bool>()) {
            for (const auto& e : c.app->shader_library.entries()) {
                if (e.path == out.string()) {
                    r["error"] = e.error.empty() ? std::string("shader did not register") : e.error;
                    break;
                }
            }
        }
        r["summary"] = r["registered"].get<bool>()
                           ? "registered project shader operator '" + name + "'"
                           : "wrote project shader file, but it did not register";
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

    // validate_project — structural health + recovery diagnostics for the loaded project. Reports
    // whether it is saved, whether its session file exists, whether it carries a project-local
    // operator package, and whether any referenced media is missing (the `valid` gate: false only for
    // a hard on-disk-integrity problem — missing session file / missing media). It ALSO surfaces the
    // silent load-time degradations a beginner hits on a fresh machine (ADR-0040 Phase 3, Fulfillment
    // Gate #8): a track's plugin not installed, a project-local shader/operator that did not register
    // or compile, and a package source that is gone. Those set `degraded` (the project loaded but is
    // not fully functional) without flipping `valid`, and each gets an issue + a next_action in the
    // same recovery vocabulary as check_tutorial_prereqs.
    handlers_["validate_project"] = [](const ControlCtx& c, const json&) {
        if (!c.app || !c.session) return err(code::kNoSession, "no session");
        const std::string path = c.app->project.current_project_path;
        json issues = json::array();
        json next_actions = json::array();
        std::set<std::string> action_titles;   // dedup: many tracks may need the same plugin
        auto add_action = [&](const json& a) {
            const std::string t = a.value("title", std::string());
            if (action_titles.insert(t).second) next_actions.push_back(a);
        };
        bool degraded = false;
        json r = ok();
        r["path"] = path;
        r["saved"] = !path.empty();
        std::error_code ec;
        if (path.empty()) {
            issues.push_back({ {"level", "info"}, {"issue", "project is unsaved (in-memory only)"} });
            r["valid"] = true;   // an unsaved project is a valid state, just not on disk yet
        } else {
            const bool folder = vivid::project_paths::is_folder_project(path);
            r["is_folder_project"] = folder;
            const std::string dir = project_asset_dir(c.app);
            const std::string jpath = vivid::project_paths::session_json_path(path);
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

            // Beginner recovery (ADR-0040 P3): a track whose plugin isn't installed loaded as a silent
            // placeholder (persist.cpp), and a package source that is gone is otherwise invisible here.
            // The saved JSON is the source of intent; analyze it against this machine's plugin catalog.
            json disk = json::object();
            if (session_exists) {
                std::ifstream f(jpath);
                try { f >> disk; }
                catch (const std::exception& e) {
                    issues.push_back({ {"level", "error"}, {"issue", "session file could not be parsed"},
                                       {"path", jpath}, {"detail", e.what()} });
                    disk = json::object();
                }
            }
            auto rep = vivid::recovery::analyze_saved_project(
                disk, dir, has_pkg, [](const std::string& id) { return plugin_identity_resolves(id); });
            for (const auto& is : rep.issues) issues.push_back(is);
            for (const auto& a : rep.next_actions) add_action(a);
            degraded = degraded || rep.degraded;
        }

        // Visual-op health: the live graph knows which nodes never resolved to a registered operator
        // (missing project shader / package op) and which shader ops carry a compile error. Both are
        // otherwise silent over MCP — VisualNode::error() is the single source of truth (ADR-0019).
        if (c.vgraph) {
            bool broken_visual = false;
            for (const auto& n : c.vgraph->nodes()) {
                const std::string e = n.error();
                if (e.empty()) continue;
                degraded = true;
                broken_visual = true;
                const bool unregistered = n.op_missing();
                issues.push_back({ {"level", "error"},
                                   {"issue", unregistered ? "visual operator not registered"
                                                          : "visual operator failed to compile"},
                                   {"node_id", n.id}, {"op", n.op_type}, {"detail", e},
                                   {"suggestion", unregistered
                                       ? "The project-local shader/operator '" + n.op_type + "' is not registered. Check shaders/ and vivid-package.json, then call reload_project_files."
                                       : "Fix the shader/operator source for '" + n.op_type + "', then call reload_project_files."} });
            }
            // Only suggest reload_project_files when a *visual op* is actually broken — that is the fix
            // reload re-applies. A missing plugin needs a relaunch (its own action), not a reload.
            if (broken_visual)
                add_action(tutorial_action("Reload project files",
                    "After fixing a project-local shader or operator source on disk, call reload_project_files to re-register it without reloading the whole project."));
        }

        for (const auto& m : c.app->project.missing_media)
            issues.push_back({ {"level", "error"}, {"issue", "missing media"}, {"path", m} });
        if (!c.app->project.missing_media.empty()) r["valid"] = false;

        r["issues"] = issues;
        r["degraded"] = degraded;
        r["next_actions"] = next_actions;
        const bool valid = r.value("valid", false);
        const std::string n = std::to_string(issues.size());
        r["summary"] = !valid
                           ? "project has " + n + " issue(s) — not loadable as saved"
                           : degraded
                               ? "project loaded but is degraded — " + n + " recovery item(s)"
                               : "project is valid (" + n + " note(s))";
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
        // Loose files in the project dir + conventional authored-asset dirs. `shaders/` is for
        // shader-operator sources with JSON headers; `assets/` carries raw files consumed by FILE
        // params (including CustomShader fragments).
        for (const auto& base : { fs::path(dir), fs::path(dir) / "shaders" }) {
            if (!fs::is_directory(base, ec)) continue;
            for (const auto& e : fs::directory_iterator(base, ec)) {
                if (!e.is_regular_file(ec)) continue;
                const std::string kind = asset_kind(e.path());
                if (kind == "package_manifest") continue;   // already added above
                add(e.path(), kind);
            }
        }
        const fs::path assets_dir = fs::path(dir) / "assets";
        if (fs::is_directory(assets_dir, ec)) {
            for (const auto& e : fs::recursive_directory_iterator(assets_dir, ec)) {
                if (!e.is_regular_file(ec)) continue;
                add(e.path(), asset_kind(e.path()));
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
        // Shaders: re-scan the project's shaders. set_project re-registers each shader TYPE with the
        // current file body, but it does NOT touch live graph NODES — an already-built node keeps its
        // old compiled pipeline. So a body edit (or a newly-broken body) would never reach the running
        // node via this MCP call, and the frame-loop poll only fires when the window is actively
        // rendering. For a deterministic, MCP-native live-edit loop, rebuild the live nodes of each
        // re-registered project shader type here: rebuild_op_instances recompiles the node from the new
        // body (preserving params by name) so good edits take effect and a broken body surfaces its
        // compile error through VisualNode::error() (validate_project / broken_ops).
        const int shader_ops = c.app->shader_library.set_project(c.app->op_registry, dir);
        r["shader_operators"] = shader_ops;
        int shader_nodes_rebuilt = 0;
        if (c.vgraph) {
            for (const auto& e : c.app->shader_library.entries()) {
                if (e.tier == "project" && e.registered)
                    shader_nodes_rebuilt += c.vgraph->rebuild_op_instances(e.name);
            }
        }
        r["shader_nodes_rebuilt"] = shader_nodes_rebuilt;
        const int file_param_nodes = c.vgraph ? c.vgraph->bump_all_file_param_generations() : 0;
        r["file_param_nodes_reloaded"] = file_param_nodes;
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
                    if (!reg.empty()) { jo["op"] = reg; ++registered; c.app->project_operator_types.insert(reg); }
                    else jo["note"] = "compiled but already registered — use reload_operator_package to hot-swap";
                }
                ops.push_back(jo);
            }
        }
        if (registered > 0) c.app->file_drops.rebuild(c.app->op_loaders);
        r["operators"] = ops;
        r["newly_registered"] = registered;
        r["summary"] = "reloaded project files: " + std::to_string(shader_ops) + " shader op(s), " +
                       std::to_string(shader_nodes_rebuilt) + " shader node(s) rebuilt, " +
                       std::to_string(file_param_nodes) + " file-param node(s), " +
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
