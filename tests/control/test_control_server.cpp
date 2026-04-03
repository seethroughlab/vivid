#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/runtime_api.h"
#include "runtime/control/control_server.h"
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

// POST helper — returns parsed json, or null json on failure.
// Also validates top-level "ok" field matches expected_ok.
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
    constexpr int kPort = 19876;
    const std::string base_url = "http://127.0.0.1:19876";

    // Isolate package/catalog test state from the user's real config dir.
    std::string test_home = build_dir + "/.test_cs_home";
    std::filesystem::remove_all(test_home);
    std::filesystem::create_directories(test_home);
    setenv("HOME", test_home.c_str(), 1);
    setenv("VIVID_SKIP_PACKAGE_CATALOG_NETWORK", "1", 1);
    setenv("VISUAL", "true", 1);

    // --- Setup: staging dir with test_op_v1 ---
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

    std::string abi_probe_dir = build_dir + "/.test_cs_abi_probe";
    std::filesystem::create_directories(abi_probe_dir);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
        abi_probe_dir + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: ControlServer ===\n\n");

    // --- Runtime setup ---
    vivid::OperatorRegistry registry;
    setenv("VIVID_MOCK_RUNTIME_ABI", "999", 1);
    check(registry.scan_deferred(abi_probe_dir.c_str()), "registry.scan_deferred() for ABI diagnostics");
    unsetenv("VIVID_MOCK_RUNTIME_ABI");
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    vivid::AudioEngine audio_engine;
    bool has_gpu_ops = false;
    bool has_audio = false;

    vivid::RuntimeAPI api(graph, runtime, audio_engine, registry);

    // --- Package manager + catalog setup for MCP/API coverage ---
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

    // Scaffold destination e2e fixtures:
    // - core src root for fallback-to-core path
    // - linked local package for project package scaffolding
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

    // Tick once so nodes have output values
    runtime.tick(0.0, 0.016, 0);

    // --- Start ControlServer ---
    vivid::Settings settings;
    settings.operator_clone_destination_mode = "project_default";
    settings.project_package_name = "vivid-scaffold-e2e";
    vivid::ControlServer server;
    server.set_package_manager(&pkg_manager);
    server.set_package_catalog(&pkg_catalog);
    server.set_src_dir(scaffold_core_src);
    server.set_settings(&settings);
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
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& nodes = result["nodes"];
                auto& conns = result["connections"];
                check(nodes.is_array() && nodes.size() == 2, "2 nodes");
                check(conns.is_array() && conns.size() == 1, "1 connection");

                // Check that params have values
                auto& first_node = nodes[0];
                auto& params = first_node["params"];
                check(params.is_array() && params.size() > 0, "params have values");
                if (params.is_array() && params.size() > 0) {
                    auto& p0 = params[0];
                    check(p0.contains("type") && p0["type"].is_string(), "inspect_graph params expose type");
                    check(p0.contains("semantic_tag") && p0["semantic_tag"].is_string() &&
                              p0["semantic_tag"].get<std::string>() == "frequency_hz",
                          "inspect_graph params expose semantic_tag");
                    check(p0.contains("semantic_shape") && p0["semantic_shape"].is_string() &&
                              p0["semantic_shape"].get<std::string>() == "scalar",
                          "inspect_graph params expose semantic_shape");
                    check(p0.contains("semantic_unit") && p0["semantic_unit"].is_string() &&
                              p0["semantic_unit"].get<std::string>() == "Hz",
                          "inspect_graph params expose semantic_unit");
                    check(p0.contains("semantic_intent") && p0["semantic_intent"].is_string() &&
                              p0["semantic_intent"].get<std::string>() == "test_scale",
                          "inspect_graph params expose semantic_intent");
                }
            }
        }

        // Phase 1b: introspect_nodes — per-node perception payload
        std::fprintf(stderr, "\n--- introspect_nodes ---\n");
        {
            auto r = post(client, base_url, "introspect_nodes");
            check(r.ok, "introspect_nodes ok");
            if (!r.j.is_null()) {
                check(r.j.contains("schema_version") && r.j["schema_version"].is_number_integer() &&
                          r.j["schema_version"].get<int>() == 1,
                      "introspect_nodes schema_version=1");

                auto& result = r.j["result"];
                auto& nodes = result["nodes"];
                check(nodes.is_array(), "introspect_nodes returns nodes array");
                check(nodes.is_array() && nodes.size() == 2, "introspect_nodes returns 2 nodes");

                auto& first_node = nodes[0];
                check(first_node.contains("node_id") && first_node["node_id"].is_string(), "introspection node has node_id");
                check(first_node.contains("type") && first_node["type"].is_string(), "introspection node has type");
                check(first_node.contains("kind") && first_node["kind"].is_string(), "introspection node has kind");
                check(first_node.contains("health") && first_node["health"].is_object(), "introspection node has health object");
                check(first_node.contains("params") && first_node["params"].is_object(), "introspection node has params object");
                check(first_node.contains("param_meta") && first_node["param_meta"].is_array(), "introspection node has param_meta array");
                if (first_node.contains("param_meta") && first_node["param_meta"].is_array() &&
                    first_node["param_meta"].size() > 0) {
                    auto& pm0 = first_node["param_meta"][0];
                    check(pm0.contains("semantic_tag") && pm0["semantic_tag"].is_string() &&
                              pm0["semantic_tag"].get<std::string>() == "frequency_hz",
                          "introspection param_meta exposes semantic_tag");
                    check(pm0.contains("semantic_shape") && pm0["semantic_shape"].is_string() &&
                              pm0["semantic_shape"].get<std::string>() == "scalar",
                          "introspection param_meta exposes semantic_shape");
                    check(pm0.contains("semantic_unit") && pm0["semantic_unit"].is_string() &&
                              pm0["semantic_unit"].get<std::string>() == "Hz",
                          "introspection param_meta exposes semantic_unit");
                    check(pm0.contains("semantic_intent") && pm0["semantic_intent"].is_string() &&
                              pm0["semantic_intent"].get<std::string>() == "test_scale",
                          "introspection param_meta exposes semantic_intent");
                }
                check(first_node.contains("inputs") && first_node["inputs"].is_array(), "introspection node has inputs array");
                check(first_node.contains("outputs") && first_node["outputs"].is_array(), "introspection node has outputs array");
                check(first_node.contains("env_metrics") && first_node["env_metrics"].is_object(),
                      "introspection node has env_metrics object");
                check(first_node.contains("incoming_wires") && first_node["incoming_wires"].is_number_integer(),
                      "introspection node has incoming_wires");
                check(first_node.contains("outgoing_wires") && first_node["outgoing_wires"].is_number_integer(),
                      "introspection node has outgoing_wires");
            }
        }

        // Phase 1c: run_diagnostics — graph-level perception findings
        std::fprintf(stderr, "\n--- run_diagnostics ---\n");
        {
            auto r = post(client, base_url, "run_diagnostics");
            check(r.ok, "run_diagnostics ok");
            if (!r.j.is_null()) {
                check(r.j.contains("schema_version") && r.j["schema_version"].is_number_integer() &&
                          r.j["schema_version"].get<int>() == 1,
                      "run_diagnostics schema_version=1");

                auto& result = r.j["result"];
                auto& summary = result["summary"];
                auto& findings = result["findings"];
                auto& hints = result["hints"];

                check(summary.is_object(), "run_diagnostics has summary");
                check(findings.is_array(), "run_diagnostics has findings array");
                check(hints.is_array(), "run_diagnostics has hints array");

                if (summary.is_object()) {
                    check(summary.contains("critical") && summary["critical"].is_number_integer(), "summary has critical count");
                    check(summary.contains("warning") && summary["warning"].is_number_integer(), "summary has warning count");
                    check(summary.contains("info") && summary["info"].is_number_integer(), "summary has info count");
                    if (summary.contains("critical") && summary.contains("warning") && summary.contains("info") &&
                        summary["critical"].is_number_integer() && summary["warning"].is_number_integer() &&
                        summary["info"].is_number_integer()) {
                        check(summary["critical"].get<int>() == 0 &&
                              summary["warning"].get<int>() == 0 &&
                              summary["info"].get<int>() == 0,
                              "healthy fixture graph yields minimal diagnostics");
                    }
                }
            }
        }
        // Deterministic ordering regression: repeated diagnostics return stable finding id order.
        {
            auto r1 = post(client, base_url, "run_diagnostics");
            auto r2 = post(client, base_url, "run_diagnostics");
            check(r1.ok && r2.ok, "run_diagnostics repeat calls ok");
            std::string ids1, ids2;
            if (!r1.j.is_null()) {
                auto& result = r1.j["result"];
                auto& findings = result["findings"];
                if (findings.is_array()) {
                    for (const auto& f : findings) {
                        if (!ids1.empty()) ids1 += ",";
                        ids1 += f.value("id", "");
                        ids1 += "@";
                        ids1 += f.value("node_id", "");
                    }
                }
            }
            if (!r2.j.is_null()) {
                auto& result = r2.j["result"];
                auto& findings = result["findings"];
                if (findings.is_array()) {
                    for (const auto& f : findings) {
                        if (!ids2.empty()) ids2 += ",";
                        ids2 += f.value("id", "");
                        ids2 += "@";
                        ids2 += f.value("node_id", "");
                    }
                }
            }
            check(ids1 == ids2, "run_diagnostics findings order deterministic");
        }
        // Broken fixture regression: intentionally disconnected node should emit expected warning.
        {
            auto add_missing = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"missing_fixture"})");
            check(add_missing.ok, "add_node broken fixture ok");
            phase.store(5);
            while (phase.load() < 6) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto r = post(client, base_url, "run_diagnostics");
            check(r.ok, "run_diagnostics on broken fixture ok");
            bool found_missing = false;
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& summary = result["summary"];
                auto& findings = result["findings"];
                if (summary.is_object()) {
                    check(summary.contains("warning") && summary["warning"].is_number_integer() &&
                              summary["warning"].get<int>() >= 1,
                          "broken fixture emits warning diagnostics");
                }
                if (findings.is_array()) {
                    for (const auto& f : findings) {
                        if (f.value("id", "") == "isolated_node" &&
                            f.value("node_id", "") == "missing_fixture") {
                            found_missing = true;
                            break;
                        }
                    }
                }
            }
            check(found_missing, "broken fixture has isolated_node finding for missing_fixture");

            auto remove_missing = post(client, base_url, "remove_node",
                R"({"node_id":"missing_fixture"})");
            check(remove_missing.ok, "remove_node missing fixture ok");
            phase.store(7);
            while (phase.load() < 8) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after broken fixture cleanup ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& nodes = result["nodes"];
                check(nodes.is_array() && nodes.size() == 2,
                      "broken fixture cleanup restored 2-node graph");
            }
        }

        // Phase 1d: validate_checks / run_checks
        std::fprintf(stderr, "\n--- checks ---\n");
        {
            auto r = post(client, base_url, "validate_checks",
                R"({"checks":[{"id":"node_count_is_two","type":"state_check","path":"graph.node_count","op":"==","value":2},{"id":"no_missing_ops","type":"diagnostic_check","op":"finding_absent","finding_id":"missing_operator_type"}]})");
            check(r.ok, "validate_checks ok");
            if (!r.j.is_null()) {
                check(r.j.contains("schema_version") && r.j["schema_version"].is_number_integer() &&
                          r.j["schema_version"].get<int>() == 1,
                      "validate_checks schema_version=1");
                auto& result = r.j["result"];
                check(result.contains("valid") && result["valid"].is_boolean() && result["valid"].get<bool>(),
                      "validate_checks valid=true");
                check(result.contains("error_count") && result["error_count"].is_number_integer() &&
                          result["error_count"].get<int>() == 0,
                      "validate_checks error_count=0");
            }
        }
        {
            auto r = post(client, base_url, "validate_checks",
                R"({"checks":[{"id":"dup","type":"state_check","path":"graph.node_count","op":"==","value":2},{"id":"dup","type":"state_check","path":"graph.node_count","op":"==","value":2}]})");
            check(r.ok, "validate_checks duplicate-id request ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result.contains("valid") && result["valid"].is_boolean() && !result["valid"].get<bool>(),
                      "validate_checks duplicate-id valid=false");
                check(result.contains("error_count") && result["error_count"].is_number_integer() &&
                          result["error_count"].get<int>() > 0,
                      "validate_checks duplicate-id has error_count>0");
            }
            auto rbad = post(client, base_url, "validate_checks", R"({"foo":[]})");
            check(!rbad.ok, "validate_checks missing checks array -> ok=false");
        }
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[{"id":"node_count_is_two","type":"state_check","path":"graph.node_count","op":"==","value":2,"severity":"critical"},{"id":"no_missing_ops","type":"diagnostic_check","op":"finding_absent","finding_id":"missing_operator_type","severity":"critical"}]})");
            check(r.ok, "run_checks ok");
            if (!r.j.is_null()) {
                check(r.j.contains("schema_version") && r.j["schema_version"].is_number_integer() &&
                          r.j["schema_version"].get<int>() == 1,
                      "run_checks schema_version=1");
                auto& result = r.j["result"];
                check(result.contains("all_passed") && result["all_passed"].is_boolean() &&
                          result["all_passed"].get<bool>(),
                      "run_checks all_passed=true");
                check(result.contains("all_critical_passed") && result["all_critical_passed"].is_boolean() &&
                          result["all_critical_passed"].get<bool>(),
                      "run_checks all_critical_passed=true");
                check(result.contains("summary") && result["summary"].is_object(), "run_checks has summary");
                check(result.contains("results") && result["results"].is_array() &&
                          result["results"].size() == 2,
                      "run_checks returns 2 results");
            }
        }
        // Deterministic check result ordering regression: results sorted by id regardless of input order.
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[{"id":"z_second","type":"state_check","path":"graph.node_count","op":"==","value":2},{"id":"a_first","type":"state_check","path":"graph.node_count","op":"==","value":2}]})");
            check(r.ok, "run_checks deterministic-order request ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& rows = result["results"];
                check(rows.is_array() && rows.size() == 2,
                      "run_checks deterministic-order returns 2 results");
                if (rows.is_array() && rows.size() == 2) {
                    check(rows[0].value("id", "") == "a_first",
                          "run_checks results sorted id[0]=a_first");
                    check(rows[1].value("id", "") == "z_second",
                          "run_checks results sorted id[1]=z_second");
                }
            }
        }

        // Perception API regression: read-only endpoints must not mutate graph state.
        {
            auto gp_before = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(gp_before.ok, "pre-mutation baseline get_param ok");
            double before = 0.0;
            if (!gp_before.j.is_null()) {
                if (gp_before.j.contains("value") && gp_before.j["value"].is_number())
                    before = gp_before.j["value"].get<double>();
            }

            auto i = post(client, base_url, "introspect_nodes");
            auto d = post(client, base_url, "run_diagnostics");
            auto vc = post(client, base_url, "validate_checks",
                R"({"checks":[{"id":"baseline","type":"state_check","path":"graph.node_count","op":"==","value":2}]})");
            auto rc = post(client, base_url, "run_checks",
                R"({"checks":[{"id":"baseline","type":"state_check","path":"graph.node_count","op":"==","value":2}]})");
            check(i.ok && d.ok && vc.ok && rc.ok, "perception endpoints all return ok");

            auto gp_after = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(gp_after.ok, "post-perception get_param ok");
            if (!gp_after.j.is_null()) {
                check(gp_after.j.contains("value") && gp_after.j["value"].is_number() &&
                          std::fabs(gp_after.j["value"].get<double>() - before) < 1e-6,
                      "perception endpoints do not mutate parameter state");
            }

            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after perception endpoints ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& nodes = result["nodes"];
                auto& conns = result["connections"];
                check(nodes.is_array() && nodes.size() == 2,
                      "node count unchanged after perception endpoints");
                check(conns.is_array() && conns.size() == 1,
                      "connection count unchanged after perception endpoints");
            }
        }

        // get_graph_load_diagnostics
        std::fprintf(stderr, "\n--- get_graph_load_diagnostics ---\n");
        {
            auto r = post(client, base_url, "get_graph_load_diagnostics");
            check(r.ok, "get_graph_load_diagnostics ok");
            if (!r.j.is_null()) {
                check(r.j.contains("result") && r.j["result"].is_object(),
                      "get_graph_load_diagnostics has result");
                auto& result = r.j["result"];
                check(result.contains("graph_load_diagnostics") && result["graph_load_diagnostics"].is_array(),
                      "result has graph_load_diagnostics array");
                check(result["graph_load_diagnostics"].size() == 0,
                      "healthy graph has empty load diagnostics");
            }
        }

        // Extended checks: comparison operators
        std::fprintf(stderr, "\n--- checks: comparison operators ---\n");
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"neq","type":"state_check","path":"graph.node_count","op":"!=","value":99},
                    {"id":"gt","type":"state_check","path":"graph.node_count","op":">","value":1},
                    {"id":"gte","type":"state_check","path":"graph.node_count","op":">=","value":2},
                    {"id":"lt","type":"state_check","path":"graph.node_count","op":"<","value":99},
                    {"id":"lte","type":"state_check","path":"graph.node_count","op":"<=","value":2}
                ]})");
            check(r.ok, "run_checks comparison ops ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result["all_passed"].get<bool>(), "all comparison ops pass");
                check(result["results"].size() == 5, "5 comparison results");
            }
        }

        // Extended checks: between operator
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"a_between_pass","type":"state_check","path":"graph.node_count","op":"between","value":[1,5]},
                    {"id":"b_between_fail","type":"state_check","path":"graph.node_count","op":"between","value":[10,20]}
                ]})");
            check(r.ok, "run_checks between ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(!result["all_passed"].get<bool>(), "between: not all passed (one should fail)");
                auto& rows = result["results"];
                // Results sorted by id: a_between_pass, b_between_fail
                if (rows.is_array() && rows.size() == 2) {
                    check(rows[0]["passed"].get<bool>(), "between [1,5] passes for node_count=2");
                    check(!rows[1]["passed"].get<bool>(), "between [10,20] fails for node_count=2");
                }
            }
        }

        // Extended checks: exists / not_exists
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"a_exists_ok","type":"state_check","path":"nodes.a.kind","op":"exists"},
                    {"id":"b_not_exists_ok","type":"state_check","path":"nodes.zzz.kind","op":"not_exists"},
                    {"id":"c_exists_fail","type":"state_check","path":"nodes.zzz.kind","op":"exists"}
                ]})");
            check(r.ok, "run_checks exists/not_exists ok");
            if (!r.j.is_null()) {
                auto& rows = r.j["result"]["results"];
                // Results sorted by id: a_exists_ok, b_not_exists_ok, c_exists_fail
                if (rows.is_array() && rows.size() == 3) {
                    check(rows[0]["passed"].get<bool>(), "exists on nodes.a.kind passes");
                    check(rows[1]["passed"].get<bool>(), "not_exists on nodes.zzz.kind passes");
                    check(!rows[2]["passed"].get<bool>(), "exists on nodes.zzz.kind fails");
                }
            }
        }

        // Extended checks: state paths
        std::fprintf(stderr, "\n--- checks: state paths ---\n");
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"kind","type":"state_check","path":"nodes.a.kind","op":"==","value":"control"},
                    {"id":"health_err","type":"state_check","path":"nodes.a.health.errored","op":"==","value":false},
                    {"id":"health_miss","type":"state_check","path":"nodes.a.health.missing_operator","op":"==","value":false},
                    {"id":"param_val","type":"state_check","path":"nodes.a.params.scale","op":"==","value":3.0},
                    {"id":"output_val","type":"state_check","path":"nodes.a.outputs.out.scalar","op":"==","value":6.0},
                    {"id":"a_out_wires","type":"state_check","path":"nodes.a.outgoing_wires","op":"==","value":1},
                    {"id":"b_in_wires","type":"state_check","path":"nodes.b.incoming_wires","op":"==","value":1}
                ]})");
            check(r.ok, "run_checks state paths ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result["all_passed"].get<bool>(), "all state path checks pass");
                check(result["results"].size() == 7, "7 state path results");
            }
        }

        // Extended checks: tolerance
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"a_tol_pass","type":"state_check","path":"nodes.a.params.scale","op":"==","value":3.01,"tolerance":0.1},
                    {"id":"b_tol_fail","type":"state_check","path":"nodes.a.params.scale","op":"==","value":4.0,"tolerance":0.1}
                ]})");
            check(r.ok, "run_checks tolerance ok");
            if (!r.j.is_null()) {
                auto& rows = r.j["result"]["results"];
                // Results sorted by id: a_tol_pass, b_tol_fail
                if (rows.is_array() && rows.size() == 2) {
                    check(rows[0]["passed"].get<bool>(), "tolerance 0.1: 3.0 == 3.01 passes");
                    check(!rows[1]["passed"].get<bool>(), "tolerance 0.1: 3.0 == 4.0 fails");
                }
            }
        }

        // Extended checks: when guards
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"guard_pass","type":"state_check","path":"nodes.a.params.scale","op":"==","value":3.0,
                     "when":{"path":"nodes.a.kind","op":"==","value":"control"}},
                    {"id":"guard_skip","type":"state_check","path":"nodes.a.params.scale","op":"==","value":3.0,
                     "when":{"path":"nodes.a.kind","op":"==","value":"audio"}}
                ]})");
            check(r.ok, "run_checks when guards ok");
            if (!r.j.is_null()) {
                auto& rows = r.j["result"]["results"];
                if (rows.is_array() && rows.size() == 2) {
                    check(rows[0]["passed"].get<bool>(), "guard met: check passes");
                    check(!rows[0].value("skipped", true), "guard met: check not skipped");
                    check(rows[1].value("skipped", false), "guard not met: check skipped");
                }
            }
        }

        // Extended checks: failing checks with severity reporting
        std::fprintf(stderr, "\n--- checks: failure reporting ---\n");
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"crit_fail","type":"state_check","path":"graph.node_count","op":"==","value":999,"severity":"critical","message":"custom failure msg"},
                    {"id":"warn_fail","type":"state_check","path":"graph.node_count","op":"==","value":999,"severity":"warning"}
                ]})");
            check(r.ok, "run_checks failure reporting ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(!result["all_passed"].get<bool>(), "all_passed=false when checks fail");
                check(!result["all_critical_passed"].get<bool>(), "all_critical_passed=false when critical fails");
                auto& summary = result["summary"];
                check(summary["critical_failed"].get<int>() == 1, "critical_failed=1");
                check(summary["warning_failed"].get<int>() == 1, "warning_failed=1");
                auto& rows = result["results"];
                if (rows.is_array() && rows.size() == 2) {
                    check(!rows[0]["passed"].get<bool>(), "critical check reports passed=false");
                    check(rows[0]["severity"].get<std::string>() == "critical", "result has severity=critical");
                    check(rows[0]["message"].get<std::string>() == "custom failure msg", "result has custom message");
                }
            }
        }
        // Warning-only failure preserves all_critical_passed
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"warn_only","type":"state_check","path":"graph.node_count","op":"==","value":999,"severity":"warning"}
                ]})");
            check(r.ok, "run_checks warning-only failure ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(!result["all_passed"].get<bool>(), "all_passed=false for warning failure");
                check(result["all_critical_passed"].get<bool>(), "all_critical_passed=true when only warning fails");
            }
        }

        // Extended checks: diagnostic_check finding_present
        // (missing_fixture was removed, so use finding_absent for isolated_node on current graph)
        // We'll test count_by_severity ops on the healthy graph
        std::fprintf(stderr, "\n--- checks: diagnostic_check ops ---\n");
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"count_eq_zero","type":"diagnostic_check","op":"count_by_severity_eq","check_severity":"critical","value":0},
                    {"id":"count_lte","type":"diagnostic_check","op":"count_by_severity_lte","check_severity":"warning","value":100},
                    {"id":"count_gte","type":"diagnostic_check","op":"count_by_severity_gte","check_severity":"warning","value":0},
                    {"id":"finding_absent_ok","type":"diagnostic_check","op":"finding_absent","finding_id":"missing_operator_type"}
                ]})");
            check(r.ok, "run_checks diagnostic ops ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result["all_passed"].get<bool>(), "all diagnostic checks pass on healthy graph");
            }
        }

        // Phase 2: list_types
        std::fprintf(stderr, "\n--- list_types ---\n");
        {
            auto r = post(client, base_url, "list_types");
            check(r.ok, "list_types ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& types = result["types"];
                check(types.is_array() && types.size() > 0, "has types");
                if (types.is_array()) {
                    json t0;
                    json ms_type;
                    json sec_type;
                    json unknown_type;
                    json untagged_type;
                    json custom_type;
                    for (const auto& t : types) {
                        if (!t.contains("name") || !t["name"].is_string()) continue;
                        std::string tn = t["name"].get<std::string>();
                        if (tn == "TestOp") {
                            t0 = t;
                        } else if (tn == "MsSourceOp") {
                            ms_type = t;
                        } else if (tn == "SecDestOp") {
                            sec_type = t;
                        } else if (tn == "UnknownTagSourceOp") {
                            unknown_type = t;
                        } else if (tn == "UntaggedDestOp") {
                            untagged_type = t;
                        } else if (tn == "ExportCustomPortOp") {
                            custom_type = t;
                        }
                    }
                    check(!t0.is_null(), "contains TestOp");
                    check(t0.contains("brief") && t0["brief"].is_string() &&
                              t0["brief"].get<std::string>() ==
                                  "Fixture operator docs for MCP/control-server tests.",
                          "list_types merges doc brief");
                    check(t0.contains("has_docs") && t0["has_docs"].is_boolean() &&
                              t0["has_docs"].get<bool>(),
                          "list_types marks documented operator");
                    check(t0.contains("operator_family") && t0["operator_family"].is_string() &&
                              t0["operator_family"].get<std::string>() == "test-fixture",
                          "list_types exposes operator_family");
                    check(t0.contains("lane_behavior_help") && t0["lane_behavior_help"].is_string() &&
                              !t0["lane_behavior_help"].get<std::string>().empty(),
                          "list_types exposes lane_behavior_help");
                    check(!t0.contains("params") && !t0.contains("outputs"),
                          "list_types stays compact and omits rich descriptor arrays");
                    check(!ms_type.is_null(), "contains MsSourceOp");
                    check(!sec_type.is_null(), "contains SecDestOp");
                    check(!unknown_type.is_null(), "contains UnknownTagSourceOp");
                    check(!untagged_type.is_null(), "contains UntaggedDestOp");
                    check(!custom_type.is_null(), "contains ExportCustomPortOp");
                }
            }
        }

        // Phase 2a: operator_docs
        std::fprintf(stderr, "\n--- operator_docs ---\n");
        {
            auto r = post(client, base_url, "operator_docs",
                R"({"name":"TestOp"})");
            check(r.ok, "operator_docs TestOp ok");
            if (!r.j.is_null()) {
                auto& op = r.j["result"];
                check(op.contains("has_docs") && op["has_docs"].is_boolean() &&
                          op["has_docs"].get<bool>(),
                      "operator_docs reports docs present");
                check(op.contains("brief") && op["brief"].is_string() &&
                          op["brief"].get<std::string>() ==
                              "Fixture operator docs for MCP/control-server tests.",
                      "operator_docs merges brief");
                check(op.contains("body") && op["body"].is_string() &&
                          op["body"].get<std::string>() == "Detailed body for the TestOp fixture.",
                      "operator_docs merges body");
                check(op.contains("source_path") && op["source_path"].is_string() &&
                          op["source_path"].get<std::string>() == "operators/control/test_op/test_op.cpp",
                      "operator_docs merges source_path");
                check(op.contains("lane_behavior_help") && op["lane_behavior_help"].is_string() &&
                          !op["lane_behavior_help"].get<std::string>().empty(),
                      "operator_docs includes lane_behavior_help");
                auto& params_obj = op["params"];
                check(params_obj.is_array() && params_obj.size() == 1, "operator_docs returns params");
                if (params_obj.is_array() && params_obj.size() == 1) {
                    auto& p0 = params_obj[0];
                    check(p0.contains("doc") && p0["doc"].is_string() &&
                              p0["doc"].get<std::string>() == "Scale multiplier for the output.",
                          "operator_docs merges param doc");
                }
                auto& outputs_obj = op["outputs"];
                check(outputs_obj.is_array() && outputs_obj.size() == 1, "operator_docs returns outputs");
                if (outputs_obj.is_array() && outputs_obj.size() == 1) {
                    auto& p0 = outputs_obj[0];
                    check(p0.contains("doc") && p0["doc"].is_string() &&
                              p0["doc"].get<std::string>() == "Scaled scalar output.",
                          "operator_docs merges output doc");
                    check(p0.contains("semantic_shape") && p0["semantic_shape"].is_string() &&
                              p0["semantic_shape"].get<std::string>() == "scalar",
                          "operator_docs preserves port semantic_shape");
                }
            }

            auto fallback = post(client, base_url, "operator_docs",
                R"({"name":"ExportCustomPortOp"})");
            check(fallback.ok, "operator_docs descriptor-only fallback ok");
            if (!fallback.j.is_null()) {
                auto& op = fallback.j["result"];
                check(op.contains("has_docs") && op["has_docs"].is_boolean() &&
                          !op["has_docs"].get<bool>(),
                      "operator_docs descriptor-only fallback marks has_docs false");
                check(op.contains("outputs") && op["outputs"].is_array() &&
                          op["outputs"].size() == 1,
                      "operator_docs descriptor-only fallback still returns outputs");
            }
        }

        // Phase 2b: get_registry_diagnostics
        std::fprintf(stderr, "\n--- get_registry_diagnostics ---\n");
        {
            auto r = post(client, base_url, "get_registry_diagnostics");
            check(r.ok, "get_registry_diagnostics ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result.contains("schema_version") && result["schema_version"].is_number_integer() &&
                          result["schema_version"].get<int>() == 1,
                      "registry diagnostics schema_version=1");
                auto& custom_types = result["custom_port_types"];
                auto& abi_mismatches = result["abi_mismatch_diagnostics"];
                auto& loader_failures = result["loader_failure_diagnostics"];
                check(custom_types.is_array() && custom_types.size() > 0,
                      "registry diagnostics exposes custom_port_types");
                check(abi_mismatches.is_array() && abi_mismatches.size() > 0,
                      "registry diagnostics exposes ABI mismatch diagnostics");
                check(loader_failures.is_array() && loader_failures.size() > 0,
                      "registry diagnostics exposes loader failure diagnostics");
                if (custom_types.is_array()) {
                    bool found_test_custom_ref = false;
                    for (const auto& item : custom_types) {
                        if (!item.contains("stable_type_id") || !item["stable_type_id"].is_string()) continue;
                        if (item["stable_type_id"].get<std::string>() !=
                                "seethroughlab.vivid.test_custom_ref") continue;
                        found_test_custom_ref = true;
                        check(item.contains("transport") && item["transport"].is_string() &&
                                  item["transport"].get<std::string>() == "custom_ref",
                              "registry diagnostics preserves custom port transport");
                        check(item.contains("audio_safe") && item["audio_safe"].is_boolean(),
                              "registry diagnostics preserves audio_safe");
                        break;
                    }
                    check(found_test_custom_ref, "registry diagnostics includes TestCustomRef");
                }
                if (abi_mismatches.is_array() && abi_mismatches.size() > 0) {
                    auto& first = abi_mismatches[0];
                    check(first.contains("plugin_abi") && first["plugin_abi"].is_number_unsigned(),
                          "ABI diagnostic includes plugin_abi");
                    check(first.contains("runtime_abi") && first["runtime_abi"].is_number_unsigned() &&
                              first["runtime_abi"].get<uint64_t>() == 999,
                          "ABI diagnostic includes mocked runtime ABI");
                }
                if (loader_failures.is_array() && loader_failures.size() > 0) {
                    bool found_bad_custom = false;
                    for (const auto& item : loader_failures) {
                        if (!item.contains("plugin_name") || !item["plugin_name"].is_string()) continue;
                        if (item["plugin_name"].get<std::string>() != "test_op_bad_custom_type.dylib")
                            continue;
                        found_bad_custom = true;
                        check(item.contains("code") && item["code"].is_string() &&
                                  item["code"].get<std::string>() == "custom_type_registration_failed",
                              "loader failure diagnostic exposes structured code");
                        check(item.contains("message") && item["message"].is_string() &&
                                  item["message"].get<std::string>().find("register") != std::string::npos,
                              "loader failure diagnostic exposes useful message");
                    }
                    check(found_bad_custom, "registry diagnostics include bad custom-type loader failure");
                }
            }
        }

        // operator_map
        std::fprintf(stderr, "\n--- operator_map ---\n");
        {
            auto r = post(client, base_url, "operator_map");
            check(r.ok, "operator_map ok");
            if (!r.j.is_null()) {
                check(r.j.contains("result") && r.j["result"].is_array(),
                      "operator_map result is array");
                auto& result = r.j["result"];
                bool found_test_op = false;
                bool found_abi_mismatch = false;
                for (const auto& entry : result) {
                    check(entry.contains("type") && entry["type"].is_string(),
                          "operator_map entry has type");
                    check(entry.contains("status") && entry["status"].is_string(),
                          "operator_map entry has status");
                    if (entry["type"].get<std::string>() == "TestOp" &&
                        entry["status"].get<std::string>() == "loaded")
                        found_test_op = true;
                    if (entry.contains("status") && entry["status"].is_string() &&
                        entry["status"].get<std::string>() == "abi_mismatch")
                        found_abi_mismatch = true;
                }
                check(found_test_op, "operator_map contains TestOp with status=loaded");
                check(found_abi_mismatch, "operator_map contains abi_mismatch entry");
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
            if (!r.j.is_null()) {
                check(r.j.contains("value") &&
                          std::fabs(r.j["value"].get<double>() - 9.0) < 1e-4,
                      "value is 9.0");
            }
        }

        // Phase 4b: undo/redo via MCP control methods
        std::fprintf(stderr, "\n--- undo/redo ---\n");
        {
            auto ru = post(client, base_url, "undo");
            check(ru.ok, "undo ok");
            auto rg = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(rg.ok, "get_param after undo ok");
            if (!rg.j.is_null()) {
                check(rg.j.contains("value") &&
                          std::fabs(rg.j["value"].get<double>() - 3.0) < 1e-4,
                      "value restored to 3.0 after undo");
            }

            auto rr = post(client, base_url, "redo");
            check(rr.ok, "redo ok");
            auto rg2 = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(rg2.ok, "get_param after redo ok");
            if (!rg2.j.is_null()) {
                check(rg2.j.contains("value") &&
                          std::fabs(rg2.j["value"].get<double>() - 9.0) < 1e-4,
                      "value restored to 9.0 after redo");
            }
        }

        // set_string_param
        std::fprintf(stderr, "\n--- set_string_param ---\n");
        {
            // TestOp has no string/file params, so this should fail
            auto r = post(client, base_url, "set_string_param",
                R"({"node_id":"a","param":"scale","value":"hello"})");
            check(!r.ok, "set_string_param on float param fails");

            auto r2 = post(client, base_url, "set_string_param",
                R"({"node_id":"nonexistent","param":"x","value":"y"})");
            check(!r2.ok, "set_string_param on nonexistent node fails");

            auto r3 = post(client, base_url, "set_string_param", R"({})");
            check(!r3.ok, "set_string_param with empty body fails");
        }

        // sample_node_outputs
        std::fprintf(stderr, "\n--- sample_node_outputs ---\n");
        {
            auto r = post(client, base_url, "sample_node_outputs",
                R"({"node_id":"a","duration_seconds":0.05,"interval_ms":10})");
            check(r.ok, "sample_node_outputs ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result["node_id"].get<std::string>() == "a", "sample result node_id=a");
                check(result["type"].get<std::string>() == "TestOp", "sample result type=TestOp");
                check(result["kind"].get<std::string>() == "control", "sample result kind=control");
                check(result["sample_count"].get<int>() >= 1, "sample_count >= 1");
                check(result["samples"].is_array() && result["samples"].size() >= 1,
                      "samples array non-empty");
                if (result["samples"].is_array() && result["samples"].size() > 0) {
                    auto& s0 = result["samples"][0];
                    check(s0.contains("time_seconds") && s0["time_seconds"].is_number(),
                          "sample has time_seconds");
                    check(s0.contains("outputs") && s0["outputs"].is_object(),
                          "sample has outputs object");
                    if (s0["outputs"].contains("out")) {
                        check(s0["outputs"]["out"].contains("scalar"),
                              "sample output has scalar field");
                    }
                }
            }

            // Error: nonexistent node
            auto r2 = post(client, base_url, "sample_node_outputs",
                R"({"node_id":"nonexistent","duration_seconds":0.01,"interval_ms":10})");
            check(!r2.ok, "sample_node_outputs on nonexistent node fails");
        }

        // Phase 5: add_node — add TestOp as "c"
        std::fprintf(stderr, "\n--- add_node ---\n");
        {
            auto r = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"c"})");
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
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& nodes = result["nodes"];
                auto& conns = result["connections"];
                check(nodes.is_array() && nodes.size() == 3, "3 nodes");
                check(conns.is_array() && conns.size() == 2, "2 connections");
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
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& nodes = result["nodes"];
                check(nodes.is_array() && nodes.size() == 2, "back to 2 nodes");
            }
        }

        // Phase 10b: semantic-default connect remap
        std::fprintf(stderr, "\n--- semantic default connect remap ---\n");
        {
            auto add_src = post(client, base_url, "add_node",
                R"({"type":"MsSourceOp","node_id":"ms1"})");
            check(add_src.ok, "add_node MsSourceOp ms1 ok");

            auto add_dst = post(client, base_url, "add_node",
                R"({"type":"SecDestOp","node_id":"s1"})");
            check(add_dst.ok, "add_node SecDestOp s1 ok");

            auto c = post(client, base_url, "connect",
                R"({"from_addr":"ms1/ms","to_addr":"s1/sec","semantic_defaults":true})");
            check(c.ok, "connect ms1/ms -> s1/sec with semantic_defaults ok");
            if (!c.j.is_null()) {
                std::string m = c.j.value("message", "");
                check(m.find("semantic default remap applied") != std::string::npos,
                      "connect response reports semantic remap applied");
                check(c.j.contains("inferred_remap_applied") &&
                          c.j["inferred_remap_applied"].is_boolean() &&
                          c.j["inferred_remap_applied"].get<bool>(),
                      "connect response inferred_remap_applied=true");
                check(c.j.contains("inferred_remap") && c.j["inferred_remap"].is_object(),
                      "connect response includes inferred_remap object");
            }
        }

        phase.store(9);
        while (phase.load() < 10) std::this_thread::sleep_for(std::chrono::milliseconds(5));

        {
            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after semantic connect ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& conns = result["connections"];
                bool found = false;
                if (conns.is_array()) {
                    for (const auto& conn : conns) {
                        if (conn.value("from", "") != "ms1/ms" ||
                            conn.value("to", "") != "s1/sec") continue;
                        found = true;
                        check(conn.contains("from_min") &&
                                  std::fabs(conn["from_min"].get<double>() - 0.0) < 1e-6,
                              "semantic remap from_min=0");
                        check(conn.contains("from_max") &&
                                  std::fabs(conn["from_max"].get<double>() - 2000.0) < 1e-6,
                              "semantic remap from_max=2000");
                        check(conn.contains("to_min") &&
                                  std::fabs(conn["to_min"].get<double>() - 0.0) < 1e-6,
                              "semantic remap to_min=0");
                        check(conn.contains("to_max") &&
                                  std::fabs(conn["to_max"].get<double>() - 2.0) < 1e-6,
                              "semantic remap to_max=2");
                        break;
                    }
                }
                check(found, "semantic remap connection present in inspect_graph");
            }
        }

        // Inferred remap must be user-overridable via set_connection_remap.
        {
            auto r = post(client, base_url, "set_connection_remap",
                R"({"from_addr":"ms1/ms","to_addr":"s1/sec","from_min":10.0,"from_max":20.0,"to_min":1.0,"to_max":2.0,"clamp":true})");
            check(r.ok, "set_connection_remap overrides inferred remap");
        }

        {
            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after remap override ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& conns = result["connections"];
                bool found = false;
                if (conns.is_array()) {
                    for (const auto& conn : conns) {
                        if (conn.value("from", "") != "ms1/ms" ||
                            conn.value("to", "") != "s1/sec") continue;
                        found = true;
                        check(conn.contains("from_min") &&
                                  std::fabs(conn["from_min"].get<double>() - 10.0) < 1e-6,
                              "override remap from_min=10");
                        check(conn.contains("from_max") &&
                                  std::fabs(conn["from_max"].get<double>() - 20.0) < 1e-6,
                              "override remap from_max=20");
                        check(conn.contains("to_min") &&
                                  std::fabs(conn["to_min"].get<double>() - 1.0) < 1e-6,
                              "override remap to_min=1");
                        check(conn.contains("to_max") &&
                                  std::fabs(conn["to_max"].get<double>() - 2.0) < 1e-6,
                              "override remap to_max=2");
                        check(conn.contains("clamp") && conn["clamp"].is_boolean() &&
                                  conn["clamp"].get<bool>(),
                              "override remap clamp=true");
                        break;
                    }
                }
                check(found, "semantic remap connection present after override");
            }
        }

        {
            auto d = post(client, base_url, "disconnect",
                R"({"from_addr":"ms1/ms","to_addr":"s1/sec"})");
            check(d.ok, "disconnect semantic remap connection ok");

            auto rm_src = post(client, base_url, "remove_node",
                R"({"node_id":"ms1"})");
            check(rm_src.ok, "remove_node ms1 ok");

            auto rm_dst = post(client, base_url, "remove_node",
                R"({"node_id":"s1"})");
            check(rm_dst.ok, "remove_node s1 ok");
        }

        // Phase 10c: semantic-default coercion only for explicit conversion pairs.
        std::fprintf(stderr, "\n--- semantic explicit coercion contract ---\n");
        {
            auto add_src = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"hz1"})");
            check(add_src.ok, "add_node TestOp hz1 ok");

            auto add_dst = post(client, base_url, "add_node",
                R"({"type":"SecDestOp","node_id":"s2"})");
            check(add_dst.ok, "add_node SecDestOp s2 ok");

            auto c = post(client, base_url, "connect",
                R"({"from_addr":"hz1/out","to_addr":"s2/sec","semantic_defaults":true})");
            check(c.ok, "connect hz1/out -> s2/sec with semantic_defaults ok");
            if (!c.j.is_null()) {
                std::string m = c.j.value("message", "");
                check(m.find("semantic default remap applied") == std::string::npos,
                      "non-contract coercion does not apply remap");
                check(c.j.contains("inferred_remap_applied") &&
                          c.j["inferred_remap_applied"].is_boolean() &&
                          !c.j["inferred_remap_applied"].get<bool>(),
                      "connect response inferred_remap_applied=false for non-contract pair");
                check(!c.j.contains("inferred_remap"),
                      "connect response omits inferred_remap object for non-contract pair");
            }
        }

        phase.store(11);
        while (phase.load() < 12) std::this_thread::sleep_for(std::chrono::milliseconds(5));

        {
            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after non-contract semantic connect ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& conns = result["connections"];
                bool found = false;
                if (conns.is_array()) {
                    for (const auto& conn : conns) {
                        if (conn.value("from", "") != "hz1/out" ||
                            conn.value("to", "") != "s2/sec") continue;
                        found = true;
                        check(!conn.contains("from_min"),
                              "non-contract coercion leaves from_min unset");
                        check(!conn.contains("from_max"),
                              "non-contract coercion leaves from_max unset");
                        check(!conn.contains("to_min"),
                              "non-contract coercion leaves to_min unset");
                        check(!conn.contains("to_max"),
                              "non-contract coercion leaves to_max unset");
                        break;
                    }
                }
                check(found, "non-contract coercion connection present in inspect_graph");
            }
        }

        {
            auto d = post(client, base_url, "disconnect",
                R"({"from_addr":"hz1/out","to_addr":"s2/sec"})");
            check(d.ok, "disconnect non-contract semantic connection ok");

            auto rm_src = post(client, base_url, "remove_node",
                R"({"node_id":"hz1"})");
            check(rm_src.ok, "remove_node hz1 ok");

            auto rm_dst = post(client, base_url, "remove_node",
                R"({"node_id":"s2"})");
            check(rm_dst.ok, "remove_node s2 ok");
        }

        // Unknown tags and untagged params must remain tolerated with no implicit remap.
        std::fprintf(stderr, "\n--- semantic unknown/untagged tolerance ---\n");
        {
            auto add_src = post(client, base_url, "add_node",
                R"({"type":"UnknownTagSourceOp","node_id":"ux1"})");
            check(add_src.ok, "add_node UnknownTagSourceOp ux1 ok");
            auto add_dst = post(client, base_url, "add_node",
                R"({"type":"UntaggedDestOp","node_id":"ud1"})");
            check(add_dst.ok, "add_node UntaggedDestOp ud1 ok");

            auto c = post(client, base_url, "connect",
                R"({"from_addr":"ux1/out","to_addr":"ud1/value","semantic_defaults":true})");
            check(c.ok, "connect unknown-tag -> untagged with semantic_defaults ok");
            if (!c.j.is_null()) {
                check(c.j.contains("inferred_remap_applied") &&
                          c.j["inferred_remap_applied"].is_boolean() &&
                          !c.j["inferred_remap_applied"].get<bool>(),
                      "unknown/untagged connection has no inferred remap");
                check(!c.j.contains("inferred_remap"),
                      "unknown/untagged connection omits inferred_remap payload");
            }
        }

        phase.store(13);
        while (phase.load() < 14) std::this_thread::sleep_for(std::chrono::milliseconds(5));

        {
            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after unknown/untagged connect ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& conns = result["connections"];
                bool found = false;
                if (conns.is_array()) {
                    for (const auto& conn : conns) {
                        if (conn.value("from", "") != "ux1/out" ||
                            conn.value("to", "") != "ud1/value") continue;
                        found = true;
                        check(!conn.contains("from_min"),
                              "unknown/untagged leaves from_min unset");
                        check(!conn.contains("from_max"),
                              "unknown/untagged leaves from_max unset");
                        check(!conn.contains("to_min"),
                              "unknown/untagged leaves to_min unset");
                        check(!conn.contains("to_max"),
                              "unknown/untagged leaves to_max unset");
                        break;
                    }
                }
                check(found, "unknown/untagged connection present in inspect_graph");
            }
        }

        {
            auto d = post(client, base_url, "disconnect",
                R"({"from_addr":"ux1/out","to_addr":"ud1/value"})");
            check(d.ok, "disconnect unknown/untagged connection ok");
            auto rm_src = post(client, base_url, "remove_node",
                R"({"node_id":"ux1"})");
            check(rm_src.ok, "remove_node ux1 ok");
            auto rm_dst = post(client, base_url, "remove_node",
                R"({"node_id":"ud1"})");
            check(rm_dst.ok, "remove_node ud1 ok");
        }

        // Phase 11: save_graph
        std::fprintf(stderr, "\n--- save_graph ---\n");
        std::string tmp_path = build_dir + "/test_cs_saved.json";
        std::string load_graph_path = build_dir + "/test_cs_loaded.json";
        {
            auto r = post(client, base_url, "save_graph",
                R"({"path":")" + tmp_path + R"("})");
            check(r.ok, "save_graph ok");
            check(std::filesystem::exists(tmp_path), "saved file exists");
        }
        {
            std::ofstream ofs(load_graph_path);
            ofs <<
                "{\n"
                "  \"nodes\": {\n"
                "    \"loaded_only\": { \"type\": \"TestOp\", \"params\": { \"scale\": 4.25 } }\n"
                "  }\n"
                "}\n";
        }

        // Phase 12: load_graph
        std::fprintf(stderr, "\n--- load_graph ---\n");
        {
            auto r = post(client, base_url, "load_graph",
                R"({"path":")" + load_graph_path + R"("})");
            check(r.ok, "load_graph ok");

            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after load_graph ok");
            if (!ig.j.is_null()) {
                check(!inspect_graph_node_by_id(ig.j, "loaded_only").is_null(),
                      "load_graph switched to requested file");
                check(inspect_graph_node_by_id(ig.j, "a").is_null(),
                      "load_graph no longer shows previous graph nodes");
            }

            // Undo history must reset on file load.
            auto u = post(client, base_url, "undo");
            check(!u.ok, "undo after load_graph reports no history");

            // Baseline should still be tracked: mutate then undo returns to loaded state.
            auto sp = post(client, base_url, "set_param",
                R"({"node_id":"loaded_only","param":"scale","value":8.0})");
            check(sp.ok, "set_param after load_graph ok");
            auto u2 = post(client, base_url, "undo");
            check(u2.ok, "undo after post-load mutation ok");
            auto gp = post(client, base_url, "get_param",
                R"({"node_id":"loaded_only","param":"scale"})");
            check(gp.ok, "get_param after post-load undo ok");
            if (!gp.j.is_null()) {
                check(gp.j.contains("value") &&
                          std::fabs(gp.j["value"].get<double>() - 4.25) < 1e-4,
                      "post-load undo restored loaded baseline value");
            }
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

        // Phase 13b: package endpoints (MCP/API matrix coverage)
        std::fprintf(stderr, "\n--- package endpoints ---\n");
        {
            auto rl = post(client, base_url, "list_packages");
            check(rl.ok, "list_packages ok");
            if (!rl.j.is_null()) {
                auto& result = rl.j["result"];
                auto& pkgs = result["packages"];
                check(pkgs.is_array(), "list_packages returns array");
                bool found = false;
                if (pkgs.is_array()) {
                    for (const auto& p : pkgs) {
                        if (p.contains("name") && p["name"].is_string() &&
                            p["name"].get<std::string>() == "catalog-test-pkg") {
                            found = true;
                            check(p.contains("source_scope") && p["source_scope"].is_string(),
                                  "list_packages exposes source_scope");
                            if (p.contains("source_scope") && p["source_scope"].is_string())
                                check(p["source_scope"].get<std::string>() == "user",
                                      "source_scope is user");
                            break;
                        }
                    }
                }
                check(found, "list_packages contains installed test package");
            }

            auto rc = post(client, base_url, "package_catalog");
            check(rc.ok, "package_catalog ok");
            if (!rc.j.is_null()) {
                auto& pkgs = rc.j["packages"];
                check(pkgs.is_array(), "package_catalog returns packages array");
                bool found = false;
                if (pkgs.is_array()) {
                    for (const auto& p : pkgs) {
                        if (p.contains("name") && p["name"].is_string() &&
                            p["name"].get<std::string>() == "catalog-test-pkg") {
                            found = true;
                            check(p.contains("installed") && p["installed"].is_boolean() &&
                                      p["installed"].get<bool>(),
                                  "package_catalog marks installed package");
                            check(p.contains("installed_version") && p["installed_version"].is_string() &&
                                      p["installed_version"].get<std::string>() == "1.0.0",
                                  "package_catalog includes installed_version");
                            break;
                        }
                    }
                }
                check(found, "package_catalog contains catalog-test-pkg");
            }

            auto ru = post(client, base_url, "check_package_updates", R"({"core_version":"0.9.0"})");
            check(ru.ok, "check_package_updates ok");
            if (!ru.j.is_null()) {
                check(ru.j.contains("updates_available") && ru.j["updates_available"].is_number_integer() &&
                          ru.j["updates_available"].get<int>() >= 1,
                      "check_package_updates reports updates_available");
                auto& pkgs = ru.j["packages"];
                check(pkgs.is_array(), "check_package_updates returns packages array");
                if (pkgs.is_array() && pkgs.size() > 0) {
                    auto& p0 = pkgs[0];
                    check(p0.contains("classification") && p0["classification"].is_string(),
                          "update entry includes classification");
                    check(p0.contains("update_available") && p0["update_available"].is_boolean() &&
                              p0["update_available"].get<bool>(),
                          "update entry marks update_available=true");
                    check(p0.contains("compatible") && p0["compatible"].is_boolean(),
                          "update entry includes compatible flag");
                }
            }

            auto ri = post(client, base_url, "check_package_updates", R"({"core_version":"9.0.0"})");
            check(ri.ok, "check_package_updates incompatible-core ok");
            if (!ri.j.is_null()) {
                check(ri.j.contains("incompatible_updates") &&
                          ri.j["incompatible_updates"].is_number_integer() &&
                          ri.j["incompatible_updates"].get<int>() >= 1,
                      "check_package_updates reports incompatible_updates");
                auto& pkgs = ri.j["packages"];
                if (pkgs.is_array() && pkgs.size() > 0) {
                    auto& p0 = pkgs[0];
                    check(p0.contains("classification") && p0["classification"].is_string() &&
                              p0["classification"].get<std::string>() == "incompatible_update",
                          "incompatible update classification is exposed");
                    check(p0.contains("compatible") && p0["compatible"].is_boolean() &&
                              !p0["compatible"].get<bool>(),
                          "incompatible update marks compatible=false");
                }
            }
        }

        // Phase 13c: scaffold_operator destination e2e
        std::fprintf(stderr, "\n--- scaffold_operator destination ---\n");
        {
            auto rp = post(client, base_url, "scaffold_operator",
                R"({"name":"mcp_pkg_e2e","kind":"control","destination":"package:vivid-scaffold-e2e"})");
            check(rp.ok, "scaffold_operator project destination ok");
            if (!rp.ok && !rp.j.is_null()) {
                if (rp.j.contains("error") && rp.j["error"].is_string()) {
                    std::fprintf(stderr, "  INFO: scaffold_operator project error: %s\n",
                                 rp.j["error"].get<std::string>().c_str());
                }
            }
            if (!rp.j.is_null()) {
                auto& result = rp.j["result"];
                check(result.contains("destination_is_package") &&
                          result["destination_is_package"].is_boolean() &&
                          result["destination_is_package"].get<bool>(),
                      "scaffold_operator project resolved to package");
                check(result.contains("destination_package") &&
                          result["destination_package"].is_string() &&
                          result["destination_package"].get<std::string>() == "vivid-scaffold-e2e",
                      "scaffold_operator reports destination package");
                check(result.contains("destination_root") && result["destination_root"].is_string(),
                      "scaffold_operator reports destination root");
            }
            check(std::filesystem::exists(local_pkg_src + "/src/mcp_pkg_e2e.cpp"),
                  "MCP scaffold wrote operator source into linked package src/");
            {
                std::ifstream ifs(local_pkg_src + "/CMakeLists.txt");
                std::string cmake((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                check(cmake.find("mcp_pkg_e2e") != std::string::npos,
                      "MCP scaffold patched package CMake ops list");
            }

            // Remove linked project package so auto destination falls back to core with warning.
            std::error_code ec;
            std::filesystem::remove(local_pkg_link, ec);
            check(!ec, "removed linked project package for fallback test");

            auto rc = post(client, base_url, "scaffold_operator",
                R"({"name":"mcp_core_fallback","kind":"control","destination":"auto"})");
            check(rc.ok, "scaffold_operator auto fallback ok");
            if (!rc.ok && !rc.j.is_null()) {
                if (rc.j.contains("error") && rc.j["error"].is_string()) {
                    std::fprintf(stderr, "  INFO: scaffold_operator auto fallback error: %s\n",
                                 rc.j["error"].get<std::string>().c_str());
                }
            }
            if (!rc.j.is_null()) {
                auto& result = rc.j["result"];
                check(result.contains("destination_is_package") &&
                          result["destination_is_package"].is_boolean() &&
                          !result["destination_is_package"].get<bool>(),
                      "auto fallback resolved to core destination");
                check(result.contains("destination_warning") &&
                          result["destination_warning"].is_string() &&
                          result["destination_warning"].get<std::string>().size() > 0,
                      "auto fallback returns destination warning");
                check(result.contains("destination_root") &&
                          result["destination_root"].is_string() &&
                          result["destination_root"].get<std::string>() == scaffold_core_src,
                      "auto fallback destination root is configured core src");
            }
            check(std::filesystem::exists(scaffold_core_src +
                                         "/operators/control/mcp_core_fallback/mcp_core_fallback.cpp"),
                  "fallback-to-core scaffold wrote core operator source");
        }

        // Cleanup temp file
        std::filesystem::remove(tmp_path);

        // Phase 14: Variations
        std::fprintf(stderr, "\n--- variations ---\n");
        {
            auto r1 = post(client, base_url, "save_variation", R"({"name":"V1"})");
            check(r1.ok, "save_variation V1 ok");

            auto r2 = post(client, base_url, "list_variations");
            check(r2.ok, "list_variations ok");
            if (!r2.j.is_null()) {
                std::string msg = r2.j.value("message", "");
                check(msg.find("V1") != std::string::npos,
                      "list contains V1");
            }

            auto r3 = post(client, base_url, "save_variation", R"({"name":"V2"})");
            check(r3.ok, "save_variation V2 ok");

            auto r4 = post(client, base_url, "recall_variation", R"({"name":"V1"})");
            check(r4.ok, "recall_variation V1 ok");

            auto r5 = post(client, base_url, "update_variation", R"({"name":"V1"})");
            check(r5.ok, "update_variation V1 ok");

            auto r6 = post(client, base_url, "rename_variation",
                R"({"old_name":"V1","new_name":"Intro"})");
            check(r6.ok, "rename_variation V1 -> Intro ok");

            auto r7 = post(client, base_url, "queue_variation",
                R"({"name":"V2","quantize":"instant"})");
            check(r7.ok, "queue_variation V2 instant ok");

            auto r8 = post(client, base_url, "set_quantize_clock", R"({"node_id":"a"})");
            check(r8.ok, "set_quantize_clock a ok");

            auto r9 = post(client, base_url, "remove_variation", R"({"name":"V2"})");
            check(r9.ok, "remove_variation V2 ok");

            auto r10 = post(client, base_url, "remove_variation", R"({"name":"bogus"})");
            check(!r10.ok, "remove_variation bogus fails");

            // duplicate_variation
            auto rd1 = post(client, base_url, "save_variation", R"({"name":"D1"})");
            check(rd1.ok, "save_variation D1 ok");

            auto rd2 = post(client, base_url, "duplicate_variation",
                R"({"name":"D1","new_name":"D1_copy"})");
            check(rd2.ok, "duplicate_variation D1 -> D1_copy ok");

            auto rd3 = post(client, base_url, "duplicate_variation",
                R"({"name":"D1","new_name":"D1_copy"})");
            check(!rd3.ok, "duplicate_variation name conflict fails");

            auto rd4 = post(client, base_url, "duplicate_variation",
                R"({"name":"nope","new_name":"X"})");
            check(!rd4.ok, "duplicate_variation not found fails");

            // move_variation
            auto rm1 = post(client, base_url, "move_variation",
                R"({"name":"D1_copy","to_index":0})");
            check(rm1.ok, "move_variation D1_copy to 0 ok");

            auto rm2 = post(client, base_url, "move_variation",
                R"({"name":"D1","to_index":99})");
            check(!rm2.ok, "move_variation out of range fails");

            auto rm3 = post(client, base_url, "move_variation",
                R"({"name":"nope","to_index":0})");
            check(!rm3.ok, "move_variation not found fails");

            // Cleanup
            post(client, base_url, "remove_variation", R"({"name":"D1"})");
            post(client, base_url, "remove_variation", R"({"name":"D1_copy"})");
        }

        // --- input validation: recording path traversal ---
        std::fprintf(stderr, "\n--- recording path traversal rejection ---\n");
        {
            // No capture coordinator in this test harness, so start_recording routes
            // through the path-validation guard before reaching the coordinator check.
            // A traversal path must be rejected with ok:false before coordinator dispatch.
            auto r1 = post(client, base_url, "start_recording",
                R"({"path":"../../../tmp/evil.mov"})");
            check(!r1.ok, "start_recording traversal path rejected");

            auto r2 = post(client, base_url, "start_recording",
                R"({"path":"/tmp/ok.mov"})");
            // No coordinator attached, so this either times out or errors — but must NOT
            // be the traversal rejection (i.e. it should not fail with the traversal guard).
            // We verify the path guard passed by checking the response is not the
            // hard-coded "invalid recording path" error.
            if (!r2.j.is_null()) {
                std::string err_str = r2.j.value("error", "");
                check(err_str != "invalid recording path", "valid path passes traversal guard");
            }
        }

        // --- add_node: old "id" field rejected, "node_id" accepted ---
        std::fprintf(stderr, "\n--- add_node field name validation ---\n");
        {
            auto r_bad = post(client, base_url, "add_node",
                R"({"type":"TestOp","id":"should_fail"})");
            check(!r_bad.ok, "add_node with old 'id' field rejected");

            auto r_good = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"validation_test_node"})");
            check(r_good.ok, "add_node with 'node_id' field accepted");
        }

        // ---------------------------------------------------------------
        // Sticky note CRUD via control server
        // ---------------------------------------------------------------
        std::fprintf(stderr, "\n--- sticky note CRUD ---\n");
        {
            auto r = post(client, base_url, "add_sticky_note",
                R"({"text":"Hello world","x":100,"y":200,"width":250,"height":150,"color":1})");
            check(r.ok, "add_sticky_note ok");
            std::string note_id;
            if (!r.j.is_null()) {
                if (r.j.contains("result") && r.j["result"].contains("id") &&
                    r.j["result"]["id"].is_string())
                    note_id = r.j["result"]["id"].get<std::string>();
                check(!note_id.empty(), "add_sticky_note returns id");
            }

            auto r2 = post(client, base_url, "add_sticky_note",
                R"({"id":"custom_note","text":"Second","x":300,"y":400})");
            check(r2.ok, "add_sticky_note with custom id ok");

            auto list = post(client, base_url, "list_sticky_notes");
            check(list.ok, "list_sticky_notes ok");
            if (!list.j.is_null()) {
                auto& result_v = list.j["result"];
                check(result_v.is_array() && result_v.size() == 2,
                      "list_sticky_notes returns 2 notes");
            }

            auto upd = post(client, base_url, "update_sticky_note",
                R"({"id":"custom_note","text":"Updated text","color":3})");
            check(upd.ok, "update_sticky_note ok");

            auto list2 = post(client, base_url, "list_sticky_notes");
            if (!list2.j.is_null()) {
                auto& result_v = list2.j["result"];
                if (result_v.is_array()) {
                    for (const auto& item : result_v) {
                        if (item.value("id", "") == "custom_note") {
                            check(item.contains("text") &&
                                      item["text"].get<std::string>() == "Updated text",
                                  "sticky note text updated");
                            check(item.contains("color") &&
                                      item["color"].get<int>() == 3,
                                  "sticky note color updated");
                        }
                    }
                }
            }

            auto rem = post(client, base_url, "remove_sticky_note",
                R"({"id":"custom_note"})");
            check(rem.ok, "remove_sticky_note ok");

            auto list3 = post(client, base_url, "list_sticky_notes");
            if (!list3.j.is_null()) {
                auto& result_v = list3.j["result"];
                check(result_v.is_array() && result_v.size() == 1,
                      "1 sticky note after remove");
            }

            auto rem_bad = post(client, base_url, "remove_sticky_note",
                R"({"id":"nonexistent"})");
            check(!rem_bad.ok, "remove_sticky_note nonexistent fails");

            // Clean up
            auto rem2 = post(client, base_url, "remove_sticky_note",
                std::string("{\"id\":\"") + note_id + "\"}");
            check(rem2.ok, "remove remaining sticky note ok");
        }

        // Final pass: package mutation safety with a live linked package node.
        std::fprintf(stderr, "\n--- live package mutation safety ---\n");
        {
            auto link = post(client, base_url, "link_package",
                std::string("{\"path\":\"") + live_pkg_src + "\"}");
            check(link.ok, "link_package vivid-live-pkg ok");
            if (!link.j.is_null()) {
                auto& result = link.j["result"];
                check(result.contains("name") && result["name"].is_string() &&
                          result["name"].get<std::string>() == "vivid-live-pkg",
                      "link_package returns vivid-live-pkg");
                check(result.contains("linked") && result["linked"].is_boolean() &&
                          result["linked"].get<bool>(),
                      "link_package marks package as linked");
            }

            auto pkg_docs = post(client, base_url, "package_operator_docs",
                R"({"name":"vivid-live-pkg"})");
            check(pkg_docs.ok, "package_operator_docs vivid-live-pkg ok");
            if (!pkg_docs.j.is_null()) {
                auto& result = pkg_docs.j["result"];
                check(result.contains("operators") && result["operators"].is_array() &&
                          result["operators"].size() == 1,
                      "package_operator_docs returns linked package operator");
                if (result.contains("operators") && result["operators"].is_array() &&
                    result["operators"].size() == 1) {
                    auto& op = result["operators"][0];
                    check(op.contains("name") && op["name"].is_string() &&
                              op["name"].get<std::string>() == "PkgLiveOp",
                          "package_operator_docs returns operator name");
                    check(op.contains("package") && op["package"].is_string() &&
                              op["package"].get<std::string>() == "vivid-live-pkg",
                          "package_operator_docs includes package name");
                    check(op.contains("has_docs") && op["has_docs"].is_boolean() &&
                              op["has_docs"].get<bool>(),
                          "package_operator_docs marks has_docs true");
                    check(op.contains("brief") && op["brief"].is_string() &&
                              op["brief"].get<std::string>() == "Linked package fixture operator.",
                          "package_operator_docs merges brief");
                    check(op.contains("outputs") && op["outputs"].is_array() &&
                              op["outputs"].size() == 1,
                          "package_operator_docs returns outputs");
                    if (op.contains("outputs") && op["outputs"].is_array() &&
                        op["outputs"].size() == 1) {
                        auto& out0 = op["outputs"][0];
                        check(out0.contains("doc") && out0["doc"].is_string() &&
                                  out0["doc"].get<std::string>() == "Live package scalar output.",
                              "package_operator_docs merges output doc");
                    }
                }
            }

            auto add_live = post(client, base_url, "add_node",
                R"({"type":"PkgLiveOp","node_id":"pkg_live"})");
            check(add_live.ok, "add_node pkg_live ok");
            phase.store(15);
            while (phase.load() < 16) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto intro_v1 = post(client, base_url, "introspect_nodes");
            check(intro_v1.ok, "introspect_nodes after add live package ok");
            if (!intro_v1.j.is_null()) {
                auto node = introspect_node_by_id(intro_v1.j, "pkg_live");
                check(!node.is_null(), "pkg_live appears in introspect_nodes");
                if (!node.is_null()) {
                    auto& health = node["health"];
                    auto& node_outputs = node["outputs"];
                    check(health.contains("missing_operator") &&
                              health["missing_operator"].is_boolean() &&
                              !health["missing_operator"].get<bool>(),
                          "pkg_live is resolved before rebuild");
                    if (node_outputs.is_array() && node_outputs.size() > 0) {
                        auto& out0 = node_outputs[0];
                        check(out0.contains("scalar") &&
                                  std::fabs(out0["scalar"].get<double>() - 3.0) < 1e-4,
                              "pkg_live output starts at 3");
                    } else {
                        check(false, "pkg_live exposes output after initial link");
                    }
                }
            }

            write_live_pkg_source(7.0f);
            auto rebuild = post(client, base_url, "rebuild_package",
                R"({"name":"vivid-live-pkg"})");
            check(rebuild.ok, "rebuild_package vivid-live-pkg ok");
            phase.store(17);
            while (phase.load() < 18) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto intro_v2 = post(client, base_url, "introspect_nodes");
            check(intro_v2.ok, "introspect_nodes after rebuild ok");
            if (!intro_v2.j.is_null()) {
                auto node = introspect_node_by_id(intro_v2.j, "pkg_live");
                check(!node.is_null(), "pkg_live remains after rebuild");
                if (!node.is_null()) {
                    auto& health = node["health"];
                    auto& node_outputs = node["outputs"];
                    check(health.contains("missing_operator") &&
                              health["missing_operator"].is_boolean() &&
                              !health["missing_operator"].get<bool>(),
                          "pkg_live stays resolved after rebuild");
                    if (node_outputs.is_array() && node_outputs.size() > 0) {
                        auto& out0 = node_outputs[0];
                        check(out0.contains("scalar") &&
                                  std::fabs(out0["scalar"].get<double>() - 7.0) < 1e-4,
                              "pkg_live output refreshes to rebuilt value");
                    } else {
                        check(false, "pkg_live exposes output after rebuild");
                    }
                }
            }

            auto unlink = post(client, base_url, "unlink_package",
                R"({"name":"vivid-live-pkg"})");
            check(unlink.ok, "unlink_package vivid-live-pkg ok");
            phase.store(19);
            while (phase.load() < 20) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto intro_missing = post(client, base_url, "introspect_nodes");
            check(intro_missing.ok, "introspect_nodes after unlink ok");
            if (!intro_missing.j.is_null()) {
                auto node = introspect_node_by_id(intro_missing.j, "pkg_live");
                check(!node.is_null(), "pkg_live remains in graph after unlink");
                if (!node.is_null()) {
                    auto& health = node["health"];
                    check(health.contains("missing_operator") &&
                              health["missing_operator"].is_boolean() &&
                              health["missing_operator"].get<bool>(),
                          "pkg_live becomes missing operator after unlink");
                }
            }

            auto list_missing = post(client, base_url, "list_nodes");
            check(list_missing.ok, "list_nodes after unlink ok");

            auto inspect_missing = post(client, base_url, "inspect_graph");
            check(inspect_missing.ok, "inspect_graph after unlink ok");
            if (!inspect_missing.j.is_null()) {
                auto node = inspect_graph_node_by_id(inspect_missing.j, "pkg_live");
                check(!node.is_null(), "pkg_live remains inspectable after unlink");
            }

            auto rl2 = post(client, base_url, "list_packages");
            check(rl2.ok, "list_packages after unlink ok");
            if (!rl2.j.is_null()) {
                auto& result = rl2.j["result"];
                auto& pkgs = result["packages"];
                bool found = false;
                if (pkgs.is_array()) {
                    for (const auto& p : pkgs) {
                        if (p.contains("name") && p["name"].is_string() &&
                            p["name"].get<std::string>() == "vivid-live-pkg") {
                            found = true;
                            break;
                        }
                    }
                }
                check(!found, "vivid-live-pkg removed from package list after unlink");
            }
        }

        // =============================================================
        // Re-add a test node for the remaining endpoint tests
        // (the unlink phase reloaded the graph, so node "a" is gone)
        // =============================================================
        {
            auto re_add = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"test_ep"})");
            check(re_add.ok, "re-add TestOp node for endpoint tests");
            phase.store(21);
            while (phase.load() < 22) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // =============================================================
        // Solo set/get
        // =============================================================
        {
            std::fprintf(stderr, "\n  solo set/get\n");

            auto get1 = post(client, base_url, "get_solo");
            check(get1.ok, "get_solo ok");
            if (!get1.j.is_null()) {
                check(get1.j.contains("active") && !get1.j["active"].get<bool>(),
                      "solo not active initially");
            }

            auto set1 = post(client, base_url, "set_solo",
                R"({"node_id":"test_ep"})");
            check(set1.ok, "set_solo on node test_ep ok");

            auto get2 = post(client, base_url, "get_solo");
            check(get2.ok, "get_solo after set ok");
            if (!get2.j.is_null()) {
                check(get2.j["active"].get<bool>(), "solo active after set");
                check(get2.j["node_id"].get<std::string>() == "test_ep", "solo node_id is test_ep");
            }

            // Clear solo
            auto clear = post(client, base_url, "set_solo",
                R"({"node_id":""})");
            check(clear.ok, "clear solo ok");

            auto get3 = post(client, base_url, "get_solo");
            check(get3.ok, "get_solo after clear ok");
            if (!get3.j.is_null()) {
                check(!get3.j["active"].get<bool>(), "solo cleared");
            }
        }

        // =============================================================
        // Param lock set/get
        // =============================================================
        {
            std::fprintf(stderr, "\n  param lock set/get\n");

            auto set_lock = post(client, base_url, "set_param_lock",
                R"({"node_id":"test_ep","param":"scale","flags":1})");
            check(set_lock.ok, "set_param_lock ok");

            auto get_lock = post(client, base_url, "get_param_lock",
                R"({"node_id":"test_ep","param":"scale"})");
            check(get_lock.ok, "get_param_lock ok");
            if (!get_lock.j.is_null() && get_lock.j.contains("result")) {
                auto& res = get_lock.j["result"];
                check(res.contains("flags") && res["flags"].get<int>() == 1,
                      "param lock flags == 1");
            }

            // Clear lock
            auto clear_lock = post(client, base_url, "set_param_lock",
                R"({"node_id":"test_ep","param":"scale","flags":0})");
            check(clear_lock.ok, "clear param lock ok");

            auto get_lock2 = post(client, base_url, "get_param_lock",
                R"({"node_id":"test_ep","param":"scale"})");
            check(get_lock2.ok, "get_param_lock after clear ok");
            if (!get_lock2.j.is_null() && get_lock2.j.contains("result")) {
                auto& res = get_lock2.j["result"];
                check(res.contains("flags") && res["flags"].get<int>() == 0,
                      "param lock flags cleared to 0");
            }
        }

        // =============================================================
        // Preset lifecycle: save, list, recall, rename, remove
        // =============================================================
        {
            std::fprintf(stderr, "\n  preset lifecycle\n");

            // Set param to known value before saving
            post(client, base_url, "set_param",
                R"({"node_id":"test_ep","param":"scale","value":7.5})");

            auto save = post(client, base_url, "save_preset",
                R"({"node_id":"test_ep","name":"test_preset"})");
            check(save.ok, "save_preset ok");

            auto list1 = post(client, base_url, "list_presets",
                R"({"node_id":"test_ep"})");
            check(list1.ok, "list_presets ok");
            if (!list1.j.is_null() && list1.j.contains("result")) {
                auto& res = list1.j["result"];
                bool found = false;
                if (res.contains("presets") && res["presets"].is_array()) {
                    for (const auto& p : res["presets"]) {
                        if (p.is_string() && p.get<std::string>() == "test_preset")
                            found = true;
                    }
                }
                check(found, "test_preset in preset list");
            }

            // Change param, then recall preset
            post(client, base_url, "set_param",
                R"({"node_id":"test_ep","param":"scale","value":0.0})");

            auto recall = post(client, base_url, "recall_preset",
                R"({"node_id":"test_ep","name":"test_preset"})");
            check(recall.ok, "recall_preset ok");

            // Verify param restored
            auto get_p = post(client, base_url, "get_param",
                R"({"node_id":"test_ep","param":"scale"})");
            check(get_p.ok, "get_param after recall ok");
            if (!get_p.j.is_null() && get_p.j.contains("result")) {
                float val = get_p.j["result"]["value"].get<float>();
                check(std::fabs(val - 7.5f) < 0.01f,
                      "param restored to 7.5 after recall");
            }

            // Rename preset
            auto rename = post(client, base_url, "rename_preset",
                R"({"node_id":"test_ep","old_name":"test_preset","new_name":"renamed_preset"})");
            check(rename.ok, "rename_preset ok");

            auto list2 = post(client, base_url, "list_presets",
                R"({"node_id":"test_ep"})");
            if (!list2.j.is_null() && list2.j.contains("result")) {
                auto& res = list2.j["result"];
                bool found_old = false, found_new = false;
                if (res.contains("presets") && res["presets"].is_array()) {
                    for (const auto& p : res["presets"]) {
                        if (p.is_string()) {
                            if (p.get<std::string>() == "test_preset") found_old = true;
                            if (p.get<std::string>() == "renamed_preset") found_new = true;
                        }
                    }
                }
                check(!found_old, "old preset name gone");
                check(found_new, "new preset name present");
            }

            // Update preset
            post(client, base_url, "set_param",
                R"({"node_id":"test_ep","param":"scale","value":9.9})");
            auto update = post(client, base_url, "update_preset",
                R"({"node_id":"test_ep","name":"renamed_preset"})");
            check(update.ok, "update_preset ok");

            // Remove preset
            auto remove = post(client, base_url, "remove_preset",
                R"({"node_id":"test_ep","name":"renamed_preset"})");
            check(remove.ok, "remove_preset ok");

            auto list3 = post(client, base_url, "list_presets",
                R"({"node_id":"test_ep"})");
            if (!list3.j.is_null() && list3.j.contains("result")) {
                auto& res = list3.j["result"];
                bool found = false;
                if (res.contains("presets") && res["presets"].is_array()) {
                    for (const auto& p : res["presets"]) {
                        if (p.is_string() && p.get<std::string>() == "renamed_preset")
                            found = true;
                    }
                }
                check(!found, "preset removed from list");
            }
        }

        // =============================================================
        // Factory presets
        // =============================================================
        {
            std::fprintf(stderr, "\n  factory presets\n");

            auto fp = post(client, base_url, "list_factory_presets",
                R"({"node_id":"test_ep"})");
            check(fp.ok, "list_factory_presets ok");
            if (!fp.j.is_null() && fp.j.contains("result")) {
                check(fp.j["result"].contains("presets"),
                      "factory presets response has presets field");
            }
        }

        // =============================================================
        // Set node layout
        // =============================================================
        {
            std::fprintf(stderr, "\n  set_node_layout\n");

            auto layout = post(client, base_url, "set_node_layout",
                R"({"node_id":"test_ep","x":100,"y":200})");
            check(layout.ok, "set_node_layout ok");
        }

        // =============================================================
        // Set resolution
        // =============================================================
        {
            std::fprintf(stderr, "\n  set_resolution\n");

            auto res = post(client, base_url, "set_resolution",
                R"({"node_id":"test_ep","width":1920,"height":1080})");
            check(res.ok, "set_resolution ok");
        }

        // =============================================================
        // set_analysis
        // =============================================================
        {
            std::fprintf(stderr, "\n  set_analysis\n");

            auto r1 = post(client, base_url, "set_analysis", R"({"enabled":true})");
            check(r1.ok, "set_analysis enabled=true ok");
            if (!r1.j.is_null()) {
                check(r1.j.value("message", "").find("enabled") != std::string::npos,
                      "set_analysis response mentions enabled");
            }

            auto r2 = post(client, base_url, "set_analysis", R"({"enabled":false})");
            check(r2.ok, "set_analysis enabled=false ok");

            auto r3 = post(client, base_url, "set_analysis", R"({})");
            check(!r3.ok, "set_analysis missing enabled fails");

            auto r4 = post(client, base_url, "set_analysis", R"({"enabled":"yes"})");
            check(!r4.ok, "set_analysis non-boolean enabled fails");
        }

        // =============================================================
        // set_connection_remap
        // =============================================================
        {
            std::fprintf(stderr, "\n  set_connection_remap\n");

            // Add a second node and connect to test_ep so we have a connection to remap
            auto add_remap_dst = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"remap_dst"})");
            check(add_remap_dst.ok, "add_node remap_dst ok");

            auto conn = post(client, base_url, "connect",
                R"({"from_addr":"test_ep/out","to_addr":"remap_dst/scale"})");
            check(conn.ok, "connect test_ep/out -> remap_dst/scale ok");

            auto remap = post(client, base_url, "set_connection_remap",
                R"({"from_addr":"test_ep/out","to_addr":"remap_dst/scale","from_min":0.0,"from_max":1.0,"to_min":0.0,"to_max":10.0,"clamp":true})");
            check(remap.ok, "set_connection_remap ok");

            // Error: non-existent connection
            auto r2 = post(client, base_url, "set_connection_remap",
                R"({"from_addr":"nonexist/out","to_addr":"nonexist/in","from_min":0,"from_max":1,"to_min":0,"to_max":1})");
            check(!r2.ok, "set_connection_remap on nonexistent connection fails");

            // Clean up
            post(client, base_url, "disconnect",
                R"({"from_addr":"test_ep/out","to_addr":"remap_dst/scale"})");
            post(client, base_url, "remove_node", R"({"node_id":"remap_dst"})");
        }

        // =============================================================
        // MIDI mapping lifecycle
        // =============================================================
        {
            std::fprintf(stderr, "\n  midi mapping lifecycle\n");

            auto add = post(client, base_url, "add_midi_mapping",
                R"({"node_id":"test_ep","param":"scale","cc":1,"channel":1,"range_min":0.0,"range_max":1.0})");
            check(add.ok, "add_midi_mapping ok");

            auto update = post(client, base_url, "update_midi_mapping",
                R"({"node_id":"test_ep","param":"scale","range_min":0.2,"range_max":0.8})");
            check(update.ok, "update_midi_mapping ok");

            auto remove = post(client, base_url, "remove_midi_mapping",
                R"({"node_id":"test_ep","param":"scale"})");
            check(remove.ok, "remove_midi_mapping ok");

            // Remove again should fail
            auto remove2 = post(client, base_url, "remove_midi_mapping",
                R"({"node_id":"test_ep","param":"scale"})");
            check(!remove2.ok, "remove_midi_mapping already removed fails");

            // Update non-existent should fail
            auto update2 = post(client, base_url, "update_midi_mapping",
                R"({"node_id":"test_ep","param":"scale","range_min":0.0,"range_max":1.0})");
            check(!update2.ok, "update_midi_mapping non-existent fails");
        }

        // =============================================================
        // State machine presets
        // =============================================================
        {
            std::fprintf(stderr, "\n  state machine presets\n");

            auto set1 = post(client, base_url, "set_state_preset",
                R"({"sm_node":"test_ep","state_idx":0,"target_node":"b","name":"sp1"})");
            check(set1.ok, "set_state_preset ok");

            auto inspect = post(client, base_url, "inspect_state_presets",
                R"({"sm_node":"test_ep"})");
            check(inspect.ok, "inspect_state_presets ok");

            // Invalid state_idx (must be 0-7)
            auto set_bad = post(client, base_url, "set_state_preset",
                R"({"sm_node":"test_ep","state_idx":8,"target_node":"b","name":"sp2"})");
            check(!set_bad.ok, "set_state_preset state_idx=8 fails");

            auto rem = post(client, base_url, "remove_state_preset",
                R"({"sm_node":"test_ep","state_idx":0,"target_node":"b"})");
            check(rem.ok, "remove_state_preset ok");

            // Set again and clear
            auto set2 = post(client, base_url, "set_state_preset",
                R"({"sm_node":"test_ep","state_idx":1,"target_node":"b","name":"sp3"})");
            check(set2.ok, "set_state_preset state_idx=1 ok");

            auto clear = post(client, base_url, "clear_state_presets",
                R"({"sm_node":"test_ep"})");
            check(clear.ok, "clear_state_presets ok");

            // Inspect after clear should show nothing
            auto inspect2 = post(client, base_url, "inspect_state_presets",
                R"({"sm_node":"test_ep"})");
            check(inspect2.ok, "inspect_state_presets after clear ok");
        }

        // =============================================================
        // get_discovery_report
        // =============================================================
        {
            std::fprintf(stderr, "\n  get_discovery_report\n");

            auto r = post(client, base_url, "get_discovery_report");
            check(r.ok, "get_discovery_report ok");
            if (!r.j.is_null()) {
                check(r.j.contains("result") && r.j["result"].is_object(),
                      "get_discovery_report has result");
                auto& result = r.j["result"];
                check(result.contains("scopes") && result["scopes"].is_array(),
                      "discovery_report has scopes array");
                check(result.contains("loaded") && result["loaded"].is_array(),
                      "discovery_report has loaded array");
                check(result.contains("skipped") && result["skipped"].is_array(),
                      "discovery_report has skipped array");
            }
        }

        // =============================================================
        // new_graph — MUST BE LAST (destroys graph state)
        // =============================================================
        {
            std::fprintf(stderr, "\n  new_graph\n");

            // Write minimal default_graph.json so new_graph has a template
            std::string config_dir = test_home + "/Library/Application Support/Vivid";
            std::filesystem::create_directories(config_dir);
            {
                std::ofstream ofs(config_dir + "/default_graph.json");
                ofs << R"({"nodes":{"default_a":{"type":"TestOp","params":{"scale":1.0}}},"connections":[]})";
            }

            auto r = post(client, base_url, "new_graph");
            check(r.ok, "new_graph ok");

            // Verify graph was reset
            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after new_graph ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& nodes = result["nodes"];
                check(nodes.is_array() && nodes.size() == 1,
                      "new_graph produced 1-node default graph");
                if (nodes.is_array() && nodes.size() == 1) {
                    check(nodes[0]["id"].get<std::string>() == "default_a",
                          "new_graph default node is default_a");
                }
            }
        }

        done.store(true);
    });

    // --- Main thread: pump loop ---
    while (!done.load()) {
        server.process_requests(api, graph, runtime, registry,
                                has_gpu_ops, has_audio);

        // Apply pending topology when client signals
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
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Drain any final requests
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
