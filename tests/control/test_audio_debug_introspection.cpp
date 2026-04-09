#include "runtime/operators/operator_registry.h"
#include "runtime/operators/builtin_operators.h"
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
#include <vector>
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
        if (r.root.contains("ok") && r.root["ok"].is_boolean())
            r.ok = r.root["ok"].get<bool>();
    } catch (...) {}
    return r;
}

static json find_node(const json& nodes, const std::string& node_id) {
    if (!nodes.is_array()) return nullptr;
    for (const auto& n : nodes) {
        if (n.contains("node_id") && n["node_id"].is_string() &&
            n["node_id"].get<std::string>() == node_id)
            return n;
    }
    return nullptr;
}

static json find_port(const json& ports, const std::string& name) {
    if (!ports.is_array()) return nullptr;
    for (const auto& p : ports) {
        if (p.contains("name") && p["name"].is_string() &&
            p["name"].get<std::string>() == name)
            return p;
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];
    const int kPort = find_free_loopback_port();
    std::string base_url;

    std::string test_home = build_dir + "/.test_audio_debug_home";
    std::filesystem::create_directories(test_home);
    setenv("HOME", test_home.c_str(), 1);

    const std::string staging = build_dir + "/.test_audio_debug_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/multi_channel_dc_source_op.dylib",
                               staging + "/multi_channel_dc_source_op.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/audio_reduce_op.dylib",
                               staging + "/audio_reduce_op.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/gain.dylib",
                               staging + "/gain.dylib",
                               std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Audio Debug Introspection ===\n\n");

    vivid::OperatorRegistry registry;
    register_builtin_operators(registry);
    check(registry.scan(staging.c_str()), "registry.scan(staging)");

    vivid::Graph graph;
    check(graph.add_node("src", "MultiChannelDcSourceOp"), "add src");
    check(graph.add_node("reduce", "AudioReduceOp"), "add reduce");
    check(graph.add_node("gain1", "Gain"), "add gain");
    check(graph.add_node("out", "audio_out"), "add out");
    check(graph.add_connection("src", "output", "reduce", "input"), "connect src -> reduce");
    check(graph.add_connection("reduce", "output", "gain1", "input"), "connect reduce -> gain");
    check(graph.add_connection("gain1", "output", "out", "input"), "connect gain -> out");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");
    runtime.tick(0.0, 0.016, 0, nullptr);
    runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());

    vivid::AudioEngine audio_engine;
    check(audio_engine.build(runtime), "audio_engine.build()");
    std::vector<float> output(audio_engine.buffer_size() * 2, 0.0f);
    audio_engine.process_audio_for_test(output.data(), audio_engine.buffer_size());

    vivid::RuntimeAPI api(graph, runtime, audio_engine, registry);
    auto inspect_reduce = api.inspect("reduce");
    check(inspect_reduce.ok, "inspect reduce ok");
    check(inspect_reduce.message.find("input=audio[ch=4") != std::string::npos,
          "inspect reduce shows widened audio input");
    check(inspect_reduce.message.find("output=audio[ch=1") != std::string::npos,
          "inspect reduce shows mono audio output");
    check(inspect_reduce.message.find("active]") != std::string::npos,
          "inspect reduce shows active audio summary");
    check(inspect_reduce.message.find("audio_debug: total=") != std::string::npos,
          "inspect reduce shows node audio_debug summary");

    auto inspect_gain = api.inspect("gain1");
    check(inspect_gain.ok, "inspect gain ok");
    check(inspect_gain.message.find("input=audio[ch=1") != std::string::npos,
          "inspect gain shows audio input summary");
    check(inspect_gain.message.find("output=audio[ch=1") != std::string::npos,
          "inspect gain shows audio output summary");
    check(inspect_gain.message.find("audio_debug: total=") != std::string::npos,
          "inspect gain shows node audio_debug summary");

    vivid::ControlServer server;
    bool has_gpu_ops = false;
    bool has_audio = true;
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

    std::fprintf(stderr, "\n--- introspect_nodes ---\n");
    auto intro = post(client, base_url, "introspect_nodes");
    check(intro.ok, "introspect_nodes ok");
    if (intro.valid) {
        json nodes = intro.root.value("result", json{}).value("nodes", json{});
        auto reduce = find_node(nodes, "reduce");
        auto gain = find_node(nodes, "gain1");
        check(!reduce.is_null(), "reduce node introspected");
        check(!gain.is_null(), "gain node introspected");

        if (!reduce.is_null()) {
            auto reduce_debug = reduce.value("audio_debug", json{});
            auto reduce_in = find_port(reduce.value("inputs", json{}), "input");
            auto reduce_out = find_port(reduce.value("outputs", json{}), "output");
            check(reduce_debug.is_object(), "reduce audio_debug present");
            check(!reduce_in.is_null(), "reduce input port summary present");
            check(!reduce_out.is_null(), "reduce output port summary present");
            if (reduce_debug.is_object()) {
                check(reduce_debug.contains("last_block_total_us") &&
                          reduce_debug["last_block_total_us"].is_number_integer(),
                      "reduce audio_debug has last_block_total_us");
                check(reduce_debug.contains("last_process_us") &&
                          reduce_debug["last_process_us"].is_number_integer(),
                      "reduce audio_debug has last_process_us");
                check(reduce_debug.value("last_block_total_us", 0) >=
                          reduce_debug.value("last_process_us", 0),
                      "reduce total block time covers process time");
                check(reduce_debug.contains("last_lane_count") &&
                          reduce_debug["last_lane_count"].is_number_integer(),
                      "reduce audio_debug has last_lane_count");
                check(reduce_debug.contains("lane_state_entries") &&
                          reduce_debug["lane_state_entries"].is_number_integer(),
                      "reduce audio_debug has lane_state_entries");
            }
            if (!reduce_in.is_null()) {
                check(reduce_in.value("channel_count", 0) == 4,
                      "reduce input channel_count preserved");
                check(reduce_in.contains("last_block_peak") &&
                          reduce_in["last_block_peak"].is_number() &&
                          reduce_in["last_block_peak"].get<double>() > 0.39,
                      "reduce input last_block_peak is nonzero");
                check(reduce_in.value("active", false),
                      "reduce input marked active");
            }
            if (!reduce_out.is_null()) {
                check(reduce_out.value("channel_count", 0) == 1,
                      "reduce output channel_count is mono");
                check(reduce_out.contains("last_block_peak") &&
                          reduce_out["last_block_peak"].is_number() &&
                          reduce_out["last_block_peak"].get<double>() > 0.99,
                      "reduce output last_block_peak is nonzero");
            }
        }

        if (!gain.is_null()) {
            auto gain_debug = gain.value("audio_debug", json{});
            auto gain_in = find_port(gain.value("inputs", json{}), "input");
            auto gain_out = find_port(gain.value("outputs", json{}), "output");
            check(gain_debug.is_object(), "gain audio_debug present");
            check(!gain_in.is_null(), "gain input port summary present");
            check(!gain_out.is_null(), "gain output port summary present");
            if (gain_debug.is_object()) {
                check(gain_debug.contains("last_block_budget_pct") &&
                          gain_debug["last_block_budget_pct"].is_number(),
                      "gain audio_debug has last_block_budget_pct");
            }
            if (!gain_out.is_null()) {
                check(gain_out.contains("last_block_peak") &&
                          gain_out["last_block_peak"].is_number() &&
                          gain_out["last_block_peak"].get<double>() > 0.99,
                      "gain output last_block_peak is nonzero");
                check(gain_out.value("active", false),
                      "gain output marked active");
            }
        }
    }

    std::fprintf(stderr, "\n--- sample_node_outputs ---\n");
    auto samples = post(client, base_url, "sample_node_outputs",
                        R"({"node_id":"gain1","duration_seconds":0.02,"interval_ms":10})");
    check(samples.ok, "sample_node_outputs ok");
    if (samples.valid) {
        auto result = samples.root.value("result", json{});
        auto sample_arr = result.value("samples", json{});
        check(sample_arr.is_array() && !sample_arr.empty(),
              "sample_node_outputs returns samples");
        if (sample_arr.is_array() && !sample_arr.empty()) {
            auto sample_debug = sample_arr[0].value("audio_debug", json{});
            auto outputs_obj = sample_arr[0].value("outputs", json{});
            auto out = outputs_obj.value("output", json{});
            check(sample_debug.is_object(), "sample carries node audio_debug");
            check(out.is_object(), "sampled gain output present");
            if (sample_debug.is_object()) {
                check(sample_debug.contains("ema_block_us") &&
                          sample_debug["ema_block_us"].is_number_integer(),
                      "sample audio_debug has ema_block_us");
                check(sample_debug.contains("last_lane_count") &&
                          sample_debug["last_lane_count"].is_number_integer(),
                      "sample audio_debug has last_lane_count");
            }
            if (out.is_object()) {
                check(out.value("channel_count", 0) == 1,
                      "sampled output channel_count is mono");
                check(out.contains("last_block_peak") &&
                          out["last_block_peak"].is_number() &&
                          out["last_block_peak"].get<double>() > 0.99,
                      "sampled output last_block_peak is nonzero");
                check(out.value("active", false),
                      "sampled output marked active");
            }
        }
    }

    done.store(true);
    pump.join();
    server.stop();
    audio_engine.shutdown();
    runtime.shutdown();
    std::filesystem::remove_all(staging);
    std::filesystem::remove_all(test_home);

    int f = failures.load();
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 f == 0 ? "ALL PASSED" : "SOME FAILED", f);
    return f == 0 ? 0 : 1;
}
