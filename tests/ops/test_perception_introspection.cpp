#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/runtime_api.h"
#include "runtime/control/control_server.h"
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXHttpClient.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#define VIVID_TEST_HELPERS_NO_CHECK
#include "test_helpers.h"

using json = nlohmann::json;

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
    json root;
    bool ok = false;
    bool valid = false;
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
    try {
        r.root = json::parse(resp->body);
        r.valid = true;
        if (r.root.contains("ok") && r.root["ok"].is_boolean()) {
            r.ok = r.root["ok"].get<bool>();
        }
    } catch (...) {}
    return r;
}

static json find_node(const json& nodes, const std::string& node_id) {
    if (!nodes.is_array()) return nullptr;
    for (auto& n : nodes) {
        if (n.contains("node_id") && n["node_id"].is_string() &&
            n["node_id"].get<std::string>() == node_id)
            return n;
    }
    return nullptr;
}

static json find_port(const json& ports, const std::string& port_name) {
    if (!ports.is_array()) return nullptr;
    for (auto& p : ports) {
        if (p.contains("name") && p["name"].is_string() &&
            p["name"].get<std::string>() == port_name)
            return p;
    }
    return nullptr;
}

static std::string get_str(const json& v) {
    if (v.is_null() || !v.is_string()) return "";
    return v.get<std::string>();
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];
    const int kPort = find_free_loopback_port();
    std::string base_url;

    std::string test_home = build_dir + "/.test_perception_home";
    std::filesystem::create_directories(test_home);
    setenv("HOME", test_home.c_str(), 1);

    std::string staging = build_dir + "/.test_perception_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
                               staging + "/test_op_v1.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/oscillator.dylib",
                               staging + "/oscillator.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/shape_2d.dylib",
                               staging + "/shape_2d.dylib",
                               std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Perception Introspection ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan(staging)");

    vivid::Graph graph;
    check(graph.add_node("ctrl1", "TestOp"), "add control node");
    check(graph.add_node("aud1", "Oscillator"), "add audio node");
    check(graph.add_node("gpu1", "Shape2D"), "add gpu node");
    check(graph.add_node("missing1", "DefinitelyMissingOp"), "add missing-operator node");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");
    runtime.tick(0.0, 0.016, 0, nullptr);

    vivid::AudioEngine audio_engine;
    bool has_gpu_ops = false;
    bool has_audio = false;
    vivid::RuntimeAPI api(graph, runtime, audio_engine, registry);

    vivid::ControlServer server;
    check(kPort > 0, "find_free_loopback_port()");
    check(server.start(kPort), "server.start()");
    base_url = "http://127.0.0.1:" + std::to_string(server.port());

    std::atomic<bool> done{false};
    std::thread pump([&]() {
        while (!done.load()) {
            server.process_requests(api, graph, runtime, registry, has_gpu_ops, has_audio);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    ix::HttpClient client;
    std::fprintf(stderr, "\n--- introspect_nodes (env + health coverage) ---\n");
    auto r1 = post(client, base_url, "introspect_nodes");
    check(r1.ok, "introspect_nodes ok");
    if (r1.valid) {
        auto& root = r1.root;
        check(root.contains("schema_version") && root["schema_version"].is_number_integer() &&
              root["schema_version"].get<int>() == 1, "schema_version=1");
        json nodes;
        if (root.contains("result") && root["result"].contains("nodes"))
            nodes = root["result"]["nodes"];
        check(nodes.is_array(), "nodes array present");
        check(nodes.is_array() && nodes.size() == 4, "nodes array size=4");

        json ctrl = find_node(nodes, "ctrl1");
        json aud = find_node(nodes, "aud1");
        json gpu = find_node(nodes, "gpu1");
        json miss = find_node(nodes, "missing1");
        check(!ctrl.is_null() && !aud.is_null() && !gpu.is_null() && !miss.is_null(),
              "all fixture nodes introspected");

        if (!ctrl.is_null()) {
            check(get_str(ctrl["kind"]) == "control", "control node kind");
            json dm = ctrl.value("env_metrics", json{});
            json c = dm.value("control", json{});
            check(c.is_object(), "control node has control env_metrics");
        }
        if (!aud.is_null()) {
            check(get_str(aud["kind"]) == "audio", "audio node kind");
            json dm = aud.value("env_metrics", json{});
            json a = dm.value("audio", json{});
            check(a.is_object(), "audio node has audio env_metrics");
            bool has_ipc = a.contains("input_port_count") && a["input_port_count"].is_number_integer();
            bool has_opc = a.contains("output_port_count") && a["output_port_count"].is_number_integer();
            check(has_ipc, "audio metrics has input_port_count");
            check(has_opc, "audio metrics has output_port_count");
            json outputs = aud.value("outputs", json{});
            json out = find_port(outputs, "output");
            check(!out.is_null(), "audio node output summary present");
            if (!out.is_null()) {
                check(out.contains("channel_count") && out["channel_count"].is_number_integer(),
                      "audio output has channel_count");
                check(out.contains("buffer_size") && out["buffer_size"].is_number_integer(),
                      "audio output has buffer_size");
                check(out.contains("last_block_peak") && out["last_block_peak"].is_number(),
                      "audio output has last_block_peak");
                check(out.contains("active") && out["active"].is_boolean(),
                      "audio output has active flag");
            }
        }
        if (!gpu.is_null()) {
            check(get_str(gpu["kind"]) == "gpu", "gpu node kind");
            json dm = gpu.value("env_metrics", json{});
            json g = dm.value("gpu", json{});
            check(g.is_object(), "gpu node has gpu env_metrics");
            check(g.contains("has_texture") && g["has_texture"].is_boolean(),
                  "gpu metrics has has_texture");
        }
        if (!miss.is_null()) {
            json h = miss.value("health", json{});
            check(h.contains("errored") && h["errored"].is_boolean() && h["errored"].get<bool>(),
                  "missing node health.errored=true");
            check(h.contains("missing_operator") && h["missing_operator"].is_boolean() &&
                  h["missing_operator"].get<bool>(),
                  "missing node health.missing_operator=true");
            check(h.contains("message") && h["message"].is_string(),
                  "missing node health.message is string");
        }
    }

    // Deterministic health regression: missing node health fields should be stable across calls.
    auto r2 = post(client, base_url, "introspect_nodes");
    check(r2.ok, "second introspect_nodes ok");
    if (r1.valid && r2.valid) {
        json n1 = r1.root.value("result", json{}).value("nodes", json{});
        json n2 = r2.root.value("result", json{}).value("nodes", json{});
        json m1 = find_node(n1, "missing1");
        json m2 = find_node(n2, "missing1");
        if (!m1.is_null() && !m2.is_null()) {
            json h1 = m1.value("health", json{});
            json h2 = m2.value("health", json{});
            check(get_str(h1.value("message", json{})) == get_str(h2.value("message", json{})),
                  "missing node health.message deterministic");
            check(h1.value("errored", false) == h2.value("errored", false),
                  "missing node health.errored deterministic");
            check(h1.value("missing_operator", false) == h2.value("missing_operator", false),
                  "missing node health.missing_operator deterministic");
        }
    }

    done.store(true);
    pump.join();
    server.stop();
    runtime.shutdown();
    std::filesystem::remove_all(staging);
    std::filesystem::remove_all(test_home);

    int f = failures.load();
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 f == 0 ? "ALL PASSED" : "SOME FAILED", f);
    return f == 0 ? 0 : 1;
}
