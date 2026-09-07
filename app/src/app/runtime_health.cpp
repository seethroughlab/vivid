// Pure rollup + serialization for the runtime-health snapshot (P4.3). No App / GPU
// dependency, so this compiles into the headless test as well as the app.
#include "app/runtime_health.h"

namespace vivid {

Severity severity(const HealthSnapshot& s) {
    // A lost device or a graph node pointing at an operator that no longer exists are
    // hard breakages — the render can't produce correct output.
    if (!s.gpu_ok || s.missing_ops > 0) return Severity::Error;
    // ADR-0031: sustained render bail-to-silence (oversized blocks) is an audible dropout — a hard
    // fault. Threshold is set by collect from budgets; 0 means "no audio data", so never raise on it.
    // Only oversized blocks feed this — an idle/empty session's by-design silence is not a bailout.
    if (s.audio_bailout_error_threshold > 0 && s.audio_render_bailouts >= s.audio_bailout_error_threshold)
        return Severity::Error;
    // Recoverable-but-noteworthy: the GPU reported (and survived) errors, an operator reported a
    // runtime problem (a failed-init op rendering black, or a soft notice like Render3D's light
    // ceiling), or the agent control surface isn't up. Warning, not Error — the app keeps running and
    // the channel carries soft notices too, so it must not spuriously red-alert a whole session.
    if (s.gpu_errors > 0 || s.errored_ops > 0 || !s.control_running) return Severity::Warning;
    // ADR-0031: over-budget callbacks or skipped try_lock handoffs are recoverable realtime pressure —
    // a passive Warning (frame.cpp only promotes Error to a toast, so this never nags).
    if (s.audio_over_budget > 0 || s.audio_handoff_skips > 0) return Severity::Warning;
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
    j["audio"]   = { {"session_active", s.audio_session_active},
                     {"callbacks", s.audio_callbacks},
                     {"render_bailouts", s.audio_render_bailouts},
                     {"over_budget", s.audio_over_budget},
                     {"handoff_skips", s.audio_handoff_skips},
                     {"last_callback_us", s.audio_last_callback_us},
                     {"max_callback_us", s.audio_max_callback_us},
                     {"device_open", s.audio_device_open},
                     {"device_name", s.audio_device_name},
                     {"device_sample_rate", s.audio_device_sr},
                     {"device_period", s.audio_device_period},
                     {"device_fallback", s.audio_device_fallback},
                     {"device_latency_frames", s.audio_device_latency_frames},
                     {"device_latency_ms", s.audio_device_sr
                          ? s.audio_device_latency_frames * 1000.0 / s.audio_device_sr : 0.0},
                     {"max_plugin_latency_samples", s.audio_max_plugin_latency_samples},
                     {"max_plugin_latency_ms", s.audio_device_sr
                          ? s.audio_max_plugin_latency_samples * 1000.0 / s.audio_device_sr : 0.0},
                     {"plugin_latency_unknown", s.audio_plugin_latency_unknown},
                     {"input_open", s.audio_input_open},
                     {"input_name", s.audio_input_name},
                     {"input_latency_frames", s.audio_input_latency_frames},
                     {"input_latency_ms", s.audio_device_sr
                          ? s.audio_input_latency_frames * 1000.0 / s.audio_device_sr : 0.0},
                     {"input_level", s.audio_input_level},
                     {"pdc_enabled", s.pdc_enabled},
                     {"pdc_applied_delay_ms", s.audio_device_sr
                          ? s.pdc_applied_delay_samples * 1000.0 / s.audio_device_sr : 0.0},
                     {"pdc_tracks_compensated", s.pdc_tracks_compensated},
                     {"pdc_tracks_live", s.pdc_tracks_live},
                     {"pdc_clamped", s.pdc_clamped} };
    j["gpu"]     = { {"ok", s.gpu_ok}, {"errors", s.gpu_errors} };
    if (!s.gpu_last_error.empty()) j["gpu"]["last_error"] = s.gpu_last_error;
    j["graph"]   = { {"op_nodes", s.op_nodes}, {"op_types", s.op_types},
                     {"missing_ops", s.missing_ops}, {"errored_ops", s.errored_ops},
                     {"output_fed", s.output_fed} };
    j["packages"] = { {"loaded", s.packages_loaded} };
    j["control"]  = { {"running", s.control_running} };
    return j;
}

}  // namespace vivid
