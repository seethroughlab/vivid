#pragma once

// ADR-0016 / S3 — the shader library: the three-tier search path, the scan, and the
// registration of each shader file as an operator type.
//
// Precedence is user > project > bundled: a user may shadow a shipped shader by name, and
// that is an authoring affordance, not an accident. (First registration wins, exactly as it
// does for operator dylibs — so the scan visits the tiers in precedence order.)

#include <memory>
#include <string>
#include <vector>

namespace vivid {

class OpRegistry;
struct ShaderDef;

// One row of the catalog. A shader that fails to parse STILL gets a row, carrying its
// error — a malformed file must be visible and diagnosable, never silently absent.
struct ShaderLibraryEntry {
    std::string name;      // the operator type name (empty if the header did not parse)
    std::string path;
    std::string tier;      // "user" | "project" | "bundled"
    std::string summary;
    std::string error;     // non-empty => not registered; this says why
    bool        registered = false;   // false when malformed, or shadowed by an earlier tier
};

// The search path, in precedence order. `project_dir` (a loaded project folder) may be empty.
//   user     ~/Library/Application Support/Vivid/shaders   ($VIVID_SHADERS_DIR overrides)
//   project  <project_dir>/shaders
//   bundled  <app>/Contents/Resources/shaders  (or <exe_dir>/shaders)
std::vector<std::pair<std::string, std::string>>   // (dir, tier)
shader_search_path(const std::string& project_dir = {});

// The scanned library. Owns the ShaderDefs — they must outlive every node instance and
// every cached descriptor built from one (see ShaderDef), so the App owns this for the
// whole run.
class ShaderLibrary {
public:
    // Parse every .wgsl/.glsl under each dir of the search path and register the good ones
    // into `reg` as operator types. Returns the number newly registered.
    int scan(OpRegistry& reg, const std::string& project_dir = {});

    const std::vector<ShaderLibraryEntry>& entries() const { return entries_; }

private:
    int scan_dir(const std::string& dir, const std::string& tier, OpRegistry& reg);

    std::vector<ShaderLibraryEntry>         entries_;
    std::vector<std::shared_ptr<ShaderDef>> defs_;   // kept alive for the process lifetime
};

}  // namespace vivid
