#pragma once

#include "app/project_paths.h"   // is_folder_project / session_json_path (pure)

#include <string>
#include <vector>

namespace vivid::session { struct Session; }
namespace vivid::ui { class NodeGraph; }

namespace vivid {

struct App;

// Folder-aware project save/load, shared by the control server (save_project /
// load_project) and the GUI File menu so both behave identically.
//
// A "project" is either a legacy single JSON file (path ends in ".json") or a
// project FOLDER (any other path) that holds "project.json" plus optional co-located
// assets — a custom-operator package (vivid-package.json + sources) and .glsl shaders.
// On load, a project-local package is compiled into the folder and registered BEFORE
// the session JSON is read, so a node referencing a project-local operator by name
// resolves. Compiling here blocks the main thread briefly (same as install_package).
namespace project_io {

using project_paths::is_folder_project;   // pure path helpers (app/project_paths.h)
using project_paths::session_json_path;

struct SaveResult {
    bool        ok = false;
    std::string error;          // when !ok
    std::string session_file;   // the JSON file actually written
};

// Write the session. For a folder project, creates the directory and writes
// "<dir>/project.json"; for a .json path, writes it directly. Does NOT touch
// co-located assets (they are authored in place). Updates app.project and the live project asset
// root on success, so project-relative FILE params resolve immediately after first save.
SaveResult save(App& app, ui::NodeGraph& graph, int win_w, int win_h, float split_x, float dock_h,
                const std::string& path);

// One project-local operator's compile+register outcome (for reporting).
struct OpResult {
    std::string name;
    bool        compiled = false;
    bool        registered = false;
    std::string note;    // e.g. "name already in use"
    std::string error;   // compiler stderr when !compiled
};
struct LoadResult {
    bool        ok = false;
    std::string error;             // when !ok
    bool        had_package = false;
    std::string package_name;
    int         registered = 0;
    std::vector<OpResult> ops;
};

// Compile+register a project-local package (if the folder has vivid-package.json),
// then load the session. win/split/dock are updated from the restored view state.
// Updates app.project on success.
LoadResult load(App& app, ui::NodeGraph& graph, int& win_w, int& win_h, float& split_x, float& dock_h,
                const std::string& path);

}  // namespace project_io
}  // namespace vivid
