#pragma once
#include <string>
#include <vector>

// Native file chooser (macOS NSOpenPanel/NSSavePanel; a no-op stub returns "" elsewhere).
// A Vivid project is a folder (or a legacy .json); the open panel allows either. Returns the
// chosen absolute path, or "" if the user cancelled / no native dialog is available. These
// run a modal panel — UI/main thread only.
namespace vivid::platform {

std::string open_project_dialog();
std::string save_project_dialog(const std::string& suggested_name);

// Choose a destination for a video export (File > Export Video). `suggested_name` seeds the name
// field (e.g. "vivid-export.mp4"). Returns the chosen absolute path, or "" if cancelled / no dialog.
std::string save_video_dialog(const std::string& suggested_name);

// Choose a single existing file (e.g. an image for an Image node). `message` labels the
// panel. Returns the chosen absolute path, or "" if cancelled / no native dialog.
// `extensions` (lowercased, no dot — e.g. {"png","jpg"}) filters the panel to those types; empty
// allows any file. (ADR-0021/P3.)
std::string open_file_dialog(const std::string& message,
                             const std::vector<std::string>& extensions = {});

// ADR-0018: a Save / Don't Save / Cancel confirmation before an action that would discard unsaved
// changes (New, Open, Quit). Modal — UI/main thread only. Off macOS the stub returns Discard (no
// native dialog to block on), so headless/non-mac builds never wedge.
enum class DiscardChoice { Save, Discard, Cancel };
DiscardChoice confirm_discard_changes();

// ADR-0018: at launch, offer to recover autosaved unsaved work. `detail` describes what/when.
// Returns true to recover, false to discard. Off macOS the stub returns false.
bool confirm_recover_autosave(const std::string& detail);

// Ph3 P2-01: a native (non-GPU) informational alert with a single OK button. Used to explain a
// state the GPU-rendered UI cannot show — chiefly a lost graphics device, where an in-app toast
// would never render. Modal — UI/main thread only. Off macOS the stub is a no-op.
void show_alert(const std::string& title, const std::string& message);

}  // namespace vivid::platform
