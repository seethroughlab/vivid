#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace vivid {

class AudioEngine;
class Graph;
class GpuContext;
class OperatorRegistry;
class PackageCatalog;
class RuntimeCore;

namespace runtime_health {

// MCP server reachability staleness threshold. Shared by the diagnostics
// panel (UI) and runtime_health::collect() so both surfaces agree on what
// "disconnected" means.
inline constexpr uint64_t kMcpStaleMs = 30000;

// ---------------------------------------------------------------------------
// RuntimeHealthSnapshot
//
// In-process aggregation of runtime health signals already tracked across
// the runtime (no new probes). Built by `collect()`, serialized by `to_json()`.
//
// Used by:
//   - control_server_checks::handle_run_diagnostics — emits the `health`
//     block (back-compat shape preserved as a strict subset).
//   - Future `get_runtime_health` endpoint (Phase 4) — same serializer.
//
// `collect()` is a pure function of the references it receives. None of
// the inputs are stored, no background work is started.
// ---------------------------------------------------------------------------

enum class Severity { Ok, Warning, Error, Fatal };

const char* severity_name(Severity s);

struct Finding {
    std::string code;        // stable identifier, e.g. "missing_required_operators"
    Severity severity = Severity::Ok;
    std::string subject;     // node id, operator type, or "" for system-wide
    std::string message;     // human description
};

struct AudioNodeHealth {
    std::string node_id;
    std::string type;
    int64_t last_block_total_us = 0;
    int64_t last_process_us = 0;
    int64_t ema_block_us = 0;
    double  last_block_budget_pct = 0.0;
    int64_t last_lane_count = 0;
    int64_t lane_state_entries = 0;
};

struct AudioHealth {
    bool running = false;
    int  sample_rate = 0;
    int  buffer_size = 0;
    int  node_count = 0;
    uint32_t xruns = 0;
    bool last_buffer_underrun = false;
    double load = 0.0;
    uint32_t lane_overflow_count = 0;
    double  peak_max = 0.0;          // max |sample| across all nodes/channels in active snapshot
    int64_t clipping_count = 0;      // count of |sample| >= 0.99 in active snapshot
    double  silence_window_seconds = 0.0;  // span of in-window samples
    bool    silence_active = false;        // sustained silence detected (Phase 8c)
    std::vector<AudioNodeHealth> top_nodes;
    std::vector<AudioNodeHealth> top_lane_state_nodes;
};

struct GraphHealth {
    int64_t declared_nodes = 0;
    int64_t declared_connections = 0;
    int64_t compiled_nodes = 0;
    int64_t frame_nodes = 0;
    int64_t audio_nodes = 0;
    int64_t total_edges = 0;
    int64_t frame_edges = 0;
    int64_t audio_edges = 0;
    int64_t snapshot_edges = 0;
    int64_t dropped_connections = 0;
    int64_t errored_nodes = 0;
    int64_t missing_operators = 0;
    std::vector<std::string> missing_operator_types;  // unique sorted list
};

struct GpuHealth {
    int64_t texture_nodes = 0;
    int64_t shader_errors = 0;
    bool device_lost = false;
    std::string last_error;
    double black_window_seconds = 0.0;  // span of in-window samples
    bool   black_active = false;        // sustained black detected (Phase 8c)
};

// Most-recent hot-reload outcome. Set when RuntimeCore has received at least
// one ReloadResult; until then `last_target` is empty. `affects_current_graph`
// distinguishes "stale operator" (Warning) from "operator currently in graph"
// (Error).
struct HotReloadHealth {
    bool last_attempt_succeeded = true;
    std::string last_target;
    std::string last_error;
    bool affects_current_graph = false;
};

// Package catalog snapshot. Populated only when collect() receives a non-null
// PackageCatalog*. `incompatible_updates` is the count of installed packages
// whose latest available version is incompatible with the current core build.
struct PackagesHealth {
    int64_t installed = 0;
    int64_t updates_available = 0;
    int64_t incompatible_updates = 0;
};

// MCP server reachability snapshot. Computed from the optional `McpStatus`
// argument to `collect()`. `*_connected` is true iff the corresponding ping
// timestamp is non-zero AND within `kMcpStaleMs` of `now_ms`.
struct McpHealth {
    uint64_t main_last_ping_ms = 0;
    uint64_t opdev_last_ping_ms = 0;
    uint64_t now_ms = 0;
    bool main_connected = false;
    bool opdev_connected = false;
};

// Argument bundle for MCP-aware collect() callers. Default-constructed
// instances mean "no MCP signal" — the snapshot reports both servers as
// never-pinged and severity rules skip them.
struct McpStatus {
    uint64_t main_ping_ms = 0;
    uint64_t opdev_ping_ms = 0;
    uint64_t now_ms = 0;     // 0 → resolved internally via steady_clock::now()
};

struct RuntimeHealthSnapshot {
    Severity overall = Severity::Ok;
    std::vector<Finding> findings;
    AudioHealth audio;
    GraphHealth graph;
    GpuHealth   gpu;
    HotReloadHealth hot_reload;
    PackagesHealth  packages;
    McpHealth   mcp;
    std::string prior_crash_operator; // empty unless previous run crashed
};

// Lightweight rollup for per-frame UI consumption. Skips the expensive
// per-node audio top-N aggregation that `collect()` does, returning just
// what the diagnostics pill / status-bar dot need.
struct RuntimeHealthSummary {
    Severity overall = Severity::Ok;
    int finding_count = 0;
};

// Aggregator. AudioEngine, GpuContext, and PackageCatalog are nullable —
// callers without a running audio device, GPU, or package system pass nullptr;
// the corresponding sub-blocks keep their default (zero) values.
// `prior_crash_operator` is read from `core.prior_crash_operator()` and
// surfaced as a Fatal finding when set.
RuntimeHealthSnapshot collect(const Graph& graph,
                              const RuntimeCore& core,
                              const OperatorRegistry& registry,
                              const AudioEngine* audio_engine,
                              const GpuContext* gpu_context,
                              const PackageCatalog* package_catalog = nullptr,
                              McpStatus mcp = {});

// Per-frame summary: same severity rollup, no per-node aggregation. Cheap
// enough to call every frame from GraphSnapshotBuilder.
RuntimeHealthSummary collect_summary(const Graph& graph,
                                     const RuntimeCore& core,
                                     const OperatorRegistry& registry,
                                     const AudioEngine* audio_engine,
                                     const GpuContext* gpu_context,
                                     const PackageCatalog* package_catalog = nullptr,
                                     McpStatus mcp = {});

// Apply severity-rollup rules. Pure function of the audio/graph/gpu/
// prior_crash_operator sub-blocks: populates `findings` and `overall`.
// Public so tests can drive rollup without standing up a full RuntimeCore.
// Idempotent.
void apply_severity_rules(RuntimeHealthSnapshot& snap);

// Serialize the snapshot to a JSON object. The shape is a strict superset
// of the `health` block historically emitted by `run_diagnostics` —
// existing keys are preserved exactly; new keys are additive.
nlohmann::json to_json(const RuntimeHealthSnapshot& snap);

}  // namespace runtime_health
}  // namespace vivid
