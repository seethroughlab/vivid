#include "app/project_io.h"

#include "app/app.h"                     // App: project + op_registry + op_loaders
#include "persist.h"                     // save_session / load_session
#include "packages/package_manager.h"    // install_package
#include "gpu/operator_scan.h"           // load_and_register_operator
#include "gpu/visual_graph.h"            // VisualGraph::set_asset_dir

#include <filesystem>

namespace vivid::project_io {
namespace fs = std::filesystem;

SaveResult save(App& app, ui::NodeGraph& graph, int win_w, int win_h, float split_x, float dock_h,
                const std::string& path) {
    SaveResult r;
    if (is_folder_project(path)) {
        std::error_code ec;
        fs::create_directories(path, ec);
        if (ec) { r.error = "could not create project directory: " + ec.message(); return r; }
    }
    r.session_file = session_json_path(path);
    if (!save_session(r.session_file, app.session, graph, win_w, win_h, split_x, dock_h)) {
        r.error = "write failed";
        return r;
    }
    app.remember_project_path(path);   // remember the folder (or the .json), not the inner file
    r.ok = true;
    return r;
}

LoadResult load(App& app, ui::NodeGraph& graph, int& win_w, int& win_h, float& split_x, float& dock_h,
                const std::string& path) {
    LoadResult r;
    // Compile + register a project-local operator package first, so a node that names
    // a project-local operator resolves when the session JSON is read below.
    if (is_folder_project(path)) {
        std::error_code ec;
        if (fs::exists(fs::path(path) / "vivid-package.json", ec)) {
            r.had_package = true;
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
                    if (o.registered) ++r.registered;
                    else o.note = "compiled but not registered (name already in use)";
                }
                r.ops.push_back(std::move(o));
            }
        }
    }
    const std::string jpath = session_json_path(path);
    if (!load_session(jpath, app.session, graph, win_w, win_h, split_x, dock_h)) {
        r.error = "read failed";
        return r;
    }
    // A node's relative `asset` (a CustomShader .glsl) resolves against the session
    // file's directory (the project folder, or a .json's parent dir).
    if (app.vgraph) app.vgraph->set_asset_dir(fs::path(jpath).parent_path().string());
    app.remember_project_path(path);
    r.ok = true;
    return r;
}

}  // namespace vivid::project_io
