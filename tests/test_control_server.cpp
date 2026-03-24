#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/runtime_api.h"
#include "runtime/control_server.h"
#include "runtime/package_compiler.h"
#include "runtime/package_manager.h"
#include "runtime/package_catalog.h"
#include "runtime/settings.h"
#include "runtime/platform.h"
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
    std::filesystem::copy_file(build_dir + "/test_op_with_roles.dylib",
        staging + "/test_op_with_roles.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/envelope.dylib",
        staging + "/envelope.dylib",
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

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");

    vivid::AudioEngine audio_engine;
    bool has_gpu_ops = false;
    bool has_audio = false;

    vivid::RuntimeAPI api(graph, scheduler, audio_engine, registry);

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
               "struct PkgLiveOp : vivid::ControlOperatorBase {\n"
               "    static constexpr const char* kName = \"PkgLiveOp\";\n"
               "    static constexpr bool kTimeDependent = false;\n"
               "    void collect_params(std::vector<vivid::ParamBase*>&) override {}\n"
               "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n"
               "        out.push_back({\"out\", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});\n"
               "    }\n"
               "    void process(const VividProcessContext* ctx) override {\n"
            << "        ctx->output_values[0] = " << std::to_string(output_value) << "f;\n"
               "    }\n"
               "};\n\n"
               "VIVID_REGISTER(PkgLiveOp)\n";
    };
    write_live_pkg_source(3.0f);

    // Tick once so nodes have output values
    scheduler.tick(0.0, 0.016, 0);

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
                check(first_node.contains("domain") && first_node["domain"].is_string(), "introspection node has domain");
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
                check(first_node.contains("domain_metrics") && first_node["domain_metrics"].is_object(),
                      "introspection node has domain_metrics object");
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
                    auto& params = t0["params"];
                    check(params.is_array() && params.size() > 0, "TestOp has params");
                    if (params.is_array() && params.size() > 0) {
                        auto& p0 = params[0];
                        check(p0.contains("semantic_tag") && p0["semantic_tag"].is_string() &&
                                  p0["semantic_tag"].get<std::string>() == "frequency_hz",
                              "list_types param exposes semantic_tag");
                        check(p0.contains("semantic_shape") && p0["semantic_shape"].is_string() &&
                                  p0["semantic_shape"].get<std::string>() == "scalar",
                              "list_types param exposes semantic_shape");
                        check(p0.contains("semantic_unit") && p0["semantic_unit"].is_string() &&
                                  p0["semantic_unit"].get<std::string>() == "Hz",
                              "list_types param exposes semantic_unit");
                        check(p0.contains("semantic_intent") && p0["semantic_intent"].is_string() &&
                                  p0["semantic_intent"].get<std::string>() == "test_scale",
                              "list_types param exposes semantic_intent");
                    }
                    auto& outputs = t0["outputs"];
                    check(outputs.is_array() && outputs.size() > 0, "TestOp has ports");

                    auto check_first_param_tag = [&](const json& type_obj, const char* expected_tag,
                                                     const char* label) {
                        check(!type_obj.is_null(), label);
                        const auto& params_obj = type_obj.contains("params") ? type_obj["params"] : json();
                        json p0_val;
                        if (params_obj.is_array() && params_obj.size() > 0)
                            p0_val = params_obj[0];
                        std::string tag_label = std::string(label) + " tag";
                        check(!p0_val.is_null() && p0_val.contains("semantic_tag") &&
                                  p0_val["semantic_tag"].is_string() &&
                                  p0_val["semantic_tag"].get<std::string>() == expected_tag,
                              tag_label.c_str());
                    };
                    check_first_param_tag(ms_type, "time_milliseconds",
                                          "MsSourceOp semantic_tag is time_milliseconds");
                    check_first_param_tag(sec_type, "time_seconds",
                                          "SecDestOp semantic_tag is time_seconds");
                    check_first_param_tag(unknown_type, "x_test_unknown_scalar",
                                          "UnknownTagSourceOp semantic_tag preserves extension tag");
                    check(!untagged_type.is_null(), "contains UntaggedDestOp");
                    if (!untagged_type.is_null()) {
                        const auto& params_obj = untagged_type["params"];
                        json p0_val;
                        if (params_obj.is_array() && params_obj.size() > 0)
                            p0_val = params_obj[0];
                        check(p0_val.is_null() || !p0_val.contains("semantic_tag"),
                              "UntaggedDestOp param omits semantic_tag");
                    }

                    check(!custom_type.is_null(), "contains ExportCustomPortOp");
                    if (!custom_type.is_null()) {
                        const auto& outputs_obj = custom_type["outputs"];
                        json p0_val;
                        if (outputs_obj.is_array() && outputs_obj.size() > 0)
                            p0_val = outputs_obj[0];
                        check(!p0_val.is_null() && p0_val.contains("type") &&
                                  p0_val["type"].is_string() &&
                                  p0_val["type"].get<std::string>() == "custom",
                              "list_types custom port exposes custom type kind");
                        check(!p0_val.is_null() && p0_val.contains("transport") &&
                                  p0_val["transport"].is_string() &&
                                  p0_val["transport"].get<std::string>() == "custom_ref",
                              "list_types custom port exposes transport");
                        check(!p0_val.is_null() && p0_val.contains("type_name") &&
                                  p0_val["type_name"].is_string() &&
                                  p0_val["type_name"].get<std::string>() == "MediaStreamV1",
                              "list_types custom port exposes type_name");
                        check(!p0_val.is_null() && p0_val.contains("stable_type_id") &&
                                  p0_val["stable_type_id"].is_string() &&
                                  p0_val["stable_type_id"].get<std::string>() ==
                                      "seethroughlab.vivid.media_stream_v1",
                              "list_types custom port exposes stable_type_id");
                        check(!p0_val.is_null() && p0_val.contains("payload_size") &&
                                  p0_val["payload_size"].is_number_unsigned() &&
                                  p0_val["payload_size"].get<uint64_t>() > 0,
                              "list_types custom port exposes payload_size");
                        check(!p0_val.is_null() && p0_val.contains("custom_type_registered") &&
                                  p0_val["custom_type_registered"].is_boolean() &&
                                  p0_val["custom_type_registered"].get<bool>(),
                              "list_types custom port reports registry presence");
                        check(!p0_val.is_null() && p0_val.contains("audio_safe") &&
                                  p0_val["audio_safe"].is_boolean() &&
                                  p0_val["audio_safe"].get<bool>(),
                              "list_types custom port exposes audio_safe");
                    }
                }
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
                    bool found_media_stream = false;
                    for (const auto& item : custom_types) {
                        if (!item.contains("stable_type_id") || !item["stable_type_id"].is_string()) continue;
                        if (item["stable_type_id"].get<std::string>() !=
                                "seethroughlab.vivid.media_stream_v1") continue;
                        found_media_stream = true;
                        check(item.contains("transport") && item["transport"].is_string() &&
                                  item["transport"].get<std::string>() == "custom_ref",
                              "registry diagnostics preserves custom port transport");
                        check(item.contains("audio_safe") && item["audio_safe"].is_boolean() &&
                                  item["audio_safe"].get<bool>(),
                              "registry diagnostics preserves audio_safe");
                        break;
                    }
                    check(found_media_stream, "registry diagnostics includes MediaStreamV1");
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
                R"({"name":"mcp_pkg_e2e","domain":"control","destination":"package:vivid-scaffold-e2e"})");
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
                R"({"name":"mcp_core_fallback","domain":"control","destination":"auto"})");
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
        } else if (p == 5) {
            api.apply_pending(has_gpu_ops, has_audio);
            scheduler.tick(0.0, 0.016, 3);
            phase.store(6);
        } else if (p == 7) {
            api.apply_pending(has_gpu_ops, has_audio);
            scheduler.tick(0.0, 0.016, 4);
            phase.store(8);
        } else if (p == 9) {
            api.apply_pending(has_gpu_ops, has_audio);
            scheduler.tick(0.0, 0.016, 5);
            phase.store(10);
        } else if (p == 11) {
            api.apply_pending(has_gpu_ops, has_audio);
            scheduler.tick(0.0, 0.016, 6);
            phase.store(12);
        } else if (p == 13) {
            api.apply_pending(has_gpu_ops, has_audio);
            scheduler.tick(0.0, 0.016, 7);
            phase.store(14);
        } else if (p == 15) {
            api.apply_pending(has_gpu_ops, has_audio);
            scheduler.tick(0.0, 0.016, 8);
            phase.store(16);
        } else if (p == 17) {
            api.apply_pending(has_gpu_ops, has_audio);
            scheduler.tick(0.0, 0.016, 9);
            phase.store(18);
        } else if (p == 19) {
            api.apply_pending(has_gpu_ops, has_audio);
            scheduler.tick(0.0, 0.016, 10);
            phase.store(20);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Drain any final requests
    server.process_requests(api, graph, scheduler, registry,
                            has_gpu_ops, has_audio);

    client_thread.join();
    server.stop();
    scheduler.shutdown();
    std::filesystem::current_path(original_cwd);
    std::filesystem::remove_all(isolated_cwd);
    std::filesystem::remove_all(staging);
    std::filesystem::remove_all(test_home);

    int f = failures.load();
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        f == 0 ? "ALL PASSED" : "SOME FAILED", f);
    return f == 0 ? 0 : 1;
}
