#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace vivid {
struct App;

// A point-in-time health snapshot of the running engine (P4.3). Plain data so the
// rollup + serialization are pure (no App / GPU) and headless-testable; collect() is
// the only part that reads the live App. Mirrors the control_parse pure/testable split.
//
// Honesty over coverage: we only carry signals we can read truthfully + cheaply from
// App today (gpu errors, operator/graph counts, control liveness). Audio xrun metering
// and per-node timing are intentionally absent until there's a real source for them.
struct HealthSnapshot {
    // audio
    bool        audio_session_active = false;   // a real multi-track session (vs test tone)

    // gpu
    bool        gpu_ok = true;                   // device not lost
    unsigned    gpu_errors = 0;                  // uncaptured WebGPU errors since start
    std::string gpu_last_error;

    // graph / operators
    int         op_nodes = 0;                    // op nodes in the visuals chain
    int         op_types = 0;                    // registered operator types (built-in + loaded)
    int         missing_ops = 0;                 // chain nodes whose op type isn't registered (BROKEN)
    int         packages_loaded = 0;             // dlopen'd operator dylibs

    // control / process
    bool        control_running = false;         // MCP control server bound + listening
    std::string app_version;
};

enum class Severity { Ok, Warning, Error };

// Pure rollup: Error if the GPU device is lost or the graph references a vanished op;
// Warning on uncaptured GPU errors or a down control server; otherwise Ok.
Severity severity(const HealthSnapshot& s);
const char* severity_str(Severity sev);

// Pure JSON serialization (includes the rolled-up "severity").
nlohmann::json to_json(const HealthSnapshot& s);

// Read the live engine state into a snapshot (defined in runtime_health_collect.cpp,
// which links against App; the two functions above stay App-free for the headless test).
HealthSnapshot collect_health(const App& app);

}  // namespace vivid
