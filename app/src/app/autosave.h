#pragma once
#include <string>

namespace vivid {
struct App;
namespace ui { class NodeGraph; }

// ADR-0018 (R4): crash/kill-safe autosave + recovery of unsaved work.
//
// The trunk does NOT reload the last project on launch, so a per-project sidecar wouldn't be found
// on a fresh start. Instead there is ONE well-known autosave slot under user_data_dir()/autosave/:
//   session.json — the last autosaved document (the same serializer a real save uses)
//   meta.json    — { "project": "<current_project_path or empty>", "saved_at": <unix secs> }
// The frame loop writes it periodically while the document is dirty; a real save clears it. On
// launch, if session.json exists and the associated project file isn't newer than it (or the doc was
// untitled), we recover: load the session, re-point current_project_path so Save targets the real
// project, mark the document dirty, and toast.
namespace autosave {

// The autosave slot's directory (user_data_dir()/autosave), created on demand.
std::string dir();

// Write session.json + meta.json for the current document. `now_unix` is stamped into meta (the
// caller passes wall-clock seconds; the module does no time syscalls of its own).
void write(App& app, int win_w, int win_h, float split_x, float dock_h, long long now_unix);

// Remove the autosave slot (call after a successful real save — a clean doc needs no recovery).
void clear();

struct Recovery {
    bool        available = false;   // a recoverable autosave newer than its project exists
    std::string session_path;        // the session.json to load
    std::string project_path;        // the project it belongs to ("" = untitled)
};
// Inspect the slot at launch. Available when session.json exists AND (project is untitled, or its
// file is missing / older than the autosave — i.e. the autosave holds work the project doesn't).
Recovery check();

}  // namespace autosave
}  // namespace vivid
