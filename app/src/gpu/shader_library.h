#pragma once

// ADR-0016 / S3+S4 — the shader library: the three-tier search path, the scan that registers
// each shader file as an operator type, and the mtime watch that hot-reloads an edited file.
//
// Precedence is user > project > bundled: a user may shadow a shipped shader by name, and that
// is an authoring affordance, not an accident. (First registration wins, exactly as it does for
// operator dylibs — so the scan visits the tiers in precedence order.)

#include "packages/file_watcher.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vivid {

class OpRegistry;
struct ShaderDef;
struct ShaderSlot;

// One row of the catalog. A shader that fails to parse STILL gets a row, carrying its error —
// a malformed file must be visible and diagnosable, never silently absent.
struct ShaderLibraryEntry {
    std::string name;      // the operator type name (empty if the header did not parse)
    std::string path;
    std::string tier;      // "user" | "project" | "bundled"
    std::string summary;
    std::string error;     // non-empty => not registered; this says why
    bool        registered = false;   // false when malformed, or shadowed by an earlier tier

    std::shared_ptr<ShaderSlot> slot;   // the live definition (null when not registered)
};

// What a poll() found. A BODY edit is absorbed by the live nodes themselves; an INTERFACE
// change (params/ports) needs the caller to rebuild the nodes of that type, preserving their
// param values by name — the caller owns the graph, so it does that, not the library.
enum class ShaderChange {
    Body,        // recompiled in place; nothing else to do
    Interface,   // the type re-registered: rebuild this op's nodes
    Added,       // a new file appeared and registered as a new type
    Failed,      // the edit did not parse / cannot be applied; the last good version still runs
};

struct ShaderReload {
    std::string  name;
    std::string  path;
    ShaderChange change = ShaderChange::Body;
    std::string  error;   // set when Failed
};

// The search path, in precedence order. `project_dir` (a loaded project folder) may be empty.
//   user     ~/Library/Application Support/Vivid/shaders   ($VIVID_SHADERS_DIR overrides)
//   project  <project_dir>/shaders
//   bundled  <app>/Contents/Resources/shaders  (or <exe_dir>/shaders)
std::vector<std::pair<std::string, std::string>>   // (dir, tier)
shader_search_path(const std::string& project_dir = {});

// The scanned library. Owns the ShaderDefs — they must outlive every node instance and every
// cached descriptor built from one (see ShaderDef), so the App owns this for the whole run.
class ShaderLibrary {
public:
    // Parse every .wgsl/.glsl under each dir of the search path and register the good ones into
    // `reg` as operator types. Returns the number newly registered.
    int scan(OpRegistry& reg, const std::string& project_dir = {});

    // Re-walk the search path for files the library has not seen yet (and retry the ones that
    // previously failed — a fixed header should just appear). Returns the number newly registered.
    int rescan(OpRegistry& reg);

    // Once per frame, main thread: pick up edits to watched files. Cheap (an mtime stat each).
    std::vector<ShaderReload> poll(OpRegistry& reg);

    const std::vector<ShaderLibraryEntry>& entries() const { return entries_; }
    const ShaderLibraryEntry* find(const std::string& op_name) const;
    bool is_shader(const std::string& op_name) const { return find(op_name) != nullptr; }

    // Fork-to-edit: copy a shader into the user tier so it can be edited, and register the copy
    // under `new_name`. Returns the path written, or "" with `error` set.
    std::string fork(const std::string& op_name, const std::string& new_name, OpRegistry& reg,
                     std::string& error);

private:
    // Parse one file and, if it is good and its name is free, register it. Always appends a row.
    // Returns true when it registered a new type.
    bool add_file(const std::string& path, const std::string& tier, OpRegistry& reg);
    ShaderLibraryEntry* find_by_path(const std::string& path);

    std::vector<ShaderLibraryEntry> entries_;
    FileWatcher                     watcher_;    // path -> op name (or the path, when unregistered)
    std::string                     project_dir_;

    // EVERY ShaderDef the library has ever parsed, including the ones a reload superseded.
    // Nothing here is ever freed while the app runs, and it must not be.
    //
    // A ShaderDef is not just data: `ParamBase::name`/`group`/`description`/`choice_labels` and the
    // registry's CACHED DESCRIPTOR are raw `const char*` pointing straight into it. A live node that
    // adopts a reloaded def would otherwise drop the last reference to the def its own param names
    // still point at — and the names come back as freed memory. (Observed: after a body reload every
    // param on the node read as "". Keeping the old defs alive is a few KB per save and makes the
    // whole class of dangling-string bug impossible.)
    std::vector<std::shared_ptr<const ShaderDef>> defs_;
};

}  // namespace vivid
