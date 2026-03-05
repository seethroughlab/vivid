#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/runtime_api.h"
#include "runtime/control_server.h"
#include "runtime/package_compiler.h"
#include "runtime/package_manager.h"
#include "runtime/package_catalog.h"
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

    // Tick once so nodes have output values
    scheduler.tick(0.0, 0.016, 0);

    // --- Start ControlServer ---
    vivid::ControlServer server;
    server.set_package_manager(&pkg_manager);
    server.set_package_catalog(&pkg_catalog);
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
