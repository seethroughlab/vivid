// The live-state half of the runtime-health snapshot (P4.3): reads App + its subsystems.
// Kept separate from runtime_health.cpp so the pure rollup/serialization stay App-free
// (and headless-testable). Only the app target compiles this file.
#include "app/runtime_health.h"
#include "app/app.h"
#include "transport.h"             // ADR-0032 Phase D1: read transport->input_level for the meter
#include "gpu/gpu_context.h"
#include "gpu/visual_graph.h"
#include "ui/node_graph.h"
#include "cli/control_server.h"
#include "audio/audio_health.h"    // ADR-0031: RT health counters (read the atomics)
#include "audio/audio_budgets.h"   // ADR-0031: bailout Error threshold
#include "audio/audio_device_manager.h"  // ADR-0032 Phase A: active output-device status
#include "audio/vst3_host.h"             // ADR-0032 Phase B: plugin-latency session accessors
#include "version.h"

#include <cstdint>

namespace vivid {

HealthSnapshot collect_health(const App& app) {
    HealthSnapshot s;
    s.app_version = VIVID_VERSION;

    s.audio_session_active = app.session != nullptr;

    if (app.gpu) {
        s.gpu_ok         = !app.gpu->device_lost();
        s.gpu_errors     = app.gpu->error_count();
        s.gpu_last_error = app.gpu->last_error();
    }

    s.op_types        = static_cast<int>(app.op_registry.type_names().size());
    s.packages_loaded = static_cast<int>(app.op_loaders.size());

    if (app.graph) s.op_nodes = app.graph->op_count();
    // BROKEN nodes: op types that never resolved to a real operator. The visual graph is the
    // authority (it excludes the Output/Video host contracts, which carry no operator yet are not
    // "missing" — counting them here made severity() spuriously Error in every session).
    if (app.vgraph) s.missing_ops = app.vgraph->missing_op_count();
    // Structural blank-vs-empty signal (P2-03): does a producer feed the active Output?
    if (app.vgraph) s.output_fed = app.vgraph->output_has_feed();

    s.control_running = app.control && app.control->running();

    // ADR-0031 §4: roll the RT audio-health atomics into the snapshot as per-frame DELTAS. collect runs
    // only on the frame thread, so plain function-local statics hold the last-seen totals (single caller).
    namespace ah = vivid::audio::health;
    static uint64_t last_cb = 0, last_bail = 0, last_ob = 0, last_skip = 0;
    const uint64_t cb   = ah::g_callbacks.load(std::memory_order_relaxed);
    const uint64_t bail = ah::g_render_bailouts.load(std::memory_order_relaxed);
    const uint64_t ob   = ah::g_over_budget.load(std::memory_order_relaxed);
    const uint64_t sk   = ah::g_handoff_skips.load(std::memory_order_relaxed);
    s.audio_callbacks       = cb   - last_cb;   last_cb   = cb;
    s.audio_render_bailouts = bail - last_bail; last_bail = bail;
    s.audio_over_budget     = ob   - last_ob;   last_ob   = ob;
    s.audio_handoff_skips   = sk   - last_skip; last_skip = sk;
    s.audio_last_callback_us = ah::g_last_callback_us.load(std::memory_order_relaxed);
    s.audio_max_callback_us  = ah::g_max_callback_us.load(std::memory_order_relaxed);
    s.audio_bailout_error_threshold = vivid::audio::audio_budgets().bailout_error_count;

    // ADR-0032 Phase A: the active output device (null when headless — the app runs without audio).
    if (app.audio_devices) {
        const auto& d = app.audio_devices->status();
        s.audio_device_open     = d.open;
        s.audio_device_name     = d.active_name;
        s.audio_device_sr       = d.actual_sample_rate;
        s.audio_device_period   = d.actual_period;
        s.audio_device_fallback = d.using_fallback;
        s.audio_device_latency_frames = d.output_latency_frames;
        // ADR-0032 Phase D1: the input (capture) side of a duplex device.
        s.audio_input_open           = d.input_open;
        s.audio_input_name           = d.input_active_name;
        s.audio_input_latency_frames = d.input_latency_frames;
    }
    if (app.transport)
        s.audio_input_level = app.transport->input_level.load(std::memory_order_relaxed);
    // ADR-0032 Phase B: plugin-reported latency (read once at activate; native ops contribute 0).
    if (app.session) {
        s.audio_max_plugin_latency_samples =
            static_cast<uint32_t>(vivid::session::session_max_plugin_latency_samples(app.session));
        s.audio_plugin_latency_unknown =
            vivid::session::session_any_plugin_latency_unknown(app.session) != 0;
        // ADR-0032 Phase E1: PDC state (published by pdc_recompute).
        s.pdc_enabled              = vivid::session::session_pdc_enabled(app.session);
        s.pdc_applied_delay_samples = static_cast<uint32_t>(vivid::session::session_pdc_applied_delay(app.session));
        s.pdc_tracks_compensated   = vivid::session::session_pdc_tracks_compensated(app.session);
        s.pdc_tracks_live          = vivid::session::session_pdc_tracks_live(app.session);
        s.pdc_clamped              = vivid::session::session_pdc_clamped(app.session) != 0;
    }
    return s;
}

}  // namespace vivid
