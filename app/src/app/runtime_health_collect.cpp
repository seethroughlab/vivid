// The live-state half of the runtime-health snapshot (P4.3): reads App + its subsystems.
// Kept separate from runtime_health.cpp so the pure rollup/serialization stay App-free
// (and headless-testable). Only the app target compiles this file.
#include "app/runtime_health.h"
#include "app/app.h"
#include "gpu/gpu_context.h"
#include "gpu/visual_graph.h"
#include "ui/node_graph.h"
#include "cli/control_server.h"
#include "version.h"

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
    return s;
}

}  // namespace vivid
