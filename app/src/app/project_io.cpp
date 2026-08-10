#include "app/project_io.h"

#include "app/app.h"                     // App: project + op_registry + op_loaders
#include "persist.h"                     // save_session / load_session
#include "packages/package_manager.h"    // install_package
#include "packages/package_manifest.h"   // parse_package_manifest (op name -> source, source-forward)
#include "gpu/operator_scan.h"           // load_and_register_operator
#include "gpu/visual_graph.h"            // VisualGraph::set_asset_dir
#include "ui/node_graph.h"               // chain_load_begin / reset_nodes
#include "ui/audio_node_graph.h"         // App::audio_graph view getters (ADR-0023 6b)

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace vivid::project_io {
namespace fs = std::filesystem;

void retire_project_operators(App& app) {
    if (app.project_operator_types.empty()) return;
    for (const std::string& name : app.project_operator_types)
        app.op_registry.unregister_type(name);
    app.op_loaders.erase(
        std::remove_if(app.op_loaders.begin(), app.op_loaders.end(), [&](const auto& loader) {
            const VividOperatorDescriptor* d = loader ? loader->descriptor() : nullptr;
            return d && d->name && app.project_operator_types.count(d->name) > 0;
        }),
        app.op_loaders.end());
    app.project_operator_types.clear();
    app.project_operator_sources.clear();   // source-forward map is project-scoped too
    app.file_drops.rebuild(app.op_loaders);
}

SaveResult save(App& app, ui::NodeGraph& graph, int win_w, int win_h, float split_x, float dock_h,
                const std::string& path) {
    SaveResult r;
    if (is_folder_project(path)) {
        std::error_code ec;
        fs::create_directories(path, ec);
        if (ec) { r.error = "could not create project directory: " + ec.message(); return r; }
    }
    r.session_file = session_json_path(path);
    const auto& av = app.audio_graph->view();
    if (!save_session(r.session_file, app.session, graph, win_w, win_h, split_x, dock_h,
                      av.ox, av.oy, av.scale)) {
        r.error = "write failed";
        return r;
    }
    app.remember_project_path(path);   // remember the folder (or the .json), not the inner file
    const std::string project_dir = fs::path(r.session_file).parent_path().string();
    app.shader_library.set_project(app.op_registry, project_dir);
    if (app.vgraph) app.vgraph->set_asset_dir(project_dir);
    r.ok = true;
    return r;
}

LoadResult load(App& app, ui::NodeGraph& graph, int& win_w, int& win_h, float& split_x, float& dock_h,
                const std::string& path) {
    LoadResult r;
    // UX Phase-2 F4/F5: validate the file is a readable Vivid project BEFORE tearing down the current
    // one — a missing, unparseable, or foreign JSON should give a clear reason and leave the loaded
    // project intact, not silently replace it with an empty session (a real risk given PRD §7's
    // hand-editable project text). Every real project serializes a "graph" and "tracks" key.
    {
        const std::string vpath = session_json_path(path);
        std::error_code ec;
        if (!fs::exists(vpath, ec)) { r.error = "no project file at " + vpath; return r; }
        std::ifstream in(vpath, std::ios::binary);
        const std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const nlohmann::json doc = nlohmann::json::parse(txt, nullptr, /*allow_exceptions*/false);
        if (doc.is_discarded()) { r.error = "project file is not valid JSON: " + vpath; return r; }
        if (!doc.is_object() || (!doc.contains("graph") && !doc.contains("tracks"))) {
            r.error = "not a Vivid project (missing \"graph\"/\"tracks\"): " + vpath; return r;
        }
    }
    // Drop the old visual graph before unregistering old project-local C++ operators; node
    // instances may own code from the dylibs we are about to retire.
    graph.reset_nodes();
    graph.chain_load_begin();
    retire_project_operators(app);
    // Compile + register a project-local operator package first, so a node that names
    // a project-local operator resolves when the session JSON is read below.
    if (is_folder_project(path)) {
        std::error_code ec;
        if (fs::exists(fs::path(path) / "vivid-package.json", ec)) {
            r.had_package = true;
            // op name -> its .cpp inside the project, so a registered project-local op resolves to
            // "Open source in editor" on right-click (source-forward, ADR-0054).
            std::map<std::string, std::string> op_src;
            { PackageManifest mf = parse_package_manifest(path);
              for (const auto& op : mf.operators) op_src[op.name] = (fs::path(path) / op.source).string(); }
            PackageInstallResult ir = install_package(path, path);  // compile INTO the project folder
            if (!ir.ok) { r.error = "package: " + ir.error; return r; }
            r.package_name = ir.name;
            for (const auto& ci : ir.compiles) {
                OpResult o; o.name = ci.op_name; o.compiled = ci.success;
                if (!ci.success) {
                    o.error = ci.error_output;
                } else {
                    const std::string reg = load_and_register_operator(ci.dylib_path, app.op_registry, app.op_loaders);
                    o.registered = !reg.empty();
                    if (o.registered) {
                        ++r.registered; app.project_operator_types.insert(reg);
                        auto it = op_src.find(ci.op_name);
                        if (it != op_src.end()) app.project_operator_sources[reg] = it->second;
                    }
                    else o.note = "compiled but not registered (name already in use)";
                }
                r.ops.push_back(std::move(o));
            }
        }
    }
    const std::string jpath = session_json_path(path);
    // Register this project's project-scoped shader operators (its shaders/ dir) BEFORE the session
    // is read below, so a node that names one resolves. Cleared when another project loads / on New.
    app.shader_library.set_project(app.op_registry, fs::path(jpath).parent_path().string());
    float aox = 0.f, aoy = 0.f, ascale = 0.f;   // scale 0 = sentinel: no camera in the file
    if (!load_session(jpath, app.session, graph, win_w, win_h, split_x, dock_h, aox, aoy, ascale)) {
        r.error = "failed to load project: " + jpath;
        graph.chain_load_begin();
        if (app.vgraph) app.vgraph->reset_to_default();
        retire_project_operators(app);
        return r;
    }
    if (ascale > 0.f) app.audio_graph->set_view({ aox, aoy, ascale });   // restore the persisted camera (ADR-0023)
    // A node's relative `asset` (a CustomShader .glsl) resolves against the session
    // file's directory (the project folder, or a .json's parent dir).
    if (app.vgraph) app.vgraph->set_asset_dir(fs::path(jpath).parent_path().string());
    app.remember_project_path(path);
    app.reseed_undo_baseline = true;   // ADR-0017 / Phase-2 F1: opening a project resets the undo history
    r.ok = true;
    return r;
}

}  // namespace vivid::project_io
