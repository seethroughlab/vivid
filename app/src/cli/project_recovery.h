#pragma once

// Beginner recovery diagnostics (ADR-0040 Phase 3, Fulfillment Gate #8).
//
// A project can be structurally saved yet load *degraded* in ways that are otherwise silent over the
// control server / MCP: a track's plugin is not installed on this machine (persist.cpp falls back to
// a silent placeholder), or a project-local operator package names a source that is gone. This unit
// analyses the SAVED session JSON + the project folder against a machine-specific plugin resolver and
// reports each problem in the {issue, suggestion} + next_actions vocabulary of check_tutorial_prereqs,
// so a beginner or agent can recover without reading stderr.
//
// It is deliberately pure (JSON + filesystem + an injected resolver, no App/GPU): validate_project
// wires the real plugin catalog as the resolver and appends live-VisualGraph op health on top.

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace vivid::recovery {

using json = nlohmann::json;

// A {title, detail} recovery next-action, matching check_tutorial_prereqs' next_actions entries.
json recovery_action(const std::string& title, const std::string& detail);

// True if a saved plugin identity looks like a filesystem path (contains '/' or ends .clap/.vst3),
// rather than a plain catalog label like "Surge XT".
bool ident_looks_like_path(const std::string& id);

// The external plugin a saved instrument track depends on, or "" if it needs none (an audio track, or
// a built-in native-op instrument). Prefers the authoritative CLAP path, then a VST3/label
// instrument. Mirrors the load branches in persist.cpp's rebuild_tracks_from_doc.
std::string saved_track_plugin_identity(const json& track);

// A recovery next-action for a missing plugin. Surge XT (the tutorial's assumed beginner instrument)
// gets its known download + brew hint; any other plugin gets a generic install-and-relaunch action.
json plugin_recovery_action(const std::string& id);

// Is this saved plugin identity available on this machine?
using PluginResolver = std::function<bool(const std::string&)>;

struct RecoveryReport {
    json issues = json::array();
    json next_actions = json::array();
    bool degraded = false;
};

// Analyse a saved session + project dir for beginner-recoverable degradations:
//   - tracks whose plugin does not resolve (via `resolve`), and
//   - package operator sources missing on disk (only checked when `has_package`).
// next_actions are deduplicated by title. Visual-op health is added by the caller (it needs the live
// VisualGraph), so this stays App/GPU-free and unit-testable.
RecoveryReport analyze_saved_project(const json& saved_session, const std::string& project_dir,
                                     bool has_package, const PluginResolver& resolve);

}  // namespace vivid::recovery
