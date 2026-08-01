// Pure rollup + serialization for the runtime-health snapshot (P4.3). No App / GPU
// dependency, so this compiles into the headless test as well as the app.
#include "app/runtime_health.h"

namespace vivid {

Severity severity(const HealthSnapshot& s) {
    // A lost device or a graph node pointing at an operator that no longer exists are
    // hard breakages — the render can't produce correct output.
    if (!s.gpu_ok || s.missing_ops > 0) return Severity::Error;
    // Recoverable-but-noteworthy: the GPU reported (and survived) errors, or the agent
    // control surface isn't up.
    if (s.gpu_errors > 0 || !s.control_running) return Severity::Warning;
    // NOTE: output_fed is intentionally NOT a severity input — an unfed Output is an
    // empty-by-design canvas (benign), not a fault (P2-03).
    return Severity::Ok;
}

const char* severity_str(Severity sev) {
    switch (sev) {
        case Severity::Error:   return "error";
        case Severity::Warning: return "warning";
        default:                return "ok";
    }
}

nlohmann::json to_json(const HealthSnapshot& s) {
    nlohmann::json j;
    j["severity"] = severity_str(severity(s));
    j["app_version"] = s.app_version;
    j["audio"]   = { {"session_active", s.audio_session_active} };
    j["gpu"]     = { {"ok", s.gpu_ok}, {"errors", s.gpu_errors} };
    if (!s.gpu_last_error.empty()) j["gpu"]["last_error"] = s.gpu_last_error;
    j["graph"]   = { {"op_nodes", s.op_nodes}, {"op_types", s.op_types},
                     {"missing_ops", s.missing_ops}, {"output_fed", s.output_fed} };
    j["packages"] = { {"loaded", s.packages_loaded} };
    j["control"]  = { {"running", s.control_running} };
    return j;
}

}  // namespace vivid
