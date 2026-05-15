#include "runtime/core/runtime_health.h"

#include "runtime/audio/audio_engine.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/core/runtime_core.h"
#include "runtime/gpu/gpu_context.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/snapshot_types.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/packages/package_catalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>

namespace vivid::runtime_health {

const char* severity_name(Severity s) {
    switch (s) {
        case Severity::Ok:      return "ok";
        case Severity::Warning: return "warning";
        case Severity::Error:   return "error";
        case Severity::Fatal:   return "fatal";
    }
    return "ok";
}

namespace {

constexpr size_t kTopAudioNodes = 5;

// Extract the operator type from a hot-reload target name. Targets look like
// `pkg:<package>:<operator>` (package compile path) or `<operator>` (cmake
// target). We need the trailing operator-type segment so we can ask "is this
// type currently in the compiled graph?" — which decides whether the failure
// is an Error (required op) or a Warning (stale op).
std::string operator_type_from_target(const std::string& target) {
    auto pos = target.rfind(':');
    return (pos == std::string::npos) ? target : target.substr(pos + 1);
}

bool target_type_in_graph(const RuntimeCore& core, const std::string& target) {
    const auto* cg = core.compiled_graph();
    if (!cg) return false;
    auto type = operator_type_from_target(target);
    if (type.empty()) return false;
    for (const auto& n : cg->nodes) {
        if (n.type_name == type) return true;
    }
    return false;
}

constexpr float kClipThreshold = 0.99f;

// Shared field-gathering used by both `collect()` and `collect_summary()`.
// Populates everything except the per-node top-N audio aggregation, which
// is the only allocation-heavy work and lives in `collect()` only.
void populate_minimal(RuntimeHealthSnapshot& snap,
                      const Graph& graph,
                      const RuntimeCore& core,
                      const AudioEngine* audio_engine,
                      const GpuContext* gpu_context,
                      const PackageCatalog* package_catalog,
                      McpStatus mcp) {
    if (audio_engine) {
        snap.audio.running = audio_engine->running();
        snap.audio.sample_rate = static_cast<int>(audio_engine->sample_rate());
        snap.audio.buffer_size = static_cast<int>(audio_engine->buffer_size());
        snap.audio.node_count  = static_cast<int>(audio_engine->node_count());
        snap.audio.xruns       = audio_engine->underrun_count();
        snap.audio.last_buffer_underrun = audio_engine->last_buffer_underrun();
        snap.audio.load        = static_cast<double>(audio_engine->audio_load());
        snap.audio.late_delivery_count = audio_engine->late_delivery_count();
        snap.audio.max_delivery_gap_us = audio_engine->max_delivery_gap_us();
    }
    snap.audio.lane_overflow_count = core.audio_frame_bridge().lane_overflow_count();

    // Sustained-silence / black detection — read from the per-frame samplers
    // owned by RuntimeCore. `last_tick_time()` is the most recent `time`
    // passed to tick(), in the same domain the samplers were fed via
    // `sample_runtime_health(time)`.
    {
        const double now = core.last_tick_time();
        const auto& samp = core.health_samplers();
        snap.audio.silence_active = samp.audio_silence_active(now);
        snap.audio.silence_window_seconds = samp.audio_window_seconds(now);
        snap.gpu.black_active = samp.visual_black_active(now);
        snap.gpu.black_window_seconds = samp.visual_window_seconds(now);
    }

    // Audio peak / clipping aggregation from the latest published analysis
    // snapshot (lock-free acquire). Stays at 0 when audio analysis is
    // disabled or no analysis has published yet.
    {
        const auto& analysis = core.audio_frame_bridge().active_analysis();
        double peak_max = 0.0;
        int64_t clipping = 0;
        for (const auto& node_peaks : analysis.peak) {
            for (float v : node_peaks) {
                const float a = std::fabs(v);
                if (a > peak_max) peak_max = static_cast<double>(a);
                if (a >= kClipThreshold) ++clipping;
            }
        }
        snap.audio.peak_max = peak_max;
        snap.audio.clipping_count = clipping;
    }

    // MCP server reachability. Resolve `now_ms` from steady_clock when caller
    // didn't supply one (testability).
    {
        const uint64_t now = (mcp.now_ms != 0) ? mcp.now_ms :
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        snap.mcp.main_last_ping_ms  = mcp.main_ping_ms;
        snap.mcp.opdev_last_ping_ms = mcp.opdev_ping_ms;
        snap.mcp.now_ms = now;
        snap.mcp.main_connected =
            mcp.main_ping_ms > 0 && (now - mcp.main_ping_ms) < kMcpStaleMs;
        snap.mcp.opdev_connected =
            mcp.opdev_ping_ms > 0 && (now - mcp.opdev_ping_ms) < kMcpStaleMs;
    }

    if (gpu_context) {
        snap.gpu.device_lost = gpu_context->device_lost();
        snap.gpu.last_error  = gpu_context->last_error();
    }

    snap.prior_crash_operator = core.prior_crash_operator();

    // Hot-reload most-recent outcome.
    if (const auto& last = core.last_reload(); last.has_value()) {
        snap.hot_reload.last_target = last->target_name;
        snap.hot_reload.last_attempt_succeeded = last->success;
        snap.hot_reload.last_error = last->error_output;
        snap.hot_reload.affects_current_graph =
            !last->success && target_type_in_graph(core, last->target_name);
    }

    // Package catalog snapshot. VIVID_CORE_VERSION is injected as a
    // compile definition by both `vivid` and `vivid_runtime_testlib`; if
    // the macro is missing we fall back to "" (catalog then reports
    // incompatible_updates=0, the pre-Phase-9c behavior).
#ifdef VIVID_CORE_VERSION
    constexpr const char* kCoreVersion = VIVID_CORE_VERSION;
#else
    constexpr const char* kCoreVersion = "";
#endif
    if (package_catalog) {
        auto summary = package_catalog->summarize_updates(kCoreVersion);
        snap.packages.installed = static_cast<int64_t>(summary.installed_packages);
        snap.packages.updates_available = static_cast<int64_t>(summary.updates_available);
        snap.packages.incompatible_updates = static_cast<int64_t>(summary.incompatible_updates);
    }

    snap.graph.declared_nodes       = static_cast<int64_t>(graph.nodes().size());
    snap.graph.declared_connections = static_cast<int64_t>(graph.connections().size());

    const auto* cg = core.compiled_graph();
    if (!cg) return;

    snap.graph.compiled_nodes  = static_cast<int64_t>(cg->nodes.size());
    snap.graph.frame_nodes     = static_cast<int64_t>(cg->frame_order.size());
    snap.graph.audio_nodes     = static_cast<int64_t>(cg->audio_order.size());
    snap.graph.total_edges     = static_cast<int64_t>(cg->edges.size());
    snap.graph.frame_edges     = static_cast<int64_t>(cg->frame_direct_edges.size());
    snap.graph.audio_edges     = static_cast<int64_t>(cg->audio_direct_edges.size());
    snap.graph.snapshot_edges  = static_cast<int64_t>(
        cg->frame_to_audio_edges.size() + cg->audio_to_frame_edges.size());
    snap.graph.dropped_connections = static_cast<int64_t>(cg->dropped_connections.size());

    std::set<std::string> missing_types;
    for (const auto& n : cg->nodes) {
        if (n.errored) snap.graph.errored_nodes++;
        if (n.missing_operator) {
            snap.graph.missing_operators++;
            if (!n.type_name.empty()) missing_types.insert(n.type_name);
        }
        if (n.gpu) {
            snap.gpu.texture_nodes++;
            if (n.gpu->shader_error) snap.gpu.shader_errors++;
        }
    }
    snap.graph.missing_operator_types.assign(missing_types.begin(), missing_types.end());
}

}  // namespace

void apply_severity_rules(RuntimeHealthSnapshot& snap) {
    auto bump = [&](Severity s) {
        if (static_cast<int>(s) > static_cast<int>(snap.overall)) snap.overall = s;
    };

    if (!snap.prior_crash_operator.empty()) {
        snap.findings.push_back({
            "recovered_from_crash", Severity::Fatal,
            snap.prior_crash_operator,
            "Previous run crashed in operator '" + snap.prior_crash_operator
                + "'. Affected nodes may be disabled.",
        });
        bump(Severity::Fatal);
    }

    if (snap.gpu.device_lost) {
        snap.findings.push_back({
            "gpu_device_lost", Severity::Fatal, "",
            snap.gpu.last_error.empty()
                ? std::string("WebGPU device is lost; the runtime cannot render.")
                : "WebGPU device is lost: " + snap.gpu.last_error,
        });
        bump(Severity::Fatal);
    }

    if (snap.graph.missing_operators > 0) {
        std::string subject;
        if (!snap.graph.missing_operator_types.empty()) {
            subject = snap.graph.missing_operator_types.front();
        }
        snap.findings.push_back({
            "missing_required_operators", Severity::Error, subject,
            "Graph references " + std::to_string(snap.graph.missing_operators)
                + " operator type(s) the runtime could not load.",
        });
        bump(Severity::Error);
    }

    if (snap.graph.errored_nodes > 0) {
        snap.findings.push_back({
            "node_runtime_error", Severity::Error, "",
            "One or more compiled nodes are in an error state.",
        });
        bump(Severity::Error);
    }

    if (!snap.audio.running && snap.graph.audio_nodes > 0) {
        snap.findings.push_back({
            "audio_not_running", Severity::Error, "",
            "Graph contains audio nodes but the audio engine is not running.",
        });
        bump(Severity::Error);
    }

    if (snap.graph.dropped_connections > 0) {
        snap.findings.push_back({
            "dropped_connections", Severity::Warning, "",
            std::to_string(snap.graph.dropped_connections)
                + " connection(s) were dropped during compilation.",
        });
        bump(Severity::Warning);
    }

    if (snap.audio.xruns > 0) {
        snap.findings.push_back({
            "audio_underruns", Severity::Warning, "",
            "Audio underruns since session start: " + std::to_string(snap.audio.xruns) + ".",
        });
        bump(Severity::Warning);
    }

    if (snap.audio.lane_overflow_count > 0) {
        snap.findings.push_back({
            "lane_overflow", Severity::Warning, "",
            "Audio bridge lane storage was exceeded "
                + std::to_string(snap.audio.lane_overflow_count) + " time(s).",
        });
        bump(Severity::Warning);
    }

    if (snap.gpu.shader_errors > 0) {
        snap.findings.push_back({
            "shader_errors", Severity::Warning, "",
            std::to_string(snap.gpu.shader_errors) + " GPU node(s) report shader errors.",
        });
        bump(Severity::Warning);
    }

    if (!snap.hot_reload.last_attempt_succeeded && !snap.hot_reload.last_target.empty()) {
        if (snap.hot_reload.affects_current_graph) {
            snap.findings.push_back({
                "hot_reload_failed_required", Severity::Error,
                snap.hot_reload.last_target,
                "Hot reload failed for an operator currently in the graph: '"
                    + snap.hot_reload.last_target + "'.",
            });
            bump(Severity::Error);
        } else {
            snap.findings.push_back({
                "hot_reload_failed_stale", Severity::Warning,
                snap.hot_reload.last_target,
                "Hot reload failed for '" + snap.hot_reload.last_target
                    + "' (not currently in the graph; safe to ignore for this session).",
            });
            bump(Severity::Warning);
        }
    }

    if (snap.packages.incompatible_updates > 0) {
        snap.findings.push_back({
            "package_version_mismatch", Severity::Warning, "",
            std::to_string(snap.packages.incompatible_updates)
                + " installed package(s) have updates incompatible with this core build.",
        });
        bump(Severity::Warning);
    }

    if (snap.audio.clipping_count > 0) {
        snap.findings.push_back({
            "audio_clipping", Severity::Warning, "",
            std::to_string(snap.audio.clipping_count)
                + " audio sample(s) at or above clipping threshold.",
        });
        bump(Severity::Warning);
    }

    // MCP disconnected findings — skipped when never pinged so headless dev
    // doesn't see permanent warnings.
    if (snap.mcp.main_last_ping_ms > 0 && !snap.mcp.main_connected) {
        snap.findings.push_back({
            "mcp_main_disconnected", Severity::Warning, "vivid",
            "MCP server 'vivid' has not pinged in over 30 seconds.",
        });
        bump(Severity::Warning);
    }
    if (snap.mcp.opdev_last_ping_ms > 0 && !snap.mcp.opdev_connected) {
        snap.findings.push_back({
            "mcp_opdev_disconnected", Severity::Warning, "opdev",
            "MCP server 'opdev' has not pinged in over 30 seconds.",
        });
        bump(Severity::Warning);
    }

    if (snap.audio.silence_active) {
        char msg[160];
        std::snprintf(msg, sizeof(msg),
            "Audio output has been silent for %.1fs.",
            snap.audio.silence_window_seconds);
        snap.findings.push_back({
            "sustained_silence", Severity::Warning, "audio", msg,
        });
        bump(Severity::Warning);
    }
    if (snap.gpu.black_active) {
        char msg[160];
        std::snprintf(msg, sizeof(msg),
            "Video output has been black for %.1fs.",
            snap.gpu.black_window_seconds);
        snap.findings.push_back({
            "sustained_black", Severity::Warning, "video", msg,
        });
        bump(Severity::Warning);
    }
}

RuntimeHealthSnapshot collect(const Graph& graph,
                              const RuntimeCore& core,
                              const OperatorRegistry& /*registry*/,
                              const AudioEngine* audio_engine,
                              const GpuContext* gpu_context,
                              const PackageCatalog* package_catalog,
                              McpStatus mcp) {
    RuntimeHealthSnapshot snap;
    populate_minimal(snap, graph, core, audio_engine, gpu_context, package_catalog, mcp);

    // Per-node top-N audio aggregation — only collect() does this; summary
    // skips it to stay cheap enough for per-frame UI consumption.
    if (const auto* cg = core.compiled_graph()) {
        std::vector<AudioNodeHealth> rows;
        rows.reserve(cg->audio_order.size());
        for (uint32_t idx : cg->audio_order) {
            const auto& ns = cg->nodes[idx];
            if (!ns.audio) continue;
            auto debug = read_audio_node_debug(*ns.audio);
            if (!debug.valid) continue;
            rows.push_back({
                ns.node_id,
                ns.type_name,
                static_cast<int64_t>(debug.last_block_total_us),
                static_cast<int64_t>(debug.last_process_us),
                static_cast<int64_t>(debug.ema_block_us),
                static_cast<int64_t>(debug.peak_block_us),
                static_cast<double>(debug.last_block_budget_pct),
                static_cast<int64_t>(debug.last_lane_count),
                static_cast<int64_t>(debug.lane_state_entries),
            });
        }

        auto by_hotness = rows;
        std::sort(by_hotness.begin(), by_hotness.end(),
                  [](const AudioNodeHealth& a, const AudioNodeHealth& b) {
            if (a.ema_block_us != b.ema_block_us) return a.ema_block_us > b.ema_block_us;
            if (a.last_block_total_us != b.last_block_total_us)
                return a.last_block_total_us > b.last_block_total_us;
            return a.node_id < b.node_id;
        });
        for (size_t i = 0; i < by_hotness.size() && i < kTopAudioNodes; ++i)
            snap.audio.top_nodes.push_back(by_hotness[i]);

        auto by_lane_state = rows;
        std::sort(by_lane_state.begin(), by_lane_state.end(),
                  [](const AudioNodeHealth& a, const AudioNodeHealth& b) {
            if (a.lane_state_entries != b.lane_state_entries)
                return a.lane_state_entries > b.lane_state_entries;
            if (a.last_lane_count != b.last_lane_count)
                return a.last_lane_count > b.last_lane_count;
            return a.node_id < b.node_id;
        });
        for (size_t i = 0; i < by_lane_state.size() && i < kTopAudioNodes; ++i)
            snap.audio.top_lane_state_nodes.push_back(by_lane_state[i]);

        // Copy overrun ring (benign racy read — diagnostic only).
        uint32_t widx = cg->overrun_write_idx.load(std::memory_order_relaxed);
        uint32_t nrec = std::min(widx, CompiledGraph::kOverrunRingSize);
        snap.audio.overruns.reserve(nrec);
        for (uint32_t i = 0; i < nrec; ++i) {
            uint32_t slot = (widx - 1 - i) % CompiledGraph::kOverrunRingSize;
            const auto& rec = cg->overrun_ring[slot];
            AudioOverrunSnap s;
            s.callback_frame = rec.callback_frame;
            s.budget_us = rec.budget_us;
            s.actual_us = rec.actual_us;
            for (uint8_t j = 0; j < rec.node_count; ++j) {
                const auto& e = rec.nodes[j];
                s.nodes.push_back({e.node_id, e.total_us, e.process_us, e.lane_count});
            }
            snap.audio.overruns.push_back(std::move(s));
        }
    }

    apply_severity_rules(snap);
    return snap;
}

namespace {

nlohmann::json node_row_to_json(const AudioNodeHealth& row) {
    return nlohmann::json{
        {"node_id", row.node_id},
        {"type", row.type},
        {"last_block_total_us", row.last_block_total_us},
        {"last_process_us", row.last_process_us},
        {"ema_block_us", row.ema_block_us},
        {"peak_block_us", row.peak_block_us},
        {"last_block_budget_pct", row.last_block_budget_pct},
        {"last_lane_count", row.last_lane_count},
        {"lane_state_entries", row.lane_state_entries},
    };
}

}  // namespace

nlohmann::json to_json(const RuntimeHealthSnapshot& snap) {
    nlohmann::json audio = nlohmann::json::object();
    audio["running"] = snap.audio.running;
    audio["sample_rate"] = snap.audio.sample_rate;
    audio["buffer_size"] = snap.audio.buffer_size;
    audio["node_count"] = snap.audio.node_count;
    audio["xruns"] = snap.audio.xruns;
    audio["last_buffer_underrun"] = snap.audio.last_buffer_underrun;
    audio["load"] = snap.audio.load;
    audio["late_delivery_count"] = snap.audio.late_delivery_count;
    audio["max_delivery_gap_us"] = snap.audio.max_delivery_gap_us;
    audio["lane_overflow_count"] = snap.audio.lane_overflow_count;
    audio["peak_max"] = snap.audio.peak_max;
    audio["clipping_count"] = snap.audio.clipping_count;
    audio["silence_window_seconds"] = snap.audio.silence_window_seconds;
    audio["silence_active"] = snap.audio.silence_active;

    nlohmann::json top_nodes = nlohmann::json::array();
    for (const auto& r : snap.audio.top_nodes) top_nodes.push_back(node_row_to_json(r));
    audio["top_nodes"] = std::move(top_nodes);

    nlohmann::json top_lane_state_nodes = nlohmann::json::array();
    for (const auto& r : snap.audio.top_lane_state_nodes)
        top_lane_state_nodes.push_back(node_row_to_json(r));
    audio["top_lane_state_nodes"] = std::move(top_lane_state_nodes);

    nlohmann::json overruns_arr = nlohmann::json::array();
    for (const auto& rec : snap.audio.overruns) {
        nlohmann::json nodes_arr = nlohmann::json::array();
        for (const auto& e : rec.nodes) {
            nodes_arr.push_back({
                {"node_id",    e.node_id},
                {"total_us",   e.total_us},
                {"process_us", e.process_us},
                {"lane_count", e.lane_count},
            });
        }
        overruns_arr.push_back({
            {"callback_frame", rec.callback_frame},
            {"budget_us",      rec.budget_us},
            {"actual_us",      rec.actual_us},
            {"nodes",          std::move(nodes_arr)},
        });
    }
    audio["audio_overruns"] = std::move(overruns_arr);

    nlohmann::json graph_j = nlohmann::json::object();
    graph_j["declared_nodes"] = snap.graph.declared_nodes;
    graph_j["declared_connections"] = snap.graph.declared_connections;
    graph_j["compiled_nodes"] = snap.graph.compiled_nodes;
    graph_j["frame_nodes"] = snap.graph.frame_nodes;
    graph_j["audio_nodes"] = snap.graph.audio_nodes;
    graph_j["total_edges"] = snap.graph.total_edges;
    graph_j["frame_edges"] = snap.graph.frame_edges;
    graph_j["audio_edges"] = snap.graph.audio_edges;
    graph_j["snapshot_edges"] = snap.graph.snapshot_edges;
    graph_j["dropped_connections"] = snap.graph.dropped_connections;
    graph_j["errored_nodes"] = snap.graph.errored_nodes;
    graph_j["missing_operators"] = snap.graph.missing_operators;
    graph_j["missing_operator_types"] = snap.graph.missing_operator_types;

    nlohmann::json gpu_j = nlohmann::json::object();
    gpu_j["texture_nodes"] = snap.gpu.texture_nodes;
    gpu_j["shader_errors"] = snap.gpu.shader_errors;
    gpu_j["device_lost"] = snap.gpu.device_lost;
    gpu_j["last_error"] = snap.gpu.last_error;
    gpu_j["black_window_seconds"] = snap.gpu.black_window_seconds;
    gpu_j["black_active"] = snap.gpu.black_active;

    nlohmann::json hot_reload_j = nlohmann::json::object();
    hot_reload_j["last_attempt_succeeded"] = snap.hot_reload.last_attempt_succeeded;
    hot_reload_j["last_target"] = snap.hot_reload.last_target;
    hot_reload_j["last_error"] = snap.hot_reload.last_error;
    hot_reload_j["affects_current_graph"] = snap.hot_reload.affects_current_graph;

    nlohmann::json packages_j = nlohmann::json::object();
    packages_j["installed"] = snap.packages.installed;
    packages_j["updates_available"] = snap.packages.updates_available;
    packages_j["incompatible_updates"] = snap.packages.incompatible_updates;

    nlohmann::json mcp_j = nlohmann::json::object();
    mcp_j["main_last_ping_ms"] = snap.mcp.main_last_ping_ms;
    mcp_j["opdev_last_ping_ms"] = snap.mcp.opdev_last_ping_ms;
    mcp_j["now_ms"] = snap.mcp.now_ms;
    mcp_j["main_connected"] = snap.mcp.main_connected;
    mcp_j["opdev_connected"] = snap.mcp.opdev_connected;

    nlohmann::json findings_arr = nlohmann::json::array();
    for (const auto& f : snap.findings) {
        findings_arr.push_back({
            {"code", f.code},
            {"severity", severity_name(f.severity)},
            {"subject", f.subject},
            {"message", f.message},
        });
    }

    nlohmann::json out = nlohmann::json::object();
    out["audio"] = std::move(audio);
    out["graph"] = std::move(graph_j);
    out["gpu"] = std::move(gpu_j);
    out["hot_reload"] = std::move(hot_reload_j);
    out["packages"] = std::move(packages_j);
    out["mcp"] = std::move(mcp_j);
    out["severity"] = severity_name(snap.overall);
    out["findings"] = std::move(findings_arr);
    return out;
}

RuntimeHealthSummary collect_summary(const Graph& graph,
                                     const RuntimeCore& core,
                                     const OperatorRegistry& /*registry*/,
                                     const AudioEngine* audio_engine,
                                     const GpuContext* gpu_context,
                                     const PackageCatalog* package_catalog,
                                     McpStatus mcp) {
    // Per-frame friendly: gather every field except the per-node top-N audio
    // aggregation (which collect() does). The rollup runs against the full
    // signal set so summary.overall agrees with collect()'s overall.
    RuntimeHealthSnapshot snap;
    populate_minimal(snap, graph, core, audio_engine, gpu_context, package_catalog, mcp);
    apply_severity_rules(snap);
    return RuntimeHealthSummary{
        snap.overall,
        static_cast<int>(snap.findings.size()),
    };
}

}  // namespace vivid::runtime_health
