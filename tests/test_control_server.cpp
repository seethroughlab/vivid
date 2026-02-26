#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/runtime_api.h"
#include "runtime/control_server.h"
#include "yyjson.h"
#include <ixwebsocket/IXHttpClient.h>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>

static std::atomic<int> failures{0};

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

// POST helper — returns parsed yyjson_doc* (caller must free), or nullptr.
// Also validates top-level "ok" field matches expected_ok.
struct Response {
    yyjson_doc* doc = nullptr;
    yyjson_val* root = nullptr;
    bool ok = false;

    ~Response() { if (doc) yyjson_doc_free(doc); }
    Response() = default;
    Response(Response&& o) : doc(o.doc), root(o.root), ok(o.ok) {
        o.doc = nullptr; o.root = nullptr;
    }
    Response& operator=(Response&&) = delete;
    Response(const Response&) = delete;
};

static Response post(ix::HttpClient& client, const std::string& base_url,
                     const std::string& method, const std::string& body = "{}") {
    auto args = std::make_shared<ix::HttpRequestArgs>();
    args->connectTimeout = 5;
    args->transferTimeout = 10;
    args->extraHeaders["Content-Type"] = "application/json";

    auto resp = client.post(base_url + "/" + method, body, args);
    Response r;
    if (!resp || resp->statusCode != 200) return r;

    r.doc = yyjson_read(resp->body.c_str(), resp->body.size(), 0);
    r.root = r.doc ? yyjson_doc_get_root(r.doc) : nullptr;
    if (r.root) {
        yyjson_val* ok_val = yyjson_obj_get(r.root, "ok");
        r.ok = ok_val && yyjson_is_bool(ok_val) && yyjson_get_bool(ok_val);
    }
    return r;
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string graph_path = build_dir + "/test_runtime_api.json";
    constexpr int kPort = 19876;
    const std::string base_url = "http://127.0.0.1:19876";

    // --- Setup: staging dir with test_op_v1 ---
    std::string staging = build_dir + "/.test_cs_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
        staging + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: ControlServer ===\n\n");

    // --- Runtime setup ---
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");

    vivid::AudioEngine audio_engine;
    bool has_gpu_ops = false;
    bool has_audio = false;

    vivid::RuntimeAPI api(graph, scheduler, audio_engine, registry);

    // Tick once so nodes have output values
    scheduler.tick(0.0, 0.016, 0);

    // --- Start ControlServer ---
    vivid::ControlServer server;
    check(server.start(kPort), "server.start()");

    // Coordination between main and client threads
    std::atomic<bool> done{false};
    std::atomic<int> phase{0};

    // --- Client thread ---
    std::thread client_thread([&]() {
        ix::HttpClient client;

        // Phase 1: inspect_graph — initial state
        std::fprintf(stderr, "\n--- inspect_graph (initial) ---\n");
        {
            auto r = post(client, base_url, "inspect_graph");
            check(r.ok, "inspect_graph ok");
            if (r.root) {
                yyjson_val* result = yyjson_obj_get(r.root, "result");
                yyjson_val* nodes = result ? yyjson_obj_get(result, "nodes") : nullptr;
                yyjson_val* conns = result ? yyjson_obj_get(result, "connections") : nullptr;
                check(nodes && yyjson_arr_size(nodes) == 2, "2 nodes");
                check(conns && yyjson_arr_size(conns) == 1, "1 connection");

                // Check that params have values
                yyjson_val* first_node = nodes ? yyjson_arr_get_first(nodes) : nullptr;
                yyjson_val* params = first_node ? yyjson_obj_get(first_node, "params") : nullptr;
                check(params && yyjson_arr_size(params) > 0, "params have values");
            }
        }

        // Phase 2: list_types
        std::fprintf(stderr, "\n--- list_types ---\n");
        {
            auto r = post(client, base_url, "list_types");
            check(r.ok, "list_types ok");
            if (r.root) {
                yyjson_val* result = yyjson_obj_get(r.root, "result");
                yyjson_val* types = result ? yyjson_obj_get(result, "types") : nullptr;
                check(types && yyjson_arr_size(types) > 0, "has types");
                if (types) {
                    yyjson_val* t0 = yyjson_arr_get_first(types);
                    yyjson_val* name = t0 ? yyjson_obj_get(t0, "name") : nullptr;
                    check(name && std::string(yyjson_get_str(name)) == "TestOp",
                          "contains TestOp");
                    yyjson_val* params = t0 ? yyjson_obj_get(t0, "params") : nullptr;
                    check(params && yyjson_arr_size(params) > 0, "TestOp has params");
                    yyjson_val* outputs = t0 ? yyjson_obj_get(t0, "outputs") : nullptr;
                    check(outputs && yyjson_arr_size(outputs) > 0, "TestOp has ports");
                }
            }
        }

        // Phase 3: set_param — set a/scale=9
        std::fprintf(stderr, "\n--- set_param ---\n");
        {
            auto r = post(client, base_url, "set_param",
                R"({"node_id":"a","param":"scale","value":9.0})");
            check(r.ok, "set_param a/scale=9 ok");
        }

        // Phase 4: get_param — verify a/scale=9
        std::fprintf(stderr, "\n--- get_param ---\n");
        {
            auto r = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(r.ok, "get_param a/scale ok");
            if (r.root) {
                yyjson_val* val = yyjson_obj_get(r.root, "value");
                check(val && std::fabs(yyjson_get_num(val) - 9.0) < 1e-4,
                      "value is 9.0");
            }
        }

        // Phase 5: add_node — add TestOp as "c"
        std::fprintf(stderr, "\n--- add_node ---\n");
        {
            auto r = post(client, base_url, "add_node",
                R"({"type":"TestOp","id":"c"})");
            check(r.ok, "add_node TestOp c ok");
        }

        // Phase 6: connect — a/out -> c/scale
        std::fprintf(stderr, "\n--- connect ---\n");
        {
            auto r = post(client, base_url, "connect",
                R"({"from_addr":"a/out","to_addr":"c/scale"})");
            check(r.ok, "connect a/out -> c/scale ok");
        }

        // Signal main thread to apply_pending
        phase.store(1);
        // Wait for main thread to apply
        while (phase.load() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // Phase 7: inspect_graph — should now have 3 nodes, 2 connections
        std::fprintf(stderr, "\n--- inspect_graph (after add+connect) ---\n");
        {
            auto r = post(client, base_url, "inspect_graph");
            check(r.ok, "inspect_graph ok");
            if (r.root) {
                yyjson_val* result = yyjson_obj_get(r.root, "result");
                yyjson_val* nodes = result ? yyjson_obj_get(result, "nodes") : nullptr;
                yyjson_val* conns = result ? yyjson_obj_get(result, "connections") : nullptr;
                check(nodes && yyjson_arr_size(nodes) == 3, "3 nodes");
                check(conns && yyjson_arr_size(conns) == 2, "2 connections");
            }
        }

        // Phase 8: disconnect — a/out -> c/scale
        std::fprintf(stderr, "\n--- disconnect ---\n");
        {
            auto r = post(client, base_url, "disconnect",
                R"({"from_addr":"a/out","to_addr":"c/scale"})");
            check(r.ok, "disconnect a/out -> c/scale ok");
        }

        // Phase 9: remove_node — remove "c"
        std::fprintf(stderr, "\n--- remove_node ---\n");
        {
            auto r = post(client, base_url, "remove_node",
                R"({"node_id":"c"})");
            check(r.ok, "remove_node c ok");
        }

        // Signal main thread to apply_pending again
        phase.store(3);
        while (phase.load() < 4) std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // Phase 10: inspect_graph — back to 2 nodes
        std::fprintf(stderr, "\n--- inspect_graph (after remove) ---\n");
        {
            auto r = post(client, base_url, "inspect_graph");
            check(r.ok, "inspect_graph ok");
            if (r.root) {
                yyjson_val* result = yyjson_obj_get(r.root, "result");
                yyjson_val* nodes = result ? yyjson_obj_get(result, "nodes") : nullptr;
                check(nodes && yyjson_arr_size(nodes) == 2, "back to 2 nodes");
            }
        }

        // Phase 11: save_graph
        std::fprintf(stderr, "\n--- save_graph ---\n");
        std::string tmp_path = build_dir + "/test_cs_saved.json";
        {
            auto r = post(client, base_url, "save_graph",
                R"({"path":")" + tmp_path + R"("})");
            check(r.ok, "save_graph ok");
            check(std::filesystem::exists(tmp_path), "saved file exists");
        }

        // Phase 12: load_graph
        std::fprintf(stderr, "\n--- load_graph ---\n");
        {
            auto r = post(client, base_url, "load_graph");
            check(r.ok, "load_graph ok");
        }

        // Phase 13: error cases
        std::fprintf(stderr, "\n--- error cases ---\n");
        {
            auto r1 = post(client, base_url, "bogus_method");
            check(!r1.ok, "unknown method -> ok=false");

            auto r2 = post(client, base_url, "get_param",
                R"({"node_id":"nonexistent","param":"scale"})");
            check(!r2.ok, "unknown node -> ok=false");

            auto r3 = post(client, base_url, "set_param", "not json at all{{{");
            check(!r3.ok, "invalid JSON -> ok=false");
        }

        // Cleanup temp file
        std::filesystem::remove(tmp_path);

        done.store(true);
    });

    // --- Main thread: pump loop ---
    while (!done.load()) {
        server.process_requests(api, graph, scheduler, registry,
                                has_gpu_ops, has_audio);

        // Apply pending topology when client signals
        int p = phase.load();
        if (p == 1) {
            api.apply_pending(has_gpu_ops, has_audio);
            scheduler.tick(0.0, 0.016, 1);
            phase.store(2);
        } else if (p == 3) {
            api.apply_pending(has_gpu_ops, has_audio);
            scheduler.tick(0.0, 0.016, 2);
            phase.store(4);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Drain any final requests
    server.process_requests(api, graph, scheduler, registry,
                            has_gpu_ops, has_audio);

    client_thread.join();
    server.stop();
    scheduler.shutdown();
    std::filesystem::remove_all(staging);

    int f = failures.load();
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        f == 0 ? "ALL PASSED" : "SOME FAILED", f);
    return f == 0 ? 0 : 1;
}
