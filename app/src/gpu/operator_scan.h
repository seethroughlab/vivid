#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace vivid {

class OpRegistry;
class OperatorLoader;

// The outcome of trying to load+register one .dylib operator. `ok` means it registered; on failure
// `error_key`/`error_msg` carry the loader's structured reason (dlopen_failed, abi_mismatch,
// missing_abi_symbol, missing_required_symbols, …) so callers can surface WHY over MCP instead of
// only "not registered". `shadowed`/`quarantined` distinguish the two non-error skips; `op_name` is
// the operator's declared name whenever it is known (even when skipped).
struct RegisterResult {
    bool        ok = false;
    std::string op_name;
    std::string error_key;
    std::string error_msg;
    bool        shadowed = false;    // a registration with this name already won (built-in/first)
    bool        quarantined = false; // ADR-0018: repeat crasher, intentionally not registered
};

// Load one .dylib operator: validate it, register it by its descriptor name (with discovery
// metadata) into `reg`, and move the loader into `loaders`. Returns the full outcome. Shared by the
// startup scan and package install. ADR-0018: if `quarantined` contains the op's name it is NOT
// registered (its persisted nodes then load as op_missing()).
RegisterResult load_and_register_operator_ex(const std::string& dylib_path, OpRegistry& reg,
                                             std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                                             const std::set<std::string>* quarantined = nullptr);

// Backwards-compatible thin wrapper over the _ex form: returns the registered op name, or "" on
// failure / quarantine / when the name is already registered. Existing callers keep working.
std::string load_and_register_operator(const std::string& dylib_path, OpRegistry& reg,
                                       std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                                       const std::set<std::string>* quarantined = nullptr);

// Scan `dir` for *.dylib operators and load_and_register_operator each. Returns the
// number newly registered. If `out_errors` is non-null, each dylib that FAILED to load
// (a genuine loader error — not a benign shadow/quarantine skip) is appended so the caller
// can surface it in-app instead of leaving it stderr-only (ADR-0019 / Ph3 audit P2-02).
int scan_operator_dir(const std::string& dir, OpRegistry& reg,
                      std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                      const std::set<std::string>* quarantined = nullptr,
                      std::vector<RegisterResult>* out_errors = nullptr);

}  // namespace vivid
