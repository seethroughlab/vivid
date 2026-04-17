// Crash-recovery HTTP endpoint tests (Phase 5).
//
// Minimal ControlServer setup (no operators, no packages) just to exercise
// the three Phase 5 routes in isolation:
//   - get_last_crash
//   - clear_last_crash
//   - load_graph_safe_mode
//
// Full-runtime load tests are deferred to manual smoke (the happy path
// requires a real graph file + operators).

#include "runtime/audio/audio_engine.h"
#include "runtime/control/control_server.h"
#include "runtime/control/runtime_api.h"
#include "runtime/core/crash_recovery.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_registry.h"

#include "test_helpers.h"

#include <nlohmann/json.hpp>
#include <ixwebsocket/IXHttpClient.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;

struct Response {
    json j;
    bool ok = false;
};

static Response post_json(ix::HttpClient& client, const std::string& base_url,
                          const std::string& method, const std::string& body) {
    auto args = std::make_shared<ix::HttpRequestArgs>();
    args->connectTimeout = 5;
    args->transferTimeout = 10;
    args->extraHeaders["Content-Type"] = "application/json";
    auto resp = client.post(base_url + "/" + method, body, args);
    Response r;
    if (!resp || resp->statusCode != 200) return r;
    try { r.j = json::parse(resp->body); } catch (...) { return r; }
    if (r.j.contains("ok") && r.j["ok"].is_boolean())
        r.ok = r.j["ok"].get<bool>();
    return r;
}

static void write_latest_crash(const fs::path& dir) {
    vivid::CrashRecord rec;
    rec.timestamp     = "2026-04-16T10:00:00Z";
    rec.signal        = 11;
    rec.signal_name   = "SIGSEGV";
    rec.pid           = 4242;
    rec.operator_name = "blur";
    rec.node_type     = "blur";
    rec.node_id       = "blur1";
    rec.pkg_name      = "fx-pack";
    std::ofstream ofs(dir / "latest-crash.json", std::ios::binary | std::ios::trunc);
    ofs << rec.to_json().dump();
}

static void write_history_blur_quarantine(const fs::path& dir) {
    // Three crashes within the last 24h so scan_quarantine marks 'blur' as
    // quarantined.  Timestamps are picked to be "recent enough" relative to
    // the test's wall-clock time — we can't control "now" from the HTTP side,
    // so we stamp them at approximately now and rely on the scan window.
    std::time_t now = std::time(nullptr);
    for (int i = 0; i < 3; ++i) {
        std::time_t t = now - (60 * i);
        std::tm tm_utc{};
#if defined(_WIN32)
        gmtime_s(&tm_utc, &t);
#else
        gmtime_r(&t, &tm_utc);
#endif
        char ts[32];
        std::snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                      tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                      tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
        json j = {
            {"node_type", "blur"},
            {"operator_name", "blur"},
            {"pkg_name", "fx-pack"},
            {"timestamp", ts},
        };
        char name[64];
        std::snprintf(name, sizeof(name), "hist_%d.json", i);
        std::ofstream ofs(dir / name, std::ios::binary | std::ios::trunc);
        ofs << j.dump();
    }
}

int main(int, char**) {
    std::fprintf(stderr, "=== test_control_server_crash ===\n");

    // --- Minimal runtime for server collaborators ---
    vivid::OperatorRegistry registry;
    vivid::Graph graph;
    vivid::RuntimeCore runtime;
    runtime.build(graph, registry);  // empty graph compiles trivially

    vivid::AudioEngine audio_engine;
    vivid::RuntimeAPI api(graph, runtime, audio_engine, registry);

    // --- Crash-recovery directory ---
    ScopedTempDir crash_dir("vivid_cs_crash");
    vivid::CrashRecoveryManager mgr(crash_dir.str());

    // --- Server ---
    int port = find_free_loopback_port();
    check(port > 0, "find_free_loopback_port()");

    vivid::ControlServer server;
    server.set_crash_recovery_manager(&mgr);
    check(server.start(port), "server.start()");
    const std::string base_url = "http://127.0.0.1:" + std::to_string(server.port());

    std::atomic<bool> done{false};
    bool has_gpu_ops = false;
    bool has_audio   = false;

    std::thread client_thread([&]() {
        ix::HttpClient client;

        // ---------- Test 1: no crash record → crash:null -------------------
        std::fprintf(stderr, "\n--- get_last_crash with no record ---\n");
        {
            auto r = post_json(client, base_url, "get_last_crash", "{}");
            check(r.ok, "get_last_crash ok");
            check(r.j.contains("result"), "result object present");
            check(r.j["result"]["crash"].is_null(), "crash field null");
            check(r.j["result"]["quarantined"].is_array(),
                  "quarantined field is array");
            check(r.j["result"]["quarantined"].empty(),
                  "quarantined array empty");
        }

        // ---------- Test 2: record present → fields echoed, then cleared --
        std::fprintf(stderr, "\n--- get_last_crash with record; then clear ---\n");
        write_latest_crash(fs::path(crash_dir.str()));
        {
            auto r = post_json(client, base_url, "get_last_crash", "{}");
            check(r.ok, "get_last_crash ok (record present)");
            check(r.j["result"]["crash"].is_object(), "crash is object");
            check(r.j["result"]["crash"]["operator_name"] == "blur",
                  "operator_name = blur");
            check(r.j["result"]["crash"]["signal_name"] == "SIGSEGV",
                  "signal_name = SIGSEGV");
            check(r.j["result"]["crash"]["node_id"] == "blur1",
                  "node_id = blur1");
        }
        {
            auto r = post_json(client, base_url, "clear_last_crash", "{}");
            check(r.ok, "clear_last_crash ok");
            check(!fs::exists(fs::path(crash_dir.str()) / "latest-crash.json"),
                  "latest-crash.json removed");
        }
        {
            auto r = post_json(client, base_url, "get_last_crash", "{}");
            check(r.ok, "get_last_crash ok after clear");
            check(r.j["result"]["crash"].is_null(),
                  "crash field null after clear");
        }

        // ---------- Test 3: clear_last_crash is idempotent ---------------
        std::fprintf(stderr, "\n--- clear_last_crash idempotent ---\n");
        {
            auto r = post_json(client, base_url, "clear_last_crash", "{}");
            check(r.ok, "clear with nothing to clear still ok");
        }

        // ---------- Test 4: quarantine entries surface on get_last_crash -
        std::fprintf(stderr, "\n--- get_last_crash surfaces quarantine ---\n");
        write_history_blur_quarantine(fs::path(crash_dir.str()));
        {
            auto r = post_json(client, base_url, "get_last_crash", "{}");
            check(r.ok, "get_last_crash ok with history");
            const auto& q = r.j["result"]["quarantined"];
            check(q.is_array() && q.size() == 1,
                  "quarantined array has 1 entry");
            if (q.is_array() && q.size() == 1) {
                check(q[0]["type"] == "blur",       "quarantined type = blur");
                check(q[0]["pkg"] == "fx-pack",     "quarantined pkg  = fx-pack");
                check(q[0]["count"].get<int>() >= 3, "count >= 3");
                check(!q[0]["last_seen"].get<std::string>().empty(),
                      "last_seen populated");
            }
        }

        // ---------- Test 5: load_graph_safe_mode missing path → error ----
        std::fprintf(stderr, "\n--- load_graph_safe_mode missing path ---\n");
        {
            auto r = post_json(client, base_url, "load_graph_safe_mode", "{}");
            check(!r.ok, "missing 'path' → ok:false");
            check(r.j.contains("error"), "error field present");
            check(r.j["error"].get<std::string>().find("path") != std::string::npos,
                  "error mentions 'path'");
        }

        // ---------- Test 6: load_graph_safe_mode invalid body → error ---
        std::fprintf(stderr, "\n--- load_graph_safe_mode invalid body ---\n");
        {
            auto r = post_json(client, base_url, "load_graph_safe_mode",
                               "not valid json");
            check(!r.ok, "invalid body → ok:false");
            check(r.j.contains("error"), "error field present");
        }

        done.store(true);
    });

    // --- Drive process_requests from the main thread until the client is done
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!done.load()) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::fprintf(stderr, "  FAIL: client thread did not finish within 30s\n");
            failures++;
            break;
        }
        server.process_requests(api, graph, runtime, registry,
                                has_gpu_ops, has_audio);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Drain any final response promises.
    server.process_requests(api, graph, runtime, registry,
                            has_gpu_ops, has_audio);

    client_thread.join();
    server.stop();
    runtime.shutdown();

    std::fprintf(stderr, "\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
