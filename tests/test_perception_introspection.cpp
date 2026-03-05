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
#include <cstdlib>
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

struct Response {
    yyjson_doc* doc = nullptr;
    yyjson_val* root = nullptr;
    bool ok = false;

    ~Response() { if (doc) yyjson_doc_free(doc); }
    Response() = default;
    Response(Response&& o) : doc(o.doc), root(o.root), ok(o.ok) {
        o.doc = nullptr;
        o.root = nullptr;
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
        yyjson_val* okv = yyjson_obj_get(r.root, "ok");
        r.ok = okv && yyjson_is_bool(okv) && yyjson_get_bool(okv);
    }
    return r;
}

static yyjson_val* find_node(yyjson_val* nodes, const char* node_id) {
    if (!nodes || !yyjson_is_arr(nodes)) return nullptr;
    size_t i, max;
    yyjson_val* n = nullptr;
    yyjson_arr_foreach(nodes, i, max, n) {
        yyjson_val* idv = yyjson_obj_get(n, "node_id");
        if (idv && yyjson_is_str(idv) && std::string(yyjson_get_str(idv)) == node_id)
            return n;
    }
    return nullptr;
}

static std::string get_str(yyjson_val* v) {
    if (!v || !yyjson_is_str(v)) return "";
    const char* s = yyjson_get_str(v);
    return s ? s : "";
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];
    constexpr int kPort = 19877;
    const std::string base_url = "http://127.0.0.1:19877";

    std::string test_home = build_dir + "/.test_perception_home";
    std::filesystem::create_directories(test_home);
    setenv("HOME", test_home.c_str(), 1);

    std::string staging = build_dir + "/.test_perception_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
                               staging + "/test_op_v1.dylib",
                               std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Perception Introspection ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan(staging)");
    check(registry.scan(build_dir.c_str()), "registry.scan(build_dir)");

    vivid::Graph graph;
    check(graph.add_node("ctrl1", "TestOp"), "add control node");
    check(graph.add_node("aud1", "Oscillator"), "add audio node");
    check(graph.add_node("gpu1", "Shape"), "add gpu node");
    check(graph.add_node("missing1", "DefinitelyMissingOp"), "add missing-operator node");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");
    scheduler.tick(0.0, 0.016, 0, nullptr);

    vivid::AudioEngine audio_engine;
    bool has_gpu_ops = false;
    bool has_audio = false;
    vivid::RuntimeAPI api(graph, scheduler, audio_engine, registry);

    vivid::ControlServer server;
    check(server.start(kPort), "server.start()");

    std::atomic<bool> done{false};
    std::thread pump([&]() {
        while (!done.load()) {
            server.process_requests(api, graph, scheduler, registry, has_gpu_ops, has_audio);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    ix::HttpClient client;
    std::fprintf(stderr, "\n--- introspect_nodes (domain + health coverage) ---\n");
    auto r1 = post(client, base_url, "introspect_nodes");
    check(r1.ok, "introspect_nodes ok");
    if (r1.root) {
        yyjson_val* sv = yyjson_obj_get(r1.root, "schema_version");
        check(sv && yyjson_is_int(sv) && yyjson_get_int(sv) == 1, "schema_version=1");
        yyjson_val* result = yyjson_obj_get(r1.root, "result");
        yyjson_val* nodes = result ? yyjson_obj_get(result, "nodes") : nullptr;
        check(nodes && yyjson_is_arr(nodes), "nodes array present");
        check(nodes && yyjson_arr_size(nodes) == 4, "nodes array size=4");

        yyjson_val* ctrl = find_node(nodes, "ctrl1");
        yyjson_val* aud = find_node(nodes, "aud1");
        yyjson_val* gpu = find_node(nodes, "gpu1");
        yyjson_val* miss = find_node(nodes, "missing1");
        check(ctrl && aud && gpu && miss, "all fixture nodes introspected");

        if (ctrl) {
            check(get_str(yyjson_obj_get(ctrl, "domain")) == "control", "control node domain");
            yyjson_val* dm = yyjson_obj_get(ctrl, "domain_metrics");
            yyjson_val* c = dm ? yyjson_obj_get(dm, "control") : nullptr;
            check(c && yyjson_is_obj(c), "control node has control domain_metrics");
        }
        if (aud) {
            check(get_str(yyjson_obj_get(aud, "domain")) == "audio", "audio node domain");
            yyjson_val* dm = yyjson_obj_get(aud, "domain_metrics");
            yyjson_val* a = dm ? yyjson_obj_get(dm, "audio") : nullptr;
            check(a && yyjson_is_obj(a), "audio node has audio domain_metrics");
            yyjson_val* ipc = a ? yyjson_obj_get(a, "input_port_count") : nullptr;
            yyjson_val* opc = a ? yyjson_obj_get(a, "output_port_count") : nullptr;
            check(ipc && yyjson_is_int(ipc), "audio metrics has input_port_count");
            check(opc && yyjson_is_int(opc), "audio metrics has output_port_count");
        }
        if (gpu) {
            check(get_str(yyjson_obj_get(gpu, "domain")) == "gpu", "gpu node domain");
            yyjson_val* dm = yyjson_obj_get(gpu, "domain_metrics");
            yyjson_val* g = dm ? yyjson_obj_get(dm, "gpu") : nullptr;
            check(g && yyjson_is_obj(g), "gpu node has gpu domain_metrics");
            yyjson_val* ht = g ? yyjson_obj_get(g, "has_texture") : nullptr;
            check(ht && yyjson_is_bool(ht), "gpu metrics has has_texture");
        }
        if (miss) {
            yyjson_val* h = yyjson_obj_get(miss, "health");
            yyjson_val* err = h ? yyjson_obj_get(h, "errored") : nullptr;
            yyjson_val* mo = h ? yyjson_obj_get(h, "missing_operator") : nullptr;
            yyjson_val* msg = h ? yyjson_obj_get(h, "message") : nullptr;
            check(err && yyjson_is_bool(err) && yyjson_get_bool(err),
                  "missing node health.errored=true");
            check(mo && yyjson_is_bool(mo) && yyjson_get_bool(mo),
                  "missing node health.missing_operator=true");
            check(msg && yyjson_is_str(msg), "missing node health.message is string");
        }
    }

    // Deterministic health regression: missing node health fields should be stable across calls.
    auto r2 = post(client, base_url, "introspect_nodes");
    check(r2.ok, "second introspect_nodes ok");
    if (r1.root && r2.root) {
        yyjson_val* n1 = yyjson_obj_get(yyjson_obj_get(r1.root, "result"), "nodes");
        yyjson_val* n2 = yyjson_obj_get(yyjson_obj_get(r2.root, "result"), "nodes");
        yyjson_val* m1 = find_node(n1, "missing1");
        yyjson_val* m2 = find_node(n2, "missing1");
        if (m1 && m2) {
            yyjson_val* h1 = yyjson_obj_get(m1, "health");
            yyjson_val* h2 = yyjson_obj_get(m2, "health");
            check(get_str(yyjson_obj_get(h1, "message")) == get_str(yyjson_obj_get(h2, "message")),
                  "missing node health.message deterministic");
            check(yyjson_get_bool(yyjson_obj_get(h1, "errored")) ==
                  yyjson_get_bool(yyjson_obj_get(h2, "errored")),
                  "missing node health.errored deterministic");
            check(yyjson_get_bool(yyjson_obj_get(h1, "missing_operator")) ==
                  yyjson_get_bool(yyjson_obj_get(h2, "missing_operator")),
                  "missing node health.missing_operator deterministic");
        }
    }

    done.store(true);
    pump.join();
    server.stop();
    scheduler.shutdown();
    std::filesystem::remove_all(staging);
    std::filesystem::remove_all(test_home);

    int f = failures.load();
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 f == 0 ? "ALL PASSED" : "SOME FAILED", f);
    return f == 0 ? 0 : 1;
}
