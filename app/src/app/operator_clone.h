#pragma once
#include <memory>
#include <string>
#include <vector>

// "Clone & Edit a built-in": scaffold an editable C++ package operator that reproduces
// a built-in, compile it (the package compiler / clang++), register it in the live
// OpRegistry, and return the new type name + its source path. The caller swaps the graph
// node to the new type and opens the source. Built-in shaders are compiled-in GLSL, so a
// clone ships a WGSL package-op template per cloneable built-in (Plasma today).
namespace vivid {
class OpRegistry;
class OperatorLoader;

struct CloneResult {
    bool        ok = false;
    std::string name;         // the new registered operator type (e.g. "PlasmaClone")
    std::string source_path;  // the generated .cpp (open this in the editor)
    std::string error;        // compiler / scaffold error when !ok
};

// Whether a built-in operator has a clone template.
bool operator_has_clone_template(const std::string& builtin_name);

// The generated package-operator .cpp for `builtin_name`, with the type placeholder
// substituted to `type_name`. Empty when the built-in has no clone template. Exposed so a
// headless test can compile + register the real template without a live App or GPU.
std::string clone_operator_source(const std::string& builtin_name, const std::string& type_name);

// The vivid-package.json manifest for a single-operator clone package named `type_name`.
std::string clone_operator_manifest(const std::string& type_name);

// Clone `builtin_name` into a fresh editable package operator (compiled + registered
// into `reg`/`loaders`, the live operator catalog). The caller swaps the node + opens
// the source. Decoupled from App so the compile+register path is headless-testable.
// `target_name` names the new operator; empty picks a unique "<builtin>Clone" name.
CloneResult clone_operator(OpRegistry& reg, std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                           const std::string& builtin_name, const std::string& target_name = "");

// Clone an EXISTING editable package operator by copying its on-disk source: read
// `source_path`, rename the `old_name` type token to `new_name` (whole-word), scaffold a
// fresh single-op package, then compile + register it live. Mirrors fork_shader for a
// compiled operator that has a watched/editable source. `new_name` must be a free,
// C++-identifier-safe type name (the caller checks reg.has()).
CloneResult clone_operator_from_source(OpRegistry& reg, std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                                       const std::string& old_name, const std::string& source_path,
                                       const std::string& new_name);

}  // namespace vivid
