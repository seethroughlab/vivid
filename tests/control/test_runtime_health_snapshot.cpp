#include "runtime/core/runtime_core.h"
#include "runtime/core/runtime_health.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_registry.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <filesystem>
#include <string>
#include "test_helpers.h"

using vivid::runtime_health::RuntimeHealthSnapshot;
using vivid::runtime_health::Severity;
using vivid::runtime_health::Finding;
using vivid::runtime_health::apply_severity_rules;
using vivid::runtime_health::collect;
using vivid::runtime_health::to_json;
using vivid::runtime_health::severity_name;

// Helper: find a finding by code, return nullptr if not present.
static const Finding* find_finding(const RuntimeHealthSnapshot& snap, const std::string& code) {
    for (const auto& f : snap.findings) {
        if (f.code == code) return &f;
    }
    return nullptr;
}

int main() {
    // ─────────────────────────────────────────────────────────────────────
    // Severity rollup — pure-function tests against constructed snapshots.
    // ─────────────────────────────────────────────────────────────────────

    {
        std::fprintf(stderr, "\n=== Test 1: Clean snapshot → Ok ===\n");
        RuntimeHealthSnapshot snap;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Ok, "overall is Ok");
        check(snap.findings.empty(), "no findings");
    }

    {
        std::fprintf(stderr, "\n=== Test 2: Missing operator → Error ===\n");
        RuntimeHealthSnapshot snap;
        snap.graph.declared_nodes = 3;
        snap.graph.compiled_nodes = 3;
        snap.graph.missing_operators = 1;
        snap.graph.missing_operator_types = {"Wavetable"};
        apply_severity_rules(snap);
        check(snap.overall == Severity::Error, "overall is Error");
        const Finding* f = find_finding(snap, "missing_required_operators");
        check(f != nullptr, "has missing_required_operators finding");
        check(f && f->subject == "Wavetable", "finding subject is Wavetable");
    }

    {
        std::fprintf(stderr, "\n=== Test 3: Dropped connection → Warning ===\n");
        RuntimeHealthSnapshot snap;
        snap.graph.declared_nodes = 2;
        snap.graph.compiled_nodes = 2;
        snap.graph.dropped_connections = 1;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Warning, "overall is Warning");
        check(find_finding(snap, "dropped_connections") != nullptr,
              "has dropped_connections finding");
    }

    {
        std::fprintf(stderr, "\n=== Test 4: GPU device lost → Fatal ===\n");
        RuntimeHealthSnapshot snap;
        snap.gpu.device_lost = true;
        snap.gpu.last_error = "out of memory";
        apply_severity_rules(snap);
        check(snap.overall == Severity::Fatal, "overall is Fatal");
        const Finding* f = find_finding(snap, "gpu_device_lost");
        check(f != nullptr, "has gpu_device_lost finding");
        check(f && f->message.find("out of memory") != std::string::npos,
              "finding message includes the last error");
    }

    {
        std::fprintf(stderr, "\n=== Test 5: Severity precedence (fatal + warning) ===\n");
        RuntimeHealthSnapshot snap;
        snap.gpu.device_lost = true;
        snap.graph.dropped_connections = 1;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Fatal, "fatal beats warning");
        check(find_finding(snap, "gpu_device_lost") != nullptr, "fatal finding present");
        check(find_finding(snap, "dropped_connections") != nullptr,
              "warning finding still present");
    }

    {
        std::fprintf(stderr, "\n=== Test 6: Audio nodes but engine not running → Error ===\n");
        RuntimeHealthSnapshot snap;
        snap.audio.running = false;
        snap.graph.audio_nodes = 2;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Error, "overall is Error");
        check(find_finding(snap, "audio_not_running") != nullptr,
              "has audio_not_running finding");
    }

    {
        std::fprintf(stderr, "\n=== Test 6b: prior_crash_operator → Fatal recovered_from_crash finding ===\n");
        RuntimeHealthSnapshot snap;
        snap.prior_crash_operator = "Mixer";
        apply_severity_rules(snap);
        check(snap.overall == Severity::Fatal, "prior crash rolls up to Fatal");
        const Finding* f = find_finding(snap, "recovered_from_crash");
        check(f != nullptr, "has recovered_from_crash finding");
        check(f && f->subject == "Mixer", "subject is the offending operator name");
        check(f && f->message.find("Mixer") != std::string::npos,
              "message names the offending operator");
    }

    {
        std::fprintf(stderr, "\n=== Test 6c: hot_reload required-op failure → Error ===\n");
        RuntimeHealthSnapshot snap;
        snap.hot_reload.last_attempt_succeeded = false;
        snap.hot_reload.last_target = "pkg:vivid-wavetable:wavetable";
        snap.hot_reload.last_error = "build error";
        snap.hot_reload.affects_current_graph = true;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Error, "required-op reload failure rolls up to Error");
        const Finding* f = find_finding(snap, "hot_reload_failed_required");
        check(f != nullptr, "has hot_reload_failed_required finding");
        check(f && f->subject.find("wavetable") != std::string::npos,
              "subject names the failing target");
    }

    {
        std::fprintf(stderr, "\n=== Test 6d: hot_reload stale-op failure → Warning ===\n");
        RuntimeHealthSnapshot snap;
        snap.hot_reload.last_attempt_succeeded = false;
        snap.hot_reload.last_target = "pkg:vivid-extras:unused_op";
        snap.hot_reload.affects_current_graph = false;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Warning, "stale-op reload failure rolls up to Warning");
        check(find_finding(snap, "hot_reload_failed_stale") != nullptr,
              "has hot_reload_failed_stale finding");
        check(find_finding(snap, "hot_reload_failed_required") == nullptr,
              "does NOT also emit required finding");
    }

    {
        std::fprintf(stderr, "\n=== Test 6e: package_version_mismatch → Warning ===\n");
        RuntimeHealthSnapshot snap;
        snap.packages.installed = 3;
        snap.packages.incompatible_updates = 1;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Warning, "incompatible update rolls up to Warning");
        check(find_finding(snap, "package_version_mismatch") != nullptr,
              "has package_version_mismatch finding");
    }

    {
        std::fprintf(stderr, "\n=== Test 6f: audio clipping → Warning ===\n");
        RuntimeHealthSnapshot snap;
        snap.audio.clipping_count = 5;
        snap.audio.peak_max = 1.02;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Warning, "clipping rolls up to Warning");
        const Finding* f = find_finding(snap, "audio_clipping");
        check(f != nullptr, "has audio_clipping finding");
        check(f && f->message.find("5") != std::string::npos,
              "message includes clipping count");
    }

    {
        std::fprintf(stderr, "\n=== Test 6g: mcp_main stale → Warning, opdev never-pinged → silent ===\n");
        RuntimeHealthSnapshot snap;
        snap.mcp.main_last_ping_ms = 100;
        snap.mcp.now_ms = 100 + vivid::runtime_health::kMcpStaleMs + 1;
        snap.mcp.main_connected = false;  // explicitly stale
        snap.mcp.opdev_last_ping_ms = 0;  // never pinged → must NOT fire
        snap.mcp.opdev_connected = false;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Warning, "stale main MCP rolls up to Warning");
        check(find_finding(snap, "mcp_main_disconnected") != nullptr,
              "has mcp_main_disconnected finding");
        check(find_finding(snap, "mcp_opdev_disconnected") == nullptr,
              "never-pinged opdev does NOT emit a finding");
    }

    {
        std::fprintf(stderr, "\n=== Test 6h: both MCP servers stale → Warning with both findings ===\n");
        RuntimeHealthSnapshot snap;
        snap.mcp.main_last_ping_ms = 100;
        snap.mcp.opdev_last_ping_ms = 200;
        snap.mcp.now_ms = 1'000'000;
        snap.mcp.main_connected = false;
        snap.mcp.opdev_connected = false;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Warning, "both stale rolls up to Warning");
        check(find_finding(snap, "mcp_main_disconnected") != nullptr,
              "main finding present");
        check(find_finding(snap, "mcp_opdev_disconnected") != nullptr,
              "opdev finding present");
    }

    {
        std::fprintf(stderr, "\n=== Test 6i: sustained_silence flag → Warning + finding ===\n");
        RuntimeHealthSnapshot snap;
        snap.audio.silence_active = true;
        snap.audio.silence_window_seconds = 4.2;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Warning, "silence rolls up to Warning");
        check(find_finding(snap, "sustained_silence") != nullptr,
              "has sustained_silence finding");
    }

    {
        std::fprintf(stderr, "\n=== Test 6j: sustained_black flag → Warning + finding ===\n");
        RuntimeHealthSnapshot snap;
        snap.gpu.black_active = true;
        snap.gpu.black_window_seconds = 3.5;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Warning, "black rolls up to Warning");
        check(find_finding(snap, "sustained_black") != nullptr,
              "has sustained_black finding");
    }

    {
        std::fprintf(stderr, "\n=== Test 7: Audio xruns + lane overflow + shader errors → Warning ===\n");
        RuntimeHealthSnapshot snap;
        snap.audio.xruns = 3;
        snap.audio.lane_overflow_count = 1;
        snap.gpu.shader_errors = 2;
        apply_severity_rules(snap);
        check(snap.overall == Severity::Warning, "overall is Warning");
        check(find_finding(snap, "audio_underruns") != nullptr, "audio_underruns present");
        check(find_finding(snap, "lane_overflow") != nullptr, "lane_overflow present");
        check(find_finding(snap, "shader_errors") != nullptr, "shader_errors present");
    }

    // ─────────────────────────────────────────────────────────────────────
    // JSON serializer — back-compat shape and round-trip.
    // ─────────────────────────────────────────────────────────────────────

    {
        std::fprintf(stderr, "\n=== Test 8: to_json shape (back-compat) ===\n");
        RuntimeHealthSnapshot snap;
        snap.audio.running = true;
        snap.audio.sample_rate = 48000;
        snap.audio.buffer_size = 256;
        snap.audio.node_count = 4;
        snap.audio.xruns = 0;
        snap.audio.load = 0.42;
        snap.graph.declared_nodes = 5;
        snap.graph.compiled_nodes = 5;
        apply_severity_rules(snap);

        auto j = to_json(snap);

        // Top-level keys (back-compat: audio/graph/gpu; new: severity/findings)
        check(j.contains("audio"), "json has audio");
        check(j.contains("graph"), "json has graph");
        check(j.contains("gpu"), "json has gpu");
        check(j.contains("severity"), "json has severity (new)");
        check(j.contains("findings"), "json has findings (new)");

        // Audio fields preserved verbatim from previous handler shape.
        check(j["audio"].contains("running"), "audio.running");
        check(j["audio"].contains("sample_rate"), "audio.sample_rate");
        check(j["audio"].contains("buffer_size"), "audio.buffer_size");
        check(j["audio"].contains("node_count"), "audio.node_count");
        check(j["audio"].contains("xruns"), "audio.xruns");
        check(j["audio"].contains("last_buffer_underrun"), "audio.last_buffer_underrun");
        check(j["audio"].contains("load"), "audio.load");
        check(j["audio"].contains("top_nodes"), "audio.top_nodes");
        check(j["audio"].contains("top_lane_state_nodes"), "audio.top_lane_state_nodes");
        check(j["audio"].contains("lane_overflow_count"), "audio.lane_overflow_count (new)");
        check(j["audio"]["sample_rate"].get<int>() == 48000, "audio.sample_rate value");

        // Graph fields preserved verbatim.
        for (const char* k : {"declared_nodes", "declared_connections", "compiled_nodes",
                              "frame_nodes", "audio_nodes", "total_edges", "frame_edges",
                              "audio_edges", "snapshot_edges", "dropped_connections",
                              "errored_nodes", "missing_operators"}) {
            std::string msg = std::string("graph.") + k;
            check(j["graph"].contains(k), msg.c_str());
        }
        check(j["graph"].contains("missing_operator_types"),
              "graph.missing_operator_types (new)");

        // GPU fields preserved verbatim.
        check(j["gpu"].contains("texture_nodes"), "gpu.texture_nodes");
        check(j["gpu"].contains("shader_errors"), "gpu.shader_errors");
        check(j["gpu"].contains("device_lost"), "gpu.device_lost (new)");
        check(j["gpu"].contains("last_error"), "gpu.last_error (new)");

        check(j["severity"].get<std::string>() == "ok", "severity is 'ok'");
        check(j["findings"].is_array(), "findings is an array");
        check(j["findings"].empty(), "findings is empty when ok");
    }

    {
        std::fprintf(stderr, "\n=== Test 9: to_json round-trip determinism ===\n");
        RuntimeHealthSnapshot snap;
        snap.audio.xruns = 5;
        snap.graph.dropped_connections = 2;
        apply_severity_rules(snap);
        auto a = to_json(snap).dump();
        auto b = to_json(snap).dump();
        check(a == b, "two serializations of the same snapshot are byte-identical");

        auto parsed = nlohmann::json::parse(a);
        check(parsed["severity"].get<std::string>() == "warning",
              "severity is 'warning' after rollup");
    }

    {
        std::fprintf(stderr, "\n=== Test 10: severity_name() round-trips through JSON ===\n");
        check(std::string(severity_name(Severity::Ok)) == "ok", "Ok → ok");
        check(std::string(severity_name(Severity::Warning)) == "warning", "Warning → warning");
        check(std::string(severity_name(Severity::Error)) == "error", "Error → error");
        check(std::string(severity_name(Severity::Fatal)) == "fatal", "Fatal → fatal");
    }

    // ─────────────────────────────────────────────────────────────────────
    // collect() integration — drive a real RuntimeCore, no audio/gpu.
    // ─────────────────────────────────────────────────────────────────────

    {
        std::fprintf(stderr, "\n=== Test 11: collect() on a clean compiled graph ===\n");
        std::string staging = "./.test_runtime_health_staging";
        std::filesystem::create_directories(staging);
        std::filesystem::copy_file("test_op_v1.dylib", staging + "/test_op_v1.dylib",
            std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file("control_pass_op.dylib", staging + "/control_pass_op.dylib",
            std::filesystem::copy_options::overwrite_existing);

        vivid::OperatorRegistry registry;
        check(registry.scan(staging.c_str()), "registry.scan() succeeds");

        vivid::Graph g;
        g.add_node("a", "TestOp", {{"scale", 1.0f}});
        g.add_node("b", "ControlPassOp", {{"gain", 2.0f}});
        g.add_connection("a", "out", "b", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");
        runtime.tick(0.0, 0.016, 0);

        auto snap = collect(g, runtime, registry, /*audio=*/nullptr, /*gpu=*/nullptr);
        check(snap.overall == Severity::Ok, "clean graph rolls up to Ok");
        check(snap.findings.empty(), "no findings on clean graph");
        check(snap.graph.declared_nodes == 2, "declared_nodes == 2");
        check(snap.graph.compiled_nodes == 2, "compiled_nodes == 2");
        check(snap.graph.missing_operators == 0, "no missing operators");
        check(snap.graph.dropped_connections == 0, "no dropped connections");
        runtime.shutdown();
    }

    {
        std::fprintf(stderr, "\n=== Test 12: collect() surfaces a missing operator ===\n");
        std::string staging = "./.test_runtime_health_staging";
        // staging dir already populated by Test 11
        vivid::OperatorRegistry registry;
        check(registry.scan(staging.c_str()), "registry.scan() succeeds");

        vivid::Graph g;
        g.add_node("a", "TestOp", {{"scale", 1.0f}});
        g.add_node("ghost", "DefinitelyNotARegisteredOperator", {});

        vivid::RuntimeCore runtime;
        // build() may still return true with a placeholder for the missing node.
        runtime.build(g, registry);

        auto snap = collect(g, runtime, registry, nullptr, nullptr);
        check(snap.graph.missing_operators >= 1, "missing_operators >= 1");
        check(!snap.graph.missing_operator_types.empty(), "missing_operator_types non-empty");
        check(snap.overall == Severity::Error, "missing op rolls up to Error");
        check(find_finding(snap, "missing_required_operators") != nullptr,
              "has missing_required_operators finding");
        runtime.shutdown();
    }

    {
        std::fprintf(stderr, "\n=== Test 13: collect() on a runtime with no compiled graph ===\n");
        vivid::OperatorRegistry registry;
        vivid::Graph g;
        vivid::RuntimeCore runtime;
        // Don't call build() — compiled_graph() returns nullptr.
        auto snap = collect(g, runtime, registry, nullptr, nullptr);
        check(snap.overall == Severity::Ok, "empty runtime rolls up to Ok");
        check(snap.findings.empty(), "empty runtime has no findings");
        check(snap.graph.compiled_nodes == 0, "compiled_nodes == 0");
        check(snap.graph.declared_nodes == 0, "declared_nodes == 0");
    }

    if (failures == 0) {
        std::fprintf(stderr, "\nAll tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d failure(s).\n", failures);
    return 1;
}
