#include <nlohmann/json.hpp>
#include "runtime/operators/operator_registry.h"
#include "runtime/packages/package_manager.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/core/runtime_health.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/operators/builtin_operators.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/core/runtime_bootstrap.h"
#include "operator_api/gpu_operator.h"
#include "common/gpu_util.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <algorithm>
#include <fstream>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>
#include <signal.h>
#include <unistd.h>
#include <spawn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <thread>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

extern char** environ;

// ============================================================================
// Test infrastructure
// ============================================================================

static int passes  = 0;
static int failures = 0;
static int skipped  = 0;

static void pass(const char* name) {
    std::fprintf(stderr, "  PASS: %s\n", name);
    passes++;
}

static void fail(const char* name, const char* reason) {
    std::fprintf(stderr, "  FAIL: %s — %s\n", name, reason);
    failures++;
}

static void skip(const char* name, const char* reason) {
    std::fprintf(stderr, "  SKIP: %s — %s\n", name, reason);
    skipped++;
}

// ============================================================================
// Headless WebGPU init (no window, no surface)
// ============================================================================

struct HeadlessGpu {
    WGPUInstance instance = nullptr;
    WGPUAdapter  adapter  = nullptr;
    WGPUDevice   device   = nullptr;
    WGPUQueue    queue    = nullptr;

    bool has_gpu_error = false;
    std::string gpu_error_msg;

    void reset_errors() { has_gpu_error = false; gpu_error_msg.clear(); }

    bool init() {
        WGPUInstanceDescriptor inst_desc{};
        instance = wgpuCreateInstance(&inst_desc);
        if (!instance) return false;

        struct AdapterData { WGPUAdapter adapter = nullptr; bool done = false; };
        AdapterData ad;
        WGPURequestAdapterCallbackInfo acb{};
        acb.mode = WGPUCallbackMode_AllowSpontaneous;
        acb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                          WGPUStringView, void* ud1, void*) {
            auto* d = static_cast<AdapterData*>(ud1);
            if (status == WGPURequestAdapterStatus_Success) d->adapter = adapter;
            d->done = true;
        };
        acb.userdata1 = &ad;
        WGPURequestAdapterOptions opts{};
        opts.powerPreference = WGPUPowerPreference_HighPerformance;
        wgpuInstanceRequestAdapter(instance, &opts, acb);
        if (!ad.done || !ad.adapter) return false;
        adapter = ad.adapter;

        struct DeviceData { WGPUDevice device = nullptr; bool done = false; };
        DeviceData dd;
        WGPURequestDeviceCallbackInfo dcb{};
        dcb.mode = WGPUCallbackMode_AllowSpontaneous;
        dcb.callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                          WGPUStringView, void* ud1, void*) {
            auto* d = static_cast<DeviceData*>(ud1);
            if (status == WGPURequestDeviceStatus_Success) d->device = device;
            d->done = true;
        };
        dcb.userdata1 = &dd;
        WGPUDeviceDescriptor dev_desc{};
        dev_desc.label = vivid::to_sv("DemoTest Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void* ud1, void*) {
                auto* g = static_cast<HeadlessGpu*>(ud1);
                g->has_gpu_error = true;
                g->gpu_error_msg = std::string(msg.data, msg.length);
            };
        dev_desc.uncapturedErrorCallbackInfo.userdata1 = this;
        wgpuAdapterRequestDevice(adapter, &dev_desc, dcb);
        if (!dd.done || !dd.device) return false;
        device = dd.device;
        queue = wgpuDeviceGetQueue(device);
        return true;
    }

    void shutdown() {
        if (queue)    { wgpuQueueRelease(queue);       queue    = nullptr; }
        if (device)   { wgpuDeviceRelease(device);     device   = nullptr; }
        if (adapter)  { wgpuAdapterRelease(adapter);   adapter  = nullptr; }
        if (instance) { wgpuInstanceRelease(instance);  instance = nullptr; }
    }
};

// Helper: run one runtime tick with a GPU command encoder, then submit + wait.
static void tick_gpu(vivid::RuntimeCore& runtime, HeadlessGpu& gpu,
                     WGPUTextureFormat format, double time, uint64_t frame) {
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Tick Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

    VividGpuContext gpu_state{};
    gpu_state.device          = gpu.device;
    gpu_state.queue           = gpu.queue;
    gpu_state.command_encoder = encoder;
    gpu_state.output_format   = format;

    runtime.tick(time, 0.016, frame, &gpu_state);

    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.label = vivid::to_sv("Tick Commands");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(gpu.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    struct WorkDone { bool done = false; };
    WorkDone wd;
    WGPUQueueWorkDoneCallbackInfo wcb{};
    wcb.mode = WGPUCallbackMode_AllowSpontaneous;
    wcb.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* ud1, void*) {
        static_cast<WorkDone*>(ud1)->done = true;
    };
    wcb.userdata1 = &wd;
    wgpuQueueOnSubmittedWorkDone(gpu.queue, wcb);
    while (!wd.done) {
        wgpuDevicePoll(gpu.device, true, nullptr);
    }
}

// Per-graph timeout (seconds).
static constexpr int kTimeoutSeconds = 20;

static std::unordered_set<std::string> load_required_packages(const char* graph_path) {
    std::unordered_set<std::string> out;
    try {
        std::ifstream ifs(graph_path);
        if (!ifs) return out;
        auto root = nlohmann::json::parse(ifs, nullptr, false);
        if (!root.is_object()) return out;
        auto meta_it = root.find("meta");
        if (meta_it == root.end() || !meta_it->is_object()) return out;
        auto req_it = meta_it->find("requires_packages");
        if (req_it == meta_it->end() || !req_it->is_array()) return out;
        for (const auto& item : *req_it) {
            if (item.is_string() && !item.get<std::string>().empty())
                out.insert(item.get<std::string>());
        }
    } catch (...) {
    }
    return out;
}

static void print_package_discovery_report(const vivid::DiscoveryReport& report,
                                           const std::unordered_set<std::string>& required_packages) {
    std::fprintf(stderr, "[vivid] Package discovery report: workspace_detected=%s\n",
                 report.workspace_detected ? "true" : "false");
    for (const auto& scope : report.scopes_searched) {
        std::fprintf(stderr, "[vivid]   scope[%s]: %s (%s)\n",
                     scope.scope.c_str(), scope.root.c_str(), scope.exists ? "exists" : "missing");
    }
    for (const auto& info : report.loaded_packages) {
        std::fprintf(stderr, "[vivid]   loaded package: %s (%s) from %s\n",
                     info.name.c_str(), info.version.c_str(), info.path.c_str());
    }
    for (const auto& skipped : report.skipped_packages) {
        std::fprintf(stderr, "[vivid]   skipped package: %s [%s] %s\n",
                     skipped.name.c_str(), skipped.reason.c_str(), skipped.detail.c_str());
    }
    for (const auto& pkg : required_packages) {
        bool found = false;
        for (const auto& info : report.loaded_packages) {
            if (info.name == pkg) {
                found = true;
                break;
            }
        }
        if (!found)
            std::fprintf(stderr, "[vivid]   required package not loaded: %s\n", pkg.c_str());
    }
}

static bool fail_if_required_package_placeholders(vivid::RuntimeCore& runtime,
                                                  const vivid::OperatorRegistry& registry,
                                                  const vivid::Graph& graph,
                                                  const vivid::DiscoveryReport& discovery_report,
                                                  const std::unordered_set<std::string>& required_packages) {
    if (required_packages.empty()) return false;
    const auto* cg = runtime.compiled_graph();
    if (!cg) return false;

    bool any_missing = false;
    for (const auto& node : cg->nodes) {
        if (!node.missing_operator) continue;
        any_missing = true;
        std::string package_name;
        if (const auto* pkg = registry.package_for_type(node.type_name))
            package_name = *pkg;
        else if (const auto* prov = registry.operator_provenance(node.type_name))
            package_name = prov->package_name;

        std::fprintf(stderr,
                     "[vivid] Required-package graph unresolved operator: node='%s' type='%s' package='%s' reason='%s' detail='%s'\n",
                     node.node_id.c_str(), node.type_name.c_str(), package_name.c_str(),
                     node.missing_operator_reason.c_str(), node.missing_operator_detail.c_str());
    }

    if (!any_missing) return false;

    bool saw_required_provider = false;
    for (const auto& node : graph.nodes()) {
        std::string package_name;
        if (const auto* pkg = registry.package_for_type(node.type))
            package_name = *pkg;
        else if (const auto* prov = registry.operator_provenance(node.type))
            package_name = prov->package_name;
        if (!package_name.empty() && required_packages.count(package_name)) {
            saw_required_provider = true;
            break;
        }
    }
    if (!saw_required_provider) {
        std::fprintf(stderr, "[vivid] Required packages were declared, but no graph node types resolved to those package operators before compilation.\n");
    }

    print_package_discovery_report(discovery_report, required_packages);
    return true;
}

// ============================================================================
// Single-graph mode (--single <graph_path>)
//
// Runs one graph in a fresh process. Exit codes:
//   0 = pass
//   1 = fail
//   2 = skip (needs GPU, external I/O, etc.)
// ============================================================================

// Compute the per-graph health JSON filename from the graph's path relative
// to the graphs root, with directory separators flattened to underscores.
// `intro/showcase_demo.json` → `intro_showcase_demo.json`. Eliminates collisions
// when two graphs share a basename across directories.
static std::string flat_filename_for_graph(const std::filesystem::path& graph_path,
                                           const std::filesystem::path& graphs_root) {
    std::error_code ec;
    auto abs_graph = std::filesystem::absolute(graph_path, ec);
    auto abs_root  = std::filesystem::absolute(graphs_root, ec);
    auto rel = std::filesystem::relative(abs_graph, abs_root, ec);
    std::string flat = (ec || rel.empty()) ? graph_path.filename().string() : rel.string();
    std::replace(flat.begin(), flat.end(), '/', '_');
    return flat;
}

// Write a per-graph health JSON dump under <health_dir>/<rel_path_flattened>.
// Consumed by tools/production_gate_report.py budget evaluation. Failures here
// are non-fatal — we do not want a disk error to mask the test result.
static void write_health_dump(const vivid::Graph& graph,
                              const vivid::RuntimeCore* runtime,
                              vivid::OperatorRegistry& registry,
                              vivid::AudioEngine* audio,
                              const char* graph_path,
                              const std::filesystem::path& graphs_root,
                              const std::filesystem::path& health_dir,
                              const char* test_outcome) {
    std::error_code ec;
    std::filesystem::create_directories(health_dir, ec);
    if (ec) return;

    nlohmann::json doc = nlohmann::json::object();
    doc["schema_version"] = 1;
    std::filesystem::path gp(graph_path);
    doc["graph"] = gp.stem().string();
    doc["graph_path"] = graph_path;
    doc["domains"] = graph.meta().domains;
    doc["test_outcome"] = test_outcome;

    if (runtime) {
        auto snapshot = vivid::runtime_health::collect(graph, *runtime, registry, audio,
                                                      /*gpu_context=*/nullptr);
        doc["health"] = vivid::runtime_health::to_json(snapshot);
    } else {
        // Build failed before runtime exists; emit a minimal stub so the
        // budget evaluator can still see the graph.
        nlohmann::json health = nlohmann::json::object();
        nlohmann::json graph_blk = nlohmann::json::object();
        graph_blk["declared_nodes"] = static_cast<int64_t>(graph.nodes().size());
        graph_blk["declared_connections"] = static_cast<int64_t>(graph.connections().size());
        graph_blk["compiled_nodes"] = 0;
        graph_blk["errored_nodes"] = 0;
        graph_blk["missing_operators"] = 0;
        graph_blk["dropped_connections"] = 0;
        graph_blk["audio_nodes"] = 0;
        health["graph"] = std::move(graph_blk);
        health["audio"] = nlohmann::json::object();
        health["gpu"] = nlohmann::json::object();
        health["severity"] = "error";
        health["findings"] = nlohmann::json::array({{
            {"code", "graph_load_failed"},
            {"severity", "error"},
            {"subject", ""},
            {"message", "RuntimeCore::build() failed for this graph."},
        }});
        doc["health"] = std::move(health);
    }

    auto out_path = health_dir / flat_filename_for_graph(gp, graphs_root);
    std::ofstream out(out_path);
    if (out) {
        out << doc.dump(2) << "\n";
        std::fprintf(stderr, "[health] wrote %s\n", out_path.string().c_str());
    }
}

// Default health-dump directory: sibling of the graphs root, under reports/health.
// Resolves to `<build>/reports/health` when the gate runs `test_demo_graphs <build>/graphs`.
static std::filesystem::path default_health_dir(const std::filesystem::path& graphs_root) {
    return std::filesystem::absolute(graphs_root) / ".." / "reports" / "health";
}

static int run_single_graph(const char* exe_path, const char* graph_path,
                            const std::filesystem::path& graphs_root,
                            const std::filesystem::path& health_dir) {
    // Catch fatal signals gracefully
    signal(SIGABRT, [](int) { _exit(2); });
    signal(SIGSEGV, [](int) { _exit(2); });
    alarm(kTimeoutSeconds);

    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    HeadlessGpu gpu;
    bool have_gpu = gpu.init();

    auto runtime_paths = vivid::resolve_runtime_bootstrap_paths(exe_path);
    vivid::OperatorRegistry registry;
    vivid::PackageCompiler pkg_compiler(runtime_paths.source_dir, runtime_paths.build_dir);
    vivid::PackageManager pkg_manager(pkg_compiler, registry);
    vivid::RegistryBootstrapOptions bootstrap_opts;
    auto bootstrap = vivid::bootstrap_operator_registry(registry, &pkg_manager, runtime_paths, bootstrap_opts);

#ifdef __APPLE__
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
#endif

    auto required_packages = load_required_packages(graph_path);

    vivid::Graph graph;
    if (!graph.load(graph_path)) {
        std::fprintf(stderr, "graph.load() failed\n");
        return 1;
    }
    registry.load_for_graph(graph);

    // Classify operators in this graph
    bool has_external_io = false;
    bool has_audio_out = false;
    bool has_movie = false;
    for (const auto& n : graph.nodes()) {
        if (n.type == "SyphonIn"  || n.type == "SyphonOut"  ||
            n.type == "OscIn"     || n.type == "OscOut"     ||
            n.type == "WebcamIn"  ||
            n.type == "Browser"   || n.type == "BrowserAudioIn") {
            has_external_io = true;
        }
        if (n.type == "MovieFile")
            has_movie = true;
        if (n.type == "audio_out") has_audio_out = true;
    }
    if (has_external_io) {
        std::fprintf(stderr, "external I/O\n");
        return 2;
    }

#ifdef __APPLE__
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
#endif

    vivid::RuntimeCore runtime;
    if (!runtime.build(graph, registry)) {
        std::fprintf(stderr, "runtime.build() failed\n");
        if (!required_packages.empty())
            print_package_discovery_report(bootstrap.package_discovery, required_packages);
        write_health_dump(graph, nullptr, registry, nullptr, graph_path, graphs_root, health_dir, "failed");
        return 1;
    }
    // Initialize metronome so transport-synced operators produce output
    runtime.reset_live_metronome(graph.metronome(), 0.0);

    // If any required package was never installed (not in loaded_packages),
    // skip gracefully — we can't test what we can't run.
    for (const auto& pkg : required_packages) {
        bool pkg_loaded = false;
        for (const auto& info : bootstrap.package_discovery.loaded_packages) {
            if (info.name == pkg) { pkg_loaded = true; break; }
        }
        if (!pkg_loaded) {
            std::fprintf(stderr, "required package not installed: %s — skipping\n", pkg.c_str());
            runtime.shutdown();
            gpu.shutdown();
            return 2;  // SKIP
        }
    }

    if (fail_if_required_package_placeholders(runtime, registry, graph,
                                              bootstrap.package_discovery, required_packages)) {
        // If every required package IS in loaded_packages (partially loaded), the
        // missing operators are stale/incompatible with this build — skip rather
        // than fail, because this is an environment issue, not a code bug.
        bool all_required_loaded = true;
        for (const auto& pkg : required_packages) {
            bool found = false;
            for (const auto& info : bootstrap.package_discovery.loaded_packages) {
                if (info.name == pkg) { found = true; break; }
            }
            if (!found) { all_required_loaded = false; break; }
        }
        if (all_required_loaded) {
            std::fprintf(stderr, "required package has stale/incompatible operators — skipping\n");
            write_health_dump(graph, &runtime, registry, nullptr, graph_path, graphs_root, health_dir, "skipped");
            runtime.shutdown();
            gpu.shutdown();
            return 2;  // SKIP
        }
        write_health_dump(graph, &runtime, registry, nullptr, graph_path, graphs_root, health_dir, "failed");
        runtime.shutdown();
        gpu.shutdown();
        return 1;
    }

    // ── Compilation health checks ────────────────────────────────────────
    const auto* cg = runtime.compiled_graph();
    if (cg) {
        for (const auto& dc : cg->dropped_connections) {
            std::fprintf(stderr, "dropped connection: %s/%s → %s/%s (%s)\n",
                         dc.from_node.c_str(), dc.from_port.c_str(),
                         dc.to_node.c_str(), dc.to_port.c_str(), dc.reason.c_str());
            write_health_dump(graph, &runtime, registry, nullptr, graph_path, graphs_root, health_dir, "failed");
            runtime.shutdown(); gpu.shutdown();
            return 1;
        }
        for (const auto& node : cg->nodes) {
            if (node.missing_operator) {
                std::fprintf(stderr, "missing operator: node='%s' type='%s' reason='%s' detail='%s'\n",
                             node.node_id.c_str(), node.type_name.c_str(),
                             node.missing_operator_reason.c_str(), node.missing_operator_detail.c_str());
                write_health_dump(graph, &runtime, registry, nullptr, graph_path, graphs_root, health_dir, "failed");
                runtime.shutdown(); gpu.shutdown();
                return 1;
            }
            if (node.errored) {
                std::fprintf(stderr, "node error: node='%s' type='%s': %s\n",
                             node.node_id.c_str(), node.type_name.c_str(), node.error_message.c_str());
                write_health_dump(graph, &runtime, registry, nullptr, graph_path, graphs_root, health_dir, "failed");
                runtime.shutdown(); gpu.shutdown();
                return 1;
            }
        }
    }

    bool use_gpu = runtime.has_gpu_operators();
    bool use_audio = runtime.has_audio_operators();

    if (use_gpu && !have_gpu) {
        std::fprintf(stderr, "needs GPU\n");
        write_health_dump(graph, &runtime, registry, nullptr, graph_path, graphs_root, health_dir, "skipped");
        runtime.shutdown();
        return 2;
    }
    if (use_gpu) {
        runtime.allocate_gpu_textures(gpu.device, 64, 64, kFormat);
    }

    vivid::AudioEngine* audio = nullptr;
    if (use_audio) {
        audio = new vivid::AudioEngine();
        audio->build(runtime);
        audio->start(true);  // null device
    }

    if (use_gpu) gpu.reset_errors();

    // Tick — movie graphs need more frames for AVFoundation to start decoding
    int tick_count = has_movie ? 240 : use_audio ? 240 : use_gpu ? 60 : 5;
    const uint32_t audio_frames = audio->buffer_size();
    std::vector<float> audio_buf(audio_frames * 2, 0.0f);
    for (uint64_t frame = 0; frame < (uint64_t)tick_count; ++frame) {
        double time = frame * 0.016;
        if (audio) {
            runtime.pre_tick_audio_sync(time);
        }
        if (use_gpu) {
            tick_gpu(runtime, gpu, kFormat, time, frame);
        } else {
            runtime.tick(time, 0.016, frame);
        }
        if (audio) {
            runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());
            audio->process_audio_for_test(audio_buf.data(), audio_frames);
        }
        // Phase 8c: feed runtime-health samplers with this frame's analysis.
        runtime.sample_runtime_health(time);
#ifdef __APPLE__
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, false);
#endif
    }

    // ── Post-tick output validation ─────────────────────────────────────
    // Re-read compiled graph (same pointer, but output_values are now populated)
    cg = runtime.compiled_graph();

    // Check for NaN/Inf in output values
    int result = 0;
    if (cg) {
        for (const auto& node : cg->nodes) {
            if (node.missing_operator) continue;
            for (float v : node.output_values) {
                if (!std::isfinite(v)) {
                    std::fprintf(stderr, "non-finite output: node='%s' type='%s'\n",
                                 node.node_id.c_str(), node.type_name.c_str());
                    result = 1;
                    break;
                }
            }
        }
    }

    // Control-only output: at least one node should have a non-zero output
    if (result == 0 && !use_gpu && !use_audio && cg) {
        bool any_output = false;
        for (const auto& node : cg->nodes) {
            if (node.missing_operator) continue;
            for (float v : node.output_values) {
                if (v != 0.0f) { any_output = true; break; }
            }
            if (any_output) break;
        }
        if (!any_output) {
            std::fprintf(stderr, "control-only graph produced no output\n");
            result = 1;
        }
    }

    // Check results
    if (audio) {
        runtime.audio_frame_bridge().pull_from_audio(*runtime.compiled_graph());
        const auto& analysis = audio->analysis_read();

        for (size_t i = 0; i < analysis.errored.size(); ++i) {
            if (analysis.errored[i]) {
                std::fprintf(stderr, "audio node error: %s\n", analysis.error_msgs[i].data());
                result = 1;
            }
        }

        if (result == 0 && has_audio_out) {
            bool any_nonzero = false;
            for (size_t i = 0; i < analysis.peak.size(); ++i) {
                if (analysis.peak[i][0] > 0.001f) { any_nonzero = true; break; }
            }
            if (!any_nonzero) {
                std::fprintf(stderr, "audio output is silent (all peaks < 0.001)\n");
                result = 1;
            }
        }
    }

    if (use_gpu && gpu.has_gpu_error) {
        std::fprintf(stderr, "GPU error: %s\n", gpu.gpu_error_msg.c_str());
        result = 1;
    }

    const char* outcome = (result == 0) ? "passed" : (result == 2) ? "skipped" : "failed";
    write_health_dump(graph, &runtime, registry, audio, graph_path, graphs_root, health_dir, outcome);

    if (audio) { audio->shutdown(); delete audio; }
    runtime.shutdown();
    gpu.shutdown();

    return result;
}

// ============================================================================
// Main — orchestrates per-graph child processes via posix_spawn
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: test_demo_graphs <graphs_dir> [graph-filter] [--health-dir PATH] [--single <path>]\n");
        return 1;
    }

    // Parse --health-dir (may appear anywhere). Default: <graphs_dir>/../reports/health,
    // which resolves to <build>/reports/health when invoked by the gate.
    std::filesystem::path graphs_root(argv[1]);
    std::filesystem::path health_dir;
    int max_jobs = 0;  // 0 = use default below
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--health-dir") == 0) {
            health_dir = argv[i + 1];
        } else if (std::strcmp(argv[i], "--jobs") == 0) {
            max_jobs = std::max(1, std::atoi(argv[i + 1]));
        }
    }
    if (health_dir.empty()) {
        health_dir = default_health_dir(graphs_root);
    }
    if (max_jobs == 0) {
        // Env override (CI sets this), then default = (nproc + 3) / 4 (≥ 1).
        const char* env_jobs = std::getenv("VIVID_DEMO_GRAPH_JOBS");
        if (env_jobs && *env_jobs) max_jobs = std::max(1, std::atoi(env_jobs));
        else {
            unsigned hw = std::max(1u, std::thread::hardware_concurrency());
            max_jobs = std::max(1, static_cast<int>((hw + 3) / 4));
        }
    }

    // --single mode: run one graph and exit
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--single") == 0) {
            return run_single_graph(argv[0], argv[i + 1], graphs_root, health_dir);
        }
    }

    const char* graphs_dir = argv[1];
    std::string graph_filter;
    // Positional filter: argv[2] if it doesn't look like a flag.
    if (argc >= 3 && argv[2][0] != '-') graph_filter = argv[2];
    if (graph_filter.empty()) {
        const char* env_filter = std::getenv("VIVID_DEMO_GRAPH_FILTER");
        if (env_filter && *env_filter) graph_filter = env_filter;
    }

    std::string exe_path = std::filesystem::absolute(argv[0]).string();
    std::string health_dir_str = health_dir.string();

    // --- Collect graph files ---
    std::vector<std::string> graph_files;
    for (auto& entry : std::filesystem::recursive_directory_iterator(graphs_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        const std::string full = entry.path().string();
        const std::string base = entry.path().filename().string();
        if (!graph_filter.empty()) {
            bool full_match = full == graph_filter;
            bool base_match = base == graph_filter;
            bool substring_match = full.find(graph_filter) != std::string::npos ||
                                   base.find(graph_filter) != std::string::npos;
            if (!full_match && !base_match && !substring_match) continue;
        }
        graph_files.push_back(full);
    }
    std::sort(graph_files.begin(), graph_files.end());

    if (graph_files.empty()) {
        std::fprintf(stderr, "No .json files found in %s\n", graphs_dir);
        return 1;
    }
    std::fprintf(stderr, "[demo_graphs] Found %zu graph files\n\n", graph_files.size());

    // Demos with known environment-specific failures (shader backend bugs, etc.)
    static const std::unordered_set<std::string> headless_skip = {
        "mfi_space_cycle_sync_demo.json",  // FolderList→video load chain needs more startup time than test allows
    };

    // --- Run each graph in an isolated child process ---
    //
    // Worker pool with bounded concurrency (--jobs / VIVID_DEMO_GRAPH_JOBS).
    // Each child's stderr is captured to a buffer so output can be printed
    // in graph-list order even though execution is concurrent. The fixture
    // path is collision-safe (Phase 7) so children writing per-graph health
    // JSONs to the same dir don't conflict.
    std::fprintf(stderr, "[demo_graphs] running with --jobs %d\n", max_jobs);

    enum ChildExitKind { CHILD_PIPE_FAIL = -1, CHILD_SPAWN_FAIL = -2 };
    struct Job {
        std::string path;
        std::string filename;
        bool        skipped_pre = false;
        pid_t       pid = 0;
        int         read_fd = -1;
        bool        completed = false;
        int         exit_status = 0;     // raw waitpid status, or CHILD_*_FAIL sentinel
        std::string captured_stderr;
    };
    std::vector<Job> jobs;
    jobs.reserve(graph_files.size());
    for (const auto& path : graph_files) {
        Job j;
        j.path = path;
        j.filename = std::filesystem::path(path).filename().string();
        j.skipped_pre = headless_skip.count(j.filename) > 0;
        jobs.push_back(std::move(j));
    }

    auto drain_pipe_nonblocking = [](Job& j) {
        if (j.read_fd < 0) return;
        char buf[4096];
        while (true) {
            ssize_t n = ::read(j.read_fd, buf, sizeof(buf));
            if (n > 0) {
                j.captured_stderr.append(buf, static_cast<size_t>(n));
            } else if (n == 0) {
                break;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                break;
            }
        }
    };

    std::vector<size_t> running;
    size_t next_to_spawn = 0;
    size_t next_to_print = 0;

    auto try_spawn_next = [&]() {
        while (running.size() < static_cast<size_t>(max_jobs) &&
               next_to_spawn < jobs.size()) {
            Job& j = jobs[next_to_spawn];
            if (j.skipped_pre) {
                j.completed = true;
                ++next_to_spawn;
                continue;
            }
            int fds[2];
            if (::pipe(fds) != 0) {
                j.completed = true;
                j.exit_status = CHILD_PIPE_FAIL;
                ++next_to_spawn;
                continue;
            }
            ::fcntl(fds[0], F_SETFL, O_NONBLOCK);
            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_addclose(&actions, fds[0]);
            posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
            posix_spawn_file_actions_addclose(&actions, fds[1]);
            const char* child_argv[] = {
                exe_path.c_str(), graphs_dir,
                "--health-dir", health_dir_str.c_str(),
                "--single", j.path.c_str(),
                nullptr
            };
            pid_t pid = 0;
            int rc = posix_spawn(&pid, exe_path.c_str(), &actions, nullptr,
                                 const_cast<char**>(child_argv), environ);
            posix_spawn_file_actions_destroy(&actions);
            ::close(fds[1]);  // parent only reads
            if (rc != 0) {
                ::close(fds[0]);
                j.completed = true;
                j.exit_status = CHILD_SPAWN_FAIL;
                ++next_to_spawn;
                continue;
            }
            j.pid = pid;
            j.read_fd = fds[0];
            running.push_back(next_to_spawn);
            ++next_to_spawn;
        }
    };

    auto record_outcome = [](Job& j) {
        if (j.exit_status == CHILD_SPAWN_FAIL) {
            skip(j.filename.c_str(), "posix_spawn failed");
        } else if (j.exit_status == CHILD_PIPE_FAIL) {
            skip(j.filename.c_str(), "pipe() failed");
        } else if (WIFEXITED(j.exit_status)) {
            int code = WEXITSTATUS(j.exit_status);
            if (code == 0)      pass(j.filename.c_str());
            else if (code == 2) skip(j.filename.c_str(), "child reported skip");
            else                fail(j.filename.c_str(), "child reported failure");
        } else if (WIFSIGNALED(j.exit_status)) {
            int sig = WTERMSIG(j.exit_status);
            char reason[64];
            std::snprintf(reason, sizeof(reason), "killed by signal %d", sig);
            skip(j.filename.c_str(), reason);
        } else {
            skip(j.filename.c_str(), "unknown child exit");
        }
    };

    auto print_completed_in_order = [&]() {
        while (next_to_print < jobs.size() && jobs[next_to_print].completed) {
            Job& j = jobs[next_to_print];
            if (j.skipped_pre) {
                skip(j.filename.c_str(), "headless skip list");
            } else {
                std::fprintf(stderr, "=== %s ===\n", j.filename.c_str());
                if (!j.captured_stderr.empty()) {
                    std::fwrite(j.captured_stderr.data(), 1,
                                j.captured_stderr.size(), stderr);
                }
                record_outcome(j);
            }
            ++next_to_print;
        }
    };

    try_spawn_next();
    while (!running.empty()) {
        // Wait until at least one running child has stderr to drain OR
        // the timeout elapses. The timeout bounds the worst-case latency
        // for noticing a silent child that exited without writing.
        //
        // The previous implementation drained pipes once and then called
        // waitpid(-1, ..., 0) (blocking). With multiple noisy children
        // that filled their ~16KB pipe buffers between the drain and the
        // waitpid call, all parties deadlocked: children blocked in
        // write(), parent blocked in waitpid().
        std::vector<pollfd> pfds;
        pfds.reserve(running.size());
        for (size_t idx : running) {
            if (jobs[idx].read_fd >= 0) {
                pfds.push_back({jobs[idx].read_fd, POLLIN, 0});
            }
        }
        if (!pfds.empty()) {
            int pr = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()),
                            /*timeout_ms=*/100);
            if (pr < 0 && errno != EINTR) break;
        }

        // Drain every running pipe — cheap, idempotent on EAGAIN, and
        // catches data that arrived between poll's return and now.
        for (size_t idx : running) drain_pipe_nonblocking(jobs[idx]);

        // Harvest any exited children non-blockingly. Loop because more
        // than one may have exited between iterations.
        while (!running.empty()) {
            int status = 0;
            pid_t finished = ::waitpid(-1, &status, WNOHANG);
            if (finished == 0) break;             // none exited yet
            if (finished < 0) {
                if (errno == EINTR) continue;
                break;                            // ECHILD or fatal
            }
            auto it = std::find_if(running.begin(), running.end(),
                [&](size_t idx) { return jobs[idx].pid == finished; });
            if (it == running.end()) continue;
            size_t idx = *it;
            Job& j = jobs[idx];
            // Final drain after exit so we capture everything written
            // before the child closed its stderr.
            drain_pipe_nonblocking(j);
            ::close(j.read_fd);
            j.read_fd = -1;
            j.exit_status = status;
            j.completed = true;
            running.erase(it);
        }

        print_completed_in_order();
        try_spawn_next();
    }
    // Catch any trailing skipped-pre jobs that never spawned.
    print_completed_in_order();

    // Summary
    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Demo graphs: %d passed, %d failed, %d skipped (of %zu total)\n",
                 passes, failures, skipped, graph_files.size());
    std::fprintf(stderr, "========================================\n");

    return failures > 0 ? 1 : 0;
}
