#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/runtime_api.h"
#include "runtime/control/control_server.h"
#include "runtime/graph/subgraph_module.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/packages/package_manager.h"
#include "runtime/packages/package_catalog.h"
#include "runtime/core/settings.h"
#include "runtime/platform/platform.h"
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXHttpClient.h>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    json j;
    bool ok = false;

    Response() = default;
    Response(Response&& o) = default;
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

    try {
        r.j = json::parse(resp->body);
    } catch (...) {
        return r;
    }
    if (r.j.contains("ok") && r.j["ok"].is_boolean()) {
        r.ok = r.j["ok"].get<bool>();
    }
    return r;
}

static json introspect_node_by_id(const json& root, const char* node_id) {
    if (!root.contains("result")) return nullptr;
    const auto& result = root["result"];
    if (!result.contains("nodes") || !result["nodes"].is_array()) return nullptr;
    for (const auto& node : result["nodes"]) {
        if (node.contains("node_id") && node["node_id"].is_string() &&
            node["node_id"].get<std::string>() == node_id)
            return node;
    }
    return nullptr;
}

static json inspect_graph_node_by_id(const json& root, const char* node_id) {
    if (!root.contains("result")) return nullptr;
    const auto& result = root["result"];
    if (!result.contains("nodes") || !result["nodes"].is_array()) return nullptr;
    for (const auto& node : result["nodes"]) {
        if (node.contains("id") && node["id"].is_string() &&
            node["id"].get<std::string>() == node_id)
            return node;
    }
    return nullptr;
}

static json find_named_entry(const json& arr, const char* name) {
    if (!arr.is_array()) return nullptr;
    for (const auto& entry : arr) {
        if (entry.contains("name") && entry["name"].is_string() &&
            entry["name"].get<std::string>() == name)
            return entry;
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];
    build_dir = std::filesystem::absolute(build_dir).string();

    const auto original_cwd = std::filesystem::current_path();
    const auto isolated_cwd = std::filesystem::path(build_dir) / ".test_cs_cwd";
    std::filesystem::remove_all(isolated_cwd);
    std::filesystem::create_directories(isolated_cwd);
    std::filesystem::current_path(isolated_cwd);

    std::string graph_path = build_dir + "/test_runtime_api.json";
    const int kPort = find_free_loopback_port();
    std::string base_url;

    std::string test_home = build_dir + "/.test_cs_home";
    std::filesystem::remove_all(test_home);
    std::filesystem::create_directories(test_home);
    setenv("HOME", test_home.c_str(), 1);
    setenv("VIVID_SKIP_PACKAGE_CATALOG_NETWORK", "1", 1);
    setenv("VISUAL", "true", 1);

    std::string staging = build_dir + "/.test_cs_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
        staging + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/semantic_ms_source_op.dylib",
        staging + "/semantic_ms_source_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/semantic_s_dest_op.dylib",
        staging + "/semantic_s_dest_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/semantic_unknown_source_op.dylib",
        staging + "/semantic_unknown_source_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/untagged_dest_op.dylib",
        staging + "/untagged_dest_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/export_custom_port_op.dylib",
        staging + "/export_custom_port_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/test_op_bad_custom_type.dylib",
        staging + "/test_op_bad_custom_type.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/envelope_fr.dylib",
        staging + "/envelope_fr.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/string_source_op.dylib",
        staging + "/string_source_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/string_sink_op.dylib",
        staging + "/string_sink_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/control_pass_op.dylib",
        staging + "/control_pass_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::string abi_probe_dir = build_dir + "/.test_cs_abi_probe";
    std::filesystem::create_directories(abi_probe_dir);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
        abi_probe_dir + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: ControlServer ===\n\n");

    vivid::OperatorRegistry registry;
    setenv("VIVID_MOCK_RUNTIME_ABI", "999", 1);
    check(registry.scan_deferred(abi_probe_dir.c_str()), "registry.scan_deferred() for ABI diagnostics");
    unsetenv("VIVID_MOCK_RUNTIME_ABI");
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::SubgraphModuleRegistry subgraph_modules;
    std::string module_path = build_dir + "/.test_cs_query_surface.vivid-module.json";
    {
        std::ofstream ofs(module_path);
        ofs <<
            R"({
  "schema_version": 2,
  "module": {
    "name": "QuerySurfaceModule",
    "description": "Fixture module for control-server query parity tests.",
    "ports": [
      { "name": "mod_in", "type": "signal", "direction": "input", "bind": "mod_src/in" },
      { "name": "echo_in", "type": "string", "direction": "input", "bind": "sink/in" },
      { "name": "echo_list_in", "type": "string_lanes", "direction": "input", "bind": "sink/in_list" },
      { "name": "scaled", "type": "signal", "direction": "output", "bind": "math/out" },
      { "name": "label_out", "type": "string", "direction": "output", "bind": "text_src/out" },
      { "name": "label_list", "type": "string_lanes", "direction": "output", "bind": "text_src/list" },
      { "name": "echo_out", "type": "string", "direction": "output", "bind": "sink/out" },
      { "name": "echo_list_out", "type": "string_lanes", "direction": "output", "bind": "sink/out_list" }
    ],
    "mod_sources": [
      { "name": "motion", "bind": "mod_src/out", "description": "Numeric motion source" }
    ],
    "mod_destinations": [
      { "name": "scale", "bind": "math/scale", "description": "Scale modulation target", "group": "Main" }
    ],
    "params": [
      {
        "name": "scale",
        "bind": "math/scale",
        "type": "float",
        "min": 0.0,
        "max": 100.0,
        "default": 4.0,
        "semantic_tag": "frequency_hz",
        "semantic_shape": "scalar",
        "semantic_unit": "Hz",
        "semantic_intent": "module_scale"
      },
      {
        "name": "label",
        "bind": "text_src/value",
        "type": "text",
        "description": "Text label echoed by the fixture module."
      }
    ]
  },
  "nodes": {
    "math": { "type": "TestOp", "params": { "scale": 4.0 } },
    "mod_src": { "type": "ControlPassOp", "params": { "gain": 1.0 } },
    "text_src": { "type": "StringSourceOp", "params": { "value": "module-label" } },
    "sink": { "type": "StringSinkOp" }
  },
  "connections": []
}
)";
    }
    check(subgraph_modules.load(module_path), "subgraph module fixture loads");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");
    {
        auto& meta = graph.meta_mut();
        meta = {};
        meta.id = "control-server-fixture";
        meta.title = "Control Server Fixture";
        meta.description = "Fixture graph with instrument metadata.";
        meta.tags = {"audio", "fixture"};
        meta.difficulty = "intermediate";
        meta.domains = {"audio", "control"};
        meta.requires_packages = {"vivid-wavetable"};
        meta.featured_rank = 7;
        meta.estimated_minutes = 3;
        meta.content_kind = "instrument";
        meta.category = "synth";
        meta.family = "pads";
        meta.role = "reference";
        meta.playability = "midi";
        meta.preview_controls = {
            {"a", "scale", "Drive"},
            {"b", "scale", ""}
        };
    }

    vivid::RuntimeCore runtime;
    runtime.set_subgraph_modules(&subgraph_modules);
    check(runtime.build(graph, registry), "runtime.build()");

    vivid::AudioEngine audio_engine;
    bool has_gpu_ops = false;
    bool has_audio = false;

    vivid::RuntimeAPI api(graph, runtime, audio_engine, registry);
    api.set_subgraph_modules(&subgraph_modules);

    {
        std::filesystem::create_directories(vivid::PackageManager::packages_dir());
        std::string pkg_dir = vivid::PackageManager::packages_dir() + "/catalog-test-pkg";
        std::filesystem::create_directories(pkg_dir);
        std::ofstream ofs(pkg_dir + "/vivid-package.json");
        ofs <<
            "{\n"
            "  \"name\": \"catalog-test-pkg\",\n"
            "  \"version\": \"1.0.0\",\n"
            "  \"description\": \"installed test package\",\n"
            "  \"operators\": []\n"
            "}\n";
    }
    {
        std::string cache_path = vivid::get_config_dir() + "/package-catalog-cache.json";
        std::filesystem::create_directories(std::filesystem::path(cache_path).parent_path());
        std::ofstream ofs(cache_path);
        ofs <<
            "{\n"
            "  \"schema_version\": 1,\n"
            "  \"packages\": [\n"
            "    {\n"
            "      \"name\": \"catalog-test-pkg\",\n"
            "      \"description\": \"catalog package\",\n"
            "      \"version\": \"1.2.0\",\n"
            "      \"vivid_core\": \">=0.1.0 <2.0.0\",\n"
            "      \"author\": \"test\",\n"
            "      \"url\": \"https://example.com/catalog-test-pkg.git\"\n"
            "    }\n"
            "  ]\n"
            "}\n";
    }
    std::string vivid_src_dir;
    for (const auto& candidate : {
             std::filesystem::current_path(),
             std::filesystem::current_path().parent_path(),
             std::filesystem::absolute(build_dir),
             std::filesystem::absolute(build_dir).parent_path(),
         }) {
        if (std::filesystem::exists(candidate / "src/operator_api/operator.h")) {
            vivid_src_dir = candidate.string();
            break;
        }
    }
    check(!vivid_src_dir.empty(), "resolved vivid source dir");
    vivid::PackageCompiler pkg_compiler(vivid_src_dir, build_dir);
    vivid::PackageManager pkg_manager(pkg_compiler, registry);
    pkg_manager.scan_installed();
    vivid::PackageCatalog pkg_catalog(pkg_manager);
    pkg_catalog.refresh();
    for (int i = 0; i < 200; ++i) {
        auto st = pkg_catalog.fetch_state();
        if (st == vivid::CatalogFetchState::Ready ||
            st == vivid::CatalogFetchState::Error) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::string scaffold_core_src = build_dir + "/.test_cs_scaffold_core";
    std::filesystem::remove_all(scaffold_core_src);
    std::filesystem::create_directories(scaffold_core_src + "/operators/control");
    std::filesystem::create_directories(scaffold_core_src + "/operators/control/test_op");
    {
        std::ofstream ofs(scaffold_core_src + "/CMakeLists.txt");
        ofs << "cmake_minimum_required(VERSION 3.20)\n"
               "project(vivid_scaffold_core)\n"
               "# --- Control operator plugins ---\n"
               "\n"
               "# --- GPU operator plugins ---\n"
               "\n"
               "# --- Movie File In\n"
               "\n"
               "# --- Movie File Audio In\n";
    }
    {
        std::ofstream ofs(scaffold_core_src + "/operators/control/test_op/test_op.cpp");
        ofs << R"(#include "operator_api/operator.h"

/**
 * @brief Fixture operator docs for MCP/control-server tests.
 *
 * Detailed body for the TestOp fixture.
 *
 * @tip Use this fixture to verify MCP doc merging.
 * @param scale Scale multiplier for the output.
 * @output out Scaled scalar output.
 * @family test-fixture
 */
struct TestOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "TestOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};

VIVID_REGISTER(TestOp)
)";
    }

    std::string local_pkg_src = build_dir + "/.test_cs_linked_pkg_src";
    std::filesystem::remove_all(local_pkg_src);
    std::filesystem::create_directories(local_pkg_src + "/src");
    {
        std::ofstream ofs(local_pkg_src + "/vivid-package.json");
        ofs << "{\n"
               "  \"name\": \"vivid-scaffold-e2e\",\n"
               "  \"version\": \"0.0.1\",\n"
               "  \"build\": \"cmake\",\n"
               "  \"operators\": []\n"
               "}\n";
    }
    {
        std::ofstream ofs(local_pkg_src + "/CMakeLists.txt");
        ofs << "cmake_minimum_required(VERSION 3.20)\n"
               "project(vivid_scaffold_e2e)\n"
               "set(CONTROL_OPS\n"
               ")\n";
    }
    std::string local_pkg_link_root = vivid::PackageManager::packages_dir();
    std::filesystem::create_directories(local_pkg_link_root);
    std::string local_pkg_link = local_pkg_link_root + "/vivid-scaffold-e2e";
    std::error_code sec;
    std::filesystem::remove(local_pkg_link, sec);
    std::filesystem::create_directory_symlink(std::filesystem::absolute(local_pkg_src),
                                              local_pkg_link, sec);
    check(!sec, "created linked local scaffold package");
    pkg_manager.scan_installed();
    {
        auto packages = pkg_manager.list();
        bool found_linked = false;
        for (const auto& p : packages) {
            if (p.name == "vivid-scaffold-e2e" && p.linked) {
                found_linked = true;
                break;
            }
        }
        check(found_linked, "package manager discovered linked scaffold package");
    }

    std::string live_pkg_src = build_dir + "/.test_cs_live_pkg_src";
    std::filesystem::remove_all(live_pkg_src);
    std::filesystem::create_directories(live_pkg_src + "/operators/control/pkg_live_op");
    {
        std::ofstream ofs(live_pkg_src + "/vivid-package.json");
        ofs << "{\n"
               "  \"name\": \"vivid-live-pkg\",\n"
               "  \"version\": \"0.0.1\",\n"
               "  \"operators\": [\"control/pkg_live_op\"]\n"
               "}\n";
    }
    auto write_live_pkg_source = [&](float output_value) {
        std::ofstream ofs(live_pkg_src + "/operators/control/pkg_live_op/pkg_live_op.cpp");
        ofs << "#include \"operator_api/operator.h\"\n\n"
               "/**\n"
               " * @brief Linked package fixture operator.\n"
               " *\n"
               " * Package operator doc fixture used by control-server tests.\n"
               " * @output out Live package scalar output.\n"
               " * @family package-fixture\n"
               " */\n"
               "struct PkgLiveOp : vivid::OperatorBase, vivid::FrameProcessable {\n"
               "    static constexpr const char* kName = \"PkgLiveOp\";\n"
               "    static constexpr bool kTimeDependent = false;\n"
               "    void collect_params(std::vector<vivid::ParamBase*>&) override {}\n"
               "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n"
               "        out.push_back({\"out\", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});\n"
               "    }\n"
               "    void process_frame(const VividFrameContext* ctx) override {\n"
            << "        ctx->output_values[0] = " << std::to_string(output_value) << "f;\n"
               "    }\n"
               "};\n\n"
               "VIVID_REGISTER(PkgLiveOp)\n";
    };
    write_live_pkg_source(3.0f);

    runtime.tick(0.0, 0.016, 0);

    vivid::Settings settings;
    settings.operator_clone_destination_mode = "project_default";
    settings.project_package_name = "vivid-scaffold-e2e";
    vivid::ControlServer server;
    server.set_package_manager(&pkg_manager);
    server.set_package_catalog(&pkg_catalog);
    server.set_src_dir(scaffold_core_src);
    server.set_settings(&settings);
    check(kPort > 0, "find_free_loopback_port()");
    check(server.start(kPort), "server.start()");
    base_url = "http://127.0.0.1:" + std::to_string(server.port());
    std::fprintf(stderr, "  Control server listening on port %d\n", server.port());

    std::atomic<bool> done{false};
    std::atomic<int> phase{0};

    std::thread client_thread([&]() {
        ix::HttpClient client;

#include "test_control_server_client_perception.inc"
#include "test_control_server_client_graph_ops.inc"
#include "test_control_server_client_package_ops.inc"
#include "test_control_server_client_tail.inc"

        done.store(true);
    });

    while (!done.load()) {
        server.process_requests(api, graph, runtime, registry,
                                has_gpu_ops, has_audio);

        int p = phase.load();
        if (p == 1) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 1);
            phase.store(2);
        } else if (p == 3) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 2);
            phase.store(4);
        } else if (p == 5) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 3);
            phase.store(6);
        } else if (p == 7) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 4);
            phase.store(8);
        } else if (p == 9) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 5);
            phase.store(10);
        } else if (p == 11) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 6);
            phase.store(12);
        } else if (p == 13) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 7);
            phase.store(14);
        } else if (p == 15) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 8);
            phase.store(16);
        } else if (p == 17) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 9);
            phase.store(18);
        } else if (p == 19) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 10);
            phase.store(20);
        } else if (p == 21) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 11);
            phase.store(22);
        } else if (p == 23) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 12);
            phase.store(24);
        } else if (p == 25) {
            api.apply_pending(has_gpu_ops, has_audio);
            runtime.tick(0.0, 0.016, 13);
            phase.store(26);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    server.process_requests(api, graph, runtime, registry,
                            has_gpu_ops, has_audio);

    client_thread.join();
    server.stop();
    runtime.shutdown();
    std::filesystem::current_path(original_cwd);
    std::filesystem::remove_all(isolated_cwd);
    std::filesystem::remove_all(staging);
    std::filesystem::remove_all(test_home);

    int f = failures.load();
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        f == 0 ? "ALL PASSED" : "SOME FAILED", f);
    return f == 0 ? 0 : 1;
}
