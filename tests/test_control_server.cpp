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
#include "yyjson.h"
#include <ixwebsocket/IXHttpClient.h>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

static std::string json_str(yyjson_val* v) {
    if (!v) return "";
    const char* s = yyjson_get_str(v);
    return s ? s : "";
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string graph_path = build_dir + "/test_runtime_api.json";
    constexpr int kPort = 19876;
    const std::string base_url = "http://127.0.0.1:19876";

    // Isolate package/catalog test state from the user's real config dir.
    std::string test_home = build_dir + "/.test_cs_home";
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
            "      \"url\": \"https://example.com/catalog-test-pkg.git\",\n"
            "      \"category\": \"control\",\n"
            "      \"tags\": [\"test\", \"catalog\"]\n"
            "    }\n"
            "  ]\n"
            "}\n";
    }
    vivid::PackageCompiler pkg_compiler("", "");
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
    std::string local_pkg_link_root = build_dir + "/packages";
    std::filesystem::remove_all(local_pkg_link_root);
    std::filesystem::create_directories(local_pkg_link_root);
    std::string local_pkg_link = local_pkg_link_root + "/vivid-scaffold-e2e";
    std::error_code sec;
    std::filesystem::remove(local_pkg_link, sec);
    std::filesystem::create_directory_symlink(std::filesystem::absolute(local_pkg_src),
                                              local_pkg_link, sec);
    check(!sec, "created linked local scaffold package");
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
                if (params && yyjson_arr_size(params) > 0) {
                    yyjson_val* p0 = yyjson_arr_get_first(params);
                    yyjson_val* type = p0 ? yyjson_obj_get(p0, "type") : nullptr;
                    yyjson_val* tag = p0 ? yyjson_obj_get(p0, "semantic_tag") : nullptr;
                    yyjson_val* shape = p0 ? yyjson_obj_get(p0, "semantic_shape") : nullptr;
                    yyjson_val* unit = p0 ? yyjson_obj_get(p0, "semantic_unit") : nullptr;
                    yyjson_val* intent = p0 ? yyjson_obj_get(p0, "semantic_intent") : nullptr;
                    check(type && yyjson_is_str(type), "inspect_graph params expose type");
                    check(tag && yyjson_is_str(tag) &&
                              std::string(yyjson_get_str(tag)) == "frequency_hz",
                          "inspect_graph params expose semantic_tag");
                    check(shape && yyjson_is_str(shape) &&
                              std::string(yyjson_get_str(shape)) == "scalar",
                          "inspect_graph params expose semantic_shape");
                    check(unit && yyjson_is_str(unit) &&
                              std::string(yyjson_get_str(unit)) == "Hz",
                          "inspect_graph params expose semantic_unit");
                    check(intent && yyjson_is_str(intent) &&
                              std::string(yyjson_get_str(intent)) == "test_scale",
                          "inspect_graph params expose semantic_intent");
                }
            }
        }

        // Phase 1b: introspect_nodes — per-node perception payload
        std::fprintf(stderr, "\n--- introspect_nodes ---\n");
        {
            auto r = post(client, base_url, "introspect_nodes");
            check(r.ok, "introspect_nodes ok");
            if (r.root) {
                yyjson_val* sv = yyjson_obj_get(r.root, "schema_version");
                check(sv && yyjson_is_int(sv) && yyjson_get_int(sv) == 1,
                      "introspect_nodes schema_version=1");

                yyjson_val* result = yyjson_obj_get(r.root, "result");
                yyjson_val* nodes = result ? yyjson_obj_get(result, "nodes") : nullptr;
                check(nodes && yyjson_is_arr(nodes), "introspect_nodes returns nodes array");
                check(nodes && yyjson_arr_size(nodes) == 2, "introspect_nodes returns 2 nodes");

                yyjson_val* first_node = nodes ? yyjson_arr_get_first(nodes) : nullptr;
                yyjson_val* node_id = first_node ? yyjson_obj_get(first_node, "node_id") : nullptr;
                yyjson_val* node_type = first_node ? yyjson_obj_get(first_node, "type") : nullptr;
                yyjson_val* domain = first_node ? yyjson_obj_get(first_node, "domain") : nullptr;
                yyjson_val* health = first_node ? yyjson_obj_get(first_node, "health") : nullptr;
                yyjson_val* params = first_node ? yyjson_obj_get(first_node, "params") : nullptr;
                yyjson_val* param_meta = first_node ? yyjson_obj_get(first_node, "param_meta") : nullptr;
                yyjson_val* inputs = first_node ? yyjson_obj_get(first_node, "inputs") : nullptr;
                yyjson_val* outputs = first_node ? yyjson_obj_get(first_node, "outputs") : nullptr;
                yyjson_val* domain_metrics = first_node ? yyjson_obj_get(first_node, "domain_metrics") : nullptr;
                yyjson_val* incoming_wires = first_node ? yyjson_obj_get(first_node, "incoming_wires") : nullptr;
                yyjson_val* outgoing_wires = first_node ? yyjson_obj_get(first_node, "outgoing_wires") : nullptr;
                check(node_id && yyjson_is_str(node_id), "introspection node has node_id");
                check(node_type && yyjson_is_str(node_type), "introspection node has type");
                check(domain && yyjson_is_str(domain), "introspection node has domain");
                check(health && yyjson_is_obj(health), "introspection node has health object");
                check(params && yyjson_is_obj(params), "introspection node has params object");
                check(param_meta && yyjson_is_arr(param_meta), "introspection node has param_meta array");
                if (param_meta && yyjson_arr_size(param_meta) > 0) {
                    yyjson_val* pm0 = yyjson_arr_get_first(param_meta);
                    yyjson_val* tag = pm0 ? yyjson_obj_get(pm0, "semantic_tag") : nullptr;
                    yyjson_val* shape = pm0 ? yyjson_obj_get(pm0, "semantic_shape") : nullptr;
                    yyjson_val* unit = pm0 ? yyjson_obj_get(pm0, "semantic_unit") : nullptr;
                    yyjson_val* intent = pm0 ? yyjson_obj_get(pm0, "semantic_intent") : nullptr;
                    check(tag && yyjson_is_str(tag) &&
                              std::string(yyjson_get_str(tag)) == "frequency_hz",
                          "introspection param_meta exposes semantic_tag");
                    check(shape && yyjson_is_str(shape) &&
                              std::string(yyjson_get_str(shape)) == "scalar",
                          "introspection param_meta exposes semantic_shape");
                    check(unit && yyjson_is_str(unit) &&
                              std::string(yyjson_get_str(unit)) == "Hz",
                          "introspection param_meta exposes semantic_unit");
                    check(intent && yyjson_is_str(intent) &&
                              std::string(yyjson_get_str(intent)) == "test_scale",
                          "introspection param_meta exposes semantic_intent");
                }
                check(inputs && yyjson_is_arr(inputs), "introspection node has inputs array");
                check(outputs && yyjson_is_arr(outputs), "introspection node has outputs array");
                check(domain_metrics && yyjson_is_obj(domain_metrics),
                      "introspection node has domain_metrics object");
                check(incoming_wires && yyjson_is_int(incoming_wires),
                      "introspection node has incoming_wires");
                check(outgoing_wires && yyjson_is_int(outgoing_wires),
                      "introspection node has outgoing_wires");
            }
        }

        // Phase 1c: run_diagnostics — graph-level perception findings
        std::fprintf(stderr, "\n--- run_diagnostics ---\n");
        {
            auto r = post(client, base_url, "run_diagnostics");
            check(r.ok, "run_diagnostics ok");
            if (r.root) {
                yyjson_val* sv = yyjson_obj_get(r.root, "schema_version");
                check(sv && yyjson_is_int(sv) && yyjson_get_int(sv) == 1,
                      "run_diagnostics schema_version=1");

                yyjson_val* result = yyjson_obj_get(r.root, "result");
                yyjson_val* summary = result ? yyjson_obj_get(result, "summary") : nullptr;
                yyjson_val* findings = result ? yyjson_obj_get(result, "findings") : nullptr;
                yyjson_val* hints = result ? yyjson_obj_get(result, "hints") : nullptr;

                check(summary && yyjson_is_obj(summary), "run_diagnostics has summary");
                check(findings && yyjson_is_arr(findings), "run_diagnostics has findings array");
                check(hints && yyjson_is_arr(hints), "run_diagnostics has hints array");

                if (summary) {
                    yyjson_val* critical = yyjson_obj_get(summary, "critical");
                    yyjson_val* warning = yyjson_obj_get(summary, "warning");
                    yyjson_val* info = yyjson_obj_get(summary, "info");
                    check(critical && yyjson_is_int(critical), "summary has critical count");
                    check(warning && yyjson_is_int(warning), "summary has warning count");
                    check(info && yyjson_is_int(info), "summary has info count");
                    if (critical && warning && info &&
                        yyjson_is_int(critical) && yyjson_is_int(warning) && yyjson_is_int(info)) {
                        check(yyjson_get_int(critical) == 0 &&
                              yyjson_get_int(warning) == 0 &&
                              yyjson_get_int(info) == 0,
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
            if (r1.root) {
                yyjson_val* result = yyjson_obj_get(r1.root, "result");
                yyjson_val* findings = result ? yyjson_obj_get(result, "findings") : nullptr;
                if (findings && yyjson_is_arr(findings)) {
                    size_t i, max; yyjson_val* f = nullptr;
                    yyjson_arr_foreach(findings, i, max, f) {
                        if (!ids1.empty()) ids1 += ",";
                        ids1 += json_str(yyjson_obj_get(f, "id"));
                        ids1 += "@";
                        ids1 += json_str(yyjson_obj_get(f, "node_id"));
                    }
                }
            }
            if (r2.root) {
                yyjson_val* result = yyjson_obj_get(r2.root, "result");
                yyjson_val* findings = result ? yyjson_obj_get(result, "findings") : nullptr;
                if (findings && yyjson_is_arr(findings)) {
                    size_t i, max; yyjson_val* f = nullptr;
                    yyjson_arr_foreach(findings, i, max, f) {
                        if (!ids2.empty()) ids2 += ",";
                        ids2 += json_str(yyjson_obj_get(f, "id"));
                        ids2 += "@";
                        ids2 += json_str(yyjson_obj_get(f, "node_id"));
                    }
                }
            }
            check(ids1 == ids2, "run_diagnostics findings order deterministic");
        }
        // Broken fixture regression: intentionally disconnected node should emit expected warning.
        {
            auto add_missing = post(client, base_url, "add_node",
                R"({"type":"TestOp","id":"missing_fixture"})");
            check(add_missing.ok, "add_node broken fixture ok");
            phase.store(5);
            while (phase.load() < 6) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto r = post(client, base_url, "run_diagnostics");
            check(r.ok, "run_diagnostics on broken fixture ok");
            bool found_missing = false;
            if (r.root) {
                yyjson_val* result = yyjson_obj_get(r.root, "result");
                yyjson_val* summary = result ? yyjson_obj_get(result, "summary") : nullptr;
                yyjson_val* findings = result ? yyjson_obj_get(result, "findings") : nullptr;
                if (summary) {
                    yyjson_val* warning = yyjson_obj_get(summary, "warning");
                    check(warning && yyjson_is_int(warning) && yyjson_get_int(warning) >= 1,
                          "broken fixture emits warning diagnostics");
                }
                if (findings && yyjson_is_arr(findings)) {
                    size_t i, max; yyjson_val* f = nullptr;
                    yyjson_arr_foreach(findings, i, max, f) {
                        yyjson_val* idv = yyjson_obj_get(f, "id");
                        yyjson_val* nv = yyjson_obj_get(f, "node_id");
                        if (idv && yyjson_is_str(idv) &&
                            nv && yyjson_is_str(nv) &&
                            std::string(yyjson_get_str(idv)) == "isolated_node" &&
                            std::string(yyjson_get_str(nv)) == "missing_fixture") {
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
            if (ig.root) {
                yyjson_val* result = yyjson_obj_get(ig.root, "result");
                yyjson_val* nodes = result ? yyjson_obj_get(result, "nodes") : nullptr;
                check(nodes && yyjson_is_arr(nodes) && yyjson_arr_size(nodes) == 2,
                      "broken fixture cleanup restored 2-node graph");
            }
        }

        // Phase 1d: validate_checks / run_checks
        std::fprintf(stderr, "\n--- checks ---\n");
        {
            auto r = post(client, base_url, "validate_checks",
                R"({"checks":[{"id":"node_count_is_two","type":"state_check","path":"graph.node_count","op":"==","value":2},{"id":"no_missing_ops","type":"diagnostic_check","op":"finding_absent","finding_id":"missing_operator_type"}]})");
            check(r.ok, "validate_checks ok");
            if (r.root) {
                yyjson_val* sv = yyjson_obj_get(r.root, "schema_version");
                check(sv && yyjson_is_int(sv) && yyjson_get_int(sv) == 1,
                      "validate_checks schema_version=1");
                yyjson_val* result = yyjson_obj_get(r.root, "result");
                yyjson_val* valid = result ? yyjson_obj_get(result, "valid") : nullptr;
                yyjson_val* ec = result ? yyjson_obj_get(result, "error_count") : nullptr;
                check(valid && yyjson_is_bool(valid) && yyjson_get_bool(valid),
                      "validate_checks valid=true");
                check(ec && yyjson_is_int(ec) && yyjson_get_int(ec) == 0,
                      "validate_checks error_count=0");
            }
        }
        {
            auto r = post(client, base_url, "validate_checks",
                R"({"checks":[{"id":"dup","type":"state_check","path":"graph.node_count","op":"==","value":2},{"id":"dup","type":"state_check","path":"graph.node_count","op":"==","value":2}]})");
            check(r.ok, "validate_checks duplicate-id request ok");
            if (r.root) {
                yyjson_val* result = yyjson_obj_get(r.root, "result");
                yyjson_val* valid = result ? yyjson_obj_get(result, "valid") : nullptr;
                yyjson_val* ec = result ? yyjson_obj_get(result, "error_count") : nullptr;
                check(valid && yyjson_is_bool(valid) && !yyjson_get_bool(valid),
                      "validate_checks duplicate-id valid=false");
                check(ec && yyjson_is_int(ec) && yyjson_get_int(ec) > 0,
                      "validate_checks duplicate-id has error_count>0");
            }
            auto rbad = post(client, base_url, "validate_checks", R"({"foo":[]})");
            check(!rbad.ok, "validate_checks missing checks array -> ok=false");
        }
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[{"id":"node_count_is_two","type":"state_check","path":"graph.node_count","op":"==","value":2,"severity":"critical"},{"id":"no_missing_ops","type":"diagnostic_check","op":"finding_absent","finding_id":"missing_operator_type","severity":"critical"}]})");
            check(r.ok, "run_checks ok");
            if (r.root) {
                yyjson_val* sv = yyjson_obj_get(r.root, "schema_version");
                check(sv && yyjson_is_int(sv) && yyjson_get_int(sv) == 1,
                      "run_checks schema_version=1");
                yyjson_val* result = yyjson_obj_get(r.root, "result");
                yyjson_val* all_passed = result ? yyjson_obj_get(result, "all_passed") : nullptr;
                yyjson_val* all_critical = result ? yyjson_obj_get(result, "all_critical_passed") : nullptr;
                yyjson_val* summary = result ? yyjson_obj_get(result, "summary") : nullptr;
                yyjson_val* results = result ? yyjson_obj_get(result, "results") : nullptr;
                check(all_passed && yyjson_is_bool(all_passed) && yyjson_get_bool(all_passed),
                      "run_checks all_passed=true");
                check(all_critical && yyjson_is_bool(all_critical) && yyjson_get_bool(all_critical),
                      "run_checks all_critical_passed=true");
                check(summary && yyjson_is_obj(summary), "run_checks has summary");
                check(results && yyjson_is_arr(results) && yyjson_arr_size(results) == 2,
                      "run_checks returns 2 results");
            }
        }
        // Deterministic check result ordering regression: results sorted by id regardless of input order.
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[{"id":"z_second","type":"state_check","path":"graph.node_count","op":"==","value":2},{"id":"a_first","type":"state_check","path":"graph.node_count","op":"==","value":2}]})");
            check(r.ok, "run_checks deterministic-order request ok");
            if (r.root) {
                yyjson_val* result = yyjson_obj_get(r.root, "result");
                yyjson_val* rows = result ? yyjson_obj_get(result, "results") : nullptr;
                check(rows && yyjson_is_arr(rows) && yyjson_arr_size(rows) == 2,
                      "run_checks deterministic-order returns 2 results");
                if (rows && yyjson_arr_size(rows) == 2) {
                    yyjson_val* r0 = yyjson_arr_get(rows, 0);
                    yyjson_val* r1 = yyjson_arr_get(rows, 1);
                    check(json_str(yyjson_obj_get(r0, "id")) == "a_first",
                          "run_checks results sorted id[0]=a_first");
                    check(json_str(yyjson_obj_get(r1, "id")) == "z_second",
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
            if (gp_before.root) {
                yyjson_val* v = yyjson_obj_get(gp_before.root, "value");
                if (v && yyjson_is_num(v)) before = yyjson_get_num(v);
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
            if (gp_after.root) {
                yyjson_val* v = yyjson_obj_get(gp_after.root, "value");
                check(v && yyjson_is_num(v) && std::fabs(yyjson_get_num(v) - before) < 1e-6,
                      "perception endpoints do not mutate parameter state");
            }

            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after perception endpoints ok");
            if (ig.root) {
                yyjson_val* result = yyjson_obj_get(ig.root, "result");
                yyjson_val* nodes = result ? yyjson_obj_get(result, "nodes") : nullptr;
                yyjson_val* conns = result ? yyjson_obj_get(result, "connections") : nullptr;
                check(nodes && yyjson_is_arr(nodes) && yyjson_arr_size(nodes) == 2,
                      "node count unchanged after perception endpoints");
                check(conns && yyjson_is_arr(conns) && yyjson_arr_size(conns) == 1,
                      "connection count unchanged after perception endpoints");
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
                    yyjson_val* t0 = nullptr;
                    yyjson_val* ms_type = nullptr;
                    yyjson_val* sec_type = nullptr;
                    size_t i = 0, max = 0;
                    yyjson_val* t = nullptr;
                    yyjson_arr_foreach(types, i, max, t) {
                        yyjson_val* n = yyjson_obj_get(t, "name");
                        if (!n || !yyjson_is_str(n)) continue;
                        std::string tn = yyjson_get_str(n);
                        if (tn == "TestOp") {
                            t0 = t;
                        } else if (tn == "MsSourceOp") {
                            ms_type = t;
                        } else if (tn == "SecDestOp") {
                            sec_type = t;
                        }
                    }
                    check(t0 != nullptr, "contains TestOp");
                    yyjson_val* params = t0 ? yyjson_obj_get(t0, "params") : nullptr;
                    check(params && yyjson_arr_size(params) > 0, "TestOp has params");
                    if (params && yyjson_arr_size(params) > 0) {
                        yyjson_val* p0 = yyjson_arr_get_first(params);
                        yyjson_val* tag = p0 ? yyjson_obj_get(p0, "semantic_tag") : nullptr;
                        yyjson_val* shape = p0 ? yyjson_obj_get(p0, "semantic_shape") : nullptr;
                        yyjson_val* unit = p0 ? yyjson_obj_get(p0, "semantic_unit") : nullptr;
                        yyjson_val* intent = p0 ? yyjson_obj_get(p0, "semantic_intent") : nullptr;
                        check(tag && yyjson_is_str(tag) &&
                                  std::string(yyjson_get_str(tag)) == "frequency_hz",
                              "list_types param exposes semantic_tag");
                        check(shape && yyjson_is_str(shape) &&
                                  std::string(yyjson_get_str(shape)) == "scalar",
                              "list_types param exposes semantic_shape");
                        check(unit && yyjson_is_str(unit) &&
                                  std::string(yyjson_get_str(unit)) == "Hz",
                              "list_types param exposes semantic_unit");
                        check(intent && yyjson_is_str(intent) &&
                                  std::string(yyjson_get_str(intent)) == "test_scale",
                              "list_types param exposes semantic_intent");
                    }
                    yyjson_val* outputs = t0 ? yyjson_obj_get(t0, "outputs") : nullptr;
                    check(outputs && yyjson_arr_size(outputs) > 0, "TestOp has ports");

                    auto check_first_param_tag = [&](yyjson_val* type_obj, const char* expected_tag,
                                                     const char* label) {
                        check(type_obj != nullptr, label);
                        yyjson_val* params_obj = type_obj ? yyjson_obj_get(type_obj, "params") : nullptr;
                        yyjson_val* p0 = (params_obj && yyjson_arr_size(params_obj) > 0)
                                       ? yyjson_arr_get_first(params_obj) : nullptr;
                        yyjson_val* tag = p0 ? yyjson_obj_get(p0, "semantic_tag") : nullptr;
                        std::string tag_label = std::string(label) + " tag";
                        check(tag && yyjson_is_str(tag) &&
                                  std::string(yyjson_get_str(tag)) == expected_tag,
                              tag_label.c_str());
                    };
                    check_first_param_tag(ms_type, "time_milliseconds",
                                          "MsSourceOp semantic_tag is time_milliseconds");
                    check_first_param_tag(sec_type, "time_seconds",
                                          "SecDestOp semantic_tag is time_seconds");
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

        // Phase 4b: undo/redo via MCP control methods
        std::fprintf(stderr, "\n--- undo/redo ---\n");
        {
            auto ru = post(client, base_url, "undo");
            check(ru.ok, "undo ok");
            auto rg = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(rg.ok, "get_param after undo ok");
            if (rg.root) {
                yyjson_val* val = yyjson_obj_get(rg.root, "value");
                check(val && std::fabs(yyjson_get_num(val) - 3.0) < 1e-4,
                      "value restored to 3.0 after undo");
            }

            auto rr = post(client, base_url, "redo");
            check(rr.ok, "redo ok");
            auto rg2 = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(rg2.ok, "get_param after redo ok");
            if (rg2.root) {
                yyjson_val* val = yyjson_obj_get(rg2.root, "value");
                check(val && std::fabs(yyjson_get_num(val) - 9.0) < 1e-4,
                      "value restored to 9.0 after redo");
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

        // Phase 10b: semantic-default connect remap
        std::fprintf(stderr, "\n--- semantic default connect remap ---\n");
        {
            auto add_src = post(client, base_url, "add_node",
                R"({"type":"MsSourceOp","id":"ms1"})");
            check(add_src.ok, "add_node MsSourceOp ms1 ok");

            auto add_dst = post(client, base_url, "add_node",
                R"({"type":"SecDestOp","id":"s1"})");
            check(add_dst.ok, "add_node SecDestOp s1 ok");

            auto c = post(client, base_url, "connect",
                R"({"from_addr":"ms1/ms","to_addr":"s1/sec","semantic_defaults":true})");
            check(c.ok, "connect ms1/ms -> s1/sec with semantic_defaults ok");
            if (c.root) {
                yyjson_val* msg = yyjson_obj_get(c.root, "message");
                std::string m = json_str(msg);
                check(m.find("semantic default remap applied") != std::string::npos,
                      "connect response reports semantic remap applied");
            }
        }

        phase.store(9);
        while (phase.load() < 10) std::this_thread::sleep_for(std::chrono::milliseconds(5));

        {
            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after semantic connect ok");
            if (ig.root) {
                yyjson_val* result = yyjson_obj_get(ig.root, "result");
                yyjson_val* conns = result ? yyjson_obj_get(result, "connections") : nullptr;
                bool found = false;
                if (conns && yyjson_is_arr(conns)) {
                    yyjson_val* conn = nullptr;
                    size_t idx = 0, max = 0;
                    yyjson_arr_foreach(conns, idx, max, conn) {
                        yyjson_val* from = yyjson_obj_get(conn, "from");
                        yyjson_val* to   = yyjson_obj_get(conn, "to");
                        if (json_str(from) != "ms1/ms" || json_str(to) != "s1/sec") continue;
                        found = true;
                        yyjson_val* fmin = yyjson_obj_get(conn, "from_min");
                        yyjson_val* fmax = yyjson_obj_get(conn, "from_max");
                        yyjson_val* tmin = yyjson_obj_get(conn, "to_min");
                        yyjson_val* tmax = yyjson_obj_get(conn, "to_max");
                        check(fmin && std::fabs(yyjson_get_num(fmin) - 0.0) < 1e-6,
                              "semantic remap from_min=0");
                        check(fmax && std::fabs(yyjson_get_num(fmax) - 2000.0) < 1e-6,
                              "semantic remap from_max=2000");
                        check(tmin && std::fabs(yyjson_get_num(tmin) - 0.0) < 1e-6,
                              "semantic remap to_min=0");
                        check(tmax && std::fabs(yyjson_get_num(tmax) - 2.0) < 1e-6,
                              "semantic remap to_max=2");
                        break;
                    }
                }
                check(found, "semantic remap connection present in inspect_graph");
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

            // Undo history must reset on file load.
            auto u = post(client, base_url, "undo");
            check(!u.ok, "undo after load_graph reports no history");

            // Baseline should still be tracked: mutate then undo returns to loaded state.
            auto sp = post(client, base_url, "set_param",
                R"({"node_id":"a","param":"scale","value":8.0})");
            check(sp.ok, "set_param after load_graph ok");
            auto u2 = post(client, base_url, "undo");
            check(u2.ok, "undo after post-load mutation ok");
            auto gp = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(gp.ok, "get_param after post-load undo ok");
            if (gp.root) {
                yyjson_val* val = yyjson_obj_get(gp.root, "value");
                check(val && std::fabs(yyjson_get_num(val) - 3.0) < 1e-4,
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
            if (rl.root) {
                yyjson_val* result = yyjson_obj_get(rl.root, "result");
                yyjson_val* pkgs = result ? yyjson_obj_get(result, "packages") : nullptr;
                check(pkgs && yyjson_is_arr(pkgs), "list_packages returns array");
                bool found = false;
                if (pkgs) {
                    size_t idx, max;
                    yyjson_val* p = nullptr;
                    yyjson_arr_foreach(pkgs, idx, max, p) {
                        yyjson_val* name = yyjson_obj_get(p, "name");
                        if (name && yyjson_is_str(name) &&
                            std::string(yyjson_get_str(name)) == "catalog-test-pkg") {
                            found = true;
                            yyjson_val* scope = yyjson_obj_get(p, "source_scope");
                            check(scope && yyjson_is_str(scope), "list_packages exposes source_scope");
                            if (scope && yyjson_is_str(scope))
                                check(std::string(yyjson_get_str(scope)) == "user",
                                      "source_scope is user");
                            break;
                        }
                    }
                }
                check(found, "list_packages contains installed test package");
            }

            auto rc = post(client, base_url, "package_catalog");
            check(rc.ok, "package_catalog ok");
            if (rc.root) {
                yyjson_val* pkgs = yyjson_obj_get(rc.root, "packages");
                check(pkgs && yyjson_is_arr(pkgs), "package_catalog returns packages array");
                bool found = false;
                if (pkgs) {
                    size_t idx, max;
                    yyjson_val* p = nullptr;
                    yyjson_arr_foreach(pkgs, idx, max, p) {
                        yyjson_val* name = yyjson_obj_get(p, "name");
                        if (name && yyjson_is_str(name) &&
                            std::string(yyjson_get_str(name)) == "catalog-test-pkg") {
                            found = true;
                            yyjson_val* installed = yyjson_obj_get(p, "installed");
                            yyjson_val* iv = yyjson_obj_get(p, "installed_version");
                            check(installed && yyjson_is_bool(installed) && yyjson_get_bool(installed),
                                  "package_catalog marks installed package");
                            check(iv && yyjson_is_str(iv) &&
                                      std::string(yyjson_get_str(iv)) == "1.0.0",
                                  "package_catalog includes installed_version");
                            break;
                        }
                    }
                }
                check(found, "package_catalog contains catalog-test-pkg");
            }

            auto ru = post(client, base_url, "check_package_updates", R"({"core_version":"0.9.0"})");
            check(ru.ok, "check_package_updates ok");
            if (ru.root) {
                yyjson_val* updates = yyjson_obj_get(ru.root, "updates_available");
                yyjson_val* pkgs = yyjson_obj_get(ru.root, "packages");
                check(updates && yyjson_is_int(updates) && yyjson_get_int(updates) >= 1,
                      "check_package_updates reports updates_available");
                check(pkgs && yyjson_is_arr(pkgs), "check_package_updates returns packages array");
                if (pkgs && yyjson_arr_size(pkgs) > 0) {
                    yyjson_val* p0 = yyjson_arr_get_first(pkgs);
                    yyjson_val* cls = p0 ? yyjson_obj_get(p0, "classification") : nullptr;
                    yyjson_val* ua = p0 ? yyjson_obj_get(p0, "update_available") : nullptr;
                    yyjson_val* comp = p0 ? yyjson_obj_get(p0, "compatible") : nullptr;
                    check(cls && yyjson_is_str(cls), "update entry includes classification");
                    check(ua && yyjson_is_bool(ua) && yyjson_get_bool(ua),
                          "update entry marks update_available=true");
                    check(comp && yyjson_is_bool(comp), "update entry includes compatible flag");
                }
            }

            auto ri = post(client, base_url, "check_package_updates", R"({"core_version":"9.0.0"})");
            check(ri.ok, "check_package_updates incompatible-core ok");
            if (ri.root) {
                yyjson_val* incompatible = yyjson_obj_get(ri.root, "incompatible_updates");
                yyjson_val* pkgs = yyjson_obj_get(ri.root, "packages");
                check(incompatible && yyjson_is_int(incompatible) && yyjson_get_int(incompatible) >= 1,
                      "check_package_updates reports incompatible_updates");
                if (pkgs && yyjson_is_arr(pkgs) && yyjson_arr_size(pkgs) > 0) {
                    yyjson_val* p0 = yyjson_arr_get_first(pkgs);
                    yyjson_val* cls = p0 ? yyjson_obj_get(p0, "classification") : nullptr;
                    yyjson_val* comp = p0 ? yyjson_obj_get(p0, "compatible") : nullptr;
                    check(cls && yyjson_is_str(cls) &&
                              std::string(yyjson_get_str(cls)) == "incompatible_update",
                          "incompatible update classification is exposed");
                    check(comp && yyjson_is_bool(comp) && !yyjson_get_bool(comp),
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
            if (!rp.ok && rp.root) {
                yyjson_val* err_v = yyjson_obj_get(rp.root, "error");
                if (err_v && yyjson_is_str(err_v)) {
                    std::fprintf(stderr, "  INFO: scaffold_operator project error: %s\n",
                                 yyjson_get_str(err_v));
                }
            }
            if (rp.root) {
                yyjson_val* result = yyjson_obj_get(rp.root, "result");
                yyjson_val* is_pkg = result ? yyjson_obj_get(result, "destination_is_package") : nullptr;
                yyjson_val* pkg_name = result ? yyjson_obj_get(result, "destination_package") : nullptr;
                yyjson_val* root_path = result ? yyjson_obj_get(result, "destination_root") : nullptr;
                check(is_pkg && yyjson_is_bool(is_pkg) && yyjson_get_bool(is_pkg),
                      "scaffold_operator project resolved to package");
                check(pkg_name && yyjson_is_str(pkg_name) &&
                          std::string(yyjson_get_str(pkg_name)) == "vivid-scaffold-e2e",
                      "scaffold_operator reports destination package");
                check(root_path && yyjson_is_str(root_path), "scaffold_operator reports destination root");
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
            if (rc.root) {
                yyjson_val* result = yyjson_obj_get(rc.root, "result");
                yyjson_val* is_pkg = result ? yyjson_obj_get(result, "destination_is_package") : nullptr;
                yyjson_val* warn = result ? yyjson_obj_get(result, "destination_warning") : nullptr;
                yyjson_val* root_path = result ? yyjson_obj_get(result, "destination_root") : nullptr;
                check(is_pkg && yyjson_is_bool(is_pkg) && !yyjson_get_bool(is_pkg),
                      "auto fallback resolved to core destination");
                check(warn && yyjson_is_str(warn) && std::string(yyjson_get_str(warn)).size() > 0,
                      "auto fallback returns destination warning");
                check(root_path && yyjson_is_str(root_path) &&
                          std::string(yyjson_get_str(root_path)) == scaffold_core_src,
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
            if (r2.root) {
                yyjson_val* msg = yyjson_obj_get(r2.root, "message");
                check(msg && std::string(yyjson_get_str(msg)).find("V1") != std::string::npos,
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
    std::filesystem::remove_all(test_home);

    int f = failures.load();
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        f == 0 ? "ALL PASSED" : "SOME FAILED", f);
    return f == 0 ? 0 : 1;
}
