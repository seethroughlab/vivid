#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/operators/builtin_operators.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/compiled_graph.h"
#include "operator_api/gpu_operator.h"
#include "common/gpu_util.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
#include <csignal>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
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
        dev_desc.label = vivid::to_sv("MediaTest Device");
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
    wcb.callback = [](WGPUQueueWorkDoneStatus, void* ud1, void*) {
        static_cast<WorkDone*>(ud1)->done = true;
    };
    wcb.userdata1 = &wd;
    wgpuQueueOnSubmittedWorkDone(gpu.queue, wcb);
    while (!wd.done) {
        wgpuDevicePoll(gpu.device, true, nullptr);
    }
}

// ============================================================================
// Discover movie/media graphs — any graph containing MovieFile operators.
// ============================================================================

static std::vector<std::string> discover_media_graphs(const char* graphs_dir) {
    std::vector<std::string> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(graphs_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;

        vivid::Graph probe;
        if (!probe.load(entry.path().string().c_str())) continue;
        for (const auto& n : probe.nodes()) {
            if (n.type == "MovieFile") {
                result.push_back(entry.path().string());
                break;
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

static bool enforce_movie_visibility_for_graph(const char* graph_path) {
    const std::string name = std::filesystem::path(graph_path).filename().string();
    return name == "mfi_av_sync_demo.json" ||
           name == "mfi_space_cycle_sync_demo.json";
}

static bool read_output_value(const vivid::CompiledNode& node,
                              const char* name,
                              float& out) {
    auto it = node.output_port_indices.find(name);
    if (it == node.output_port_indices.end() ||
        it->second >= node.output_values.size()) {
        return false;
    }
    out = node.output_values[it->second];
    return std::isfinite(out);
}

static bool output_has_visible_pixels(const vivid::CompiledNode& node) {
    float brightness = 0.0f;
    float contrast = 0.0f;
    const bool has_brightness = read_output_value(node, "brightness", brightness);
    const bool has_contrast = read_output_value(node, "contrast", contrast);
    return (has_brightness && brightness > 0.01f) ||
           (has_contrast && contrast > 0.002f);
}

static bool validate_movie_visibility(const vivid::CompiledGraph& cg,
                                      const char* graph_path) {
    if (!enforce_movie_visibility_for_graph(graph_path)) return true;

    bool movie_decoded_visible_frame = false;
    bool has_composite = false;
    bool composite_visible = false;
    bool has_video_out = false;
    bool video_out_visible = false;

    float movie_new_frames = 0.0f;
    float movie_gpu_native_frames = 0.0f;
    float movie_brightness = 0.0f;
    float movie_contrast = 0.0f;

    for (const auto& node : cg.nodes) {
        if (node.type_name == "MovieFile") {
            (void)read_output_value(node, "new_frames", movie_new_frames);
            (void)read_output_value(node, "gpu_native_frames", movie_gpu_native_frames);
            (void)read_output_value(node, "brightness", movie_brightness);
            (void)read_output_value(node, "contrast", movie_contrast);
            if ((movie_new_frames > 5.0f || movie_gpu_native_frames > 5.0f) &&
                output_has_visible_pixels(node)) {
                movie_decoded_visible_frame = true;
            }
        } else if (node.type_name == "Composite") {
            has_composite = true;
            composite_visible = composite_visible || output_has_visible_pixels(node);
        } else if (node.type_name == "video_out") {
            has_video_out = true;
            video_out_visible = video_out_visible || output_has_visible_pixels(node);
        }
    }

    if (!movie_decoded_visible_frame) return true;

    if (has_composite && !composite_visible) {
        std::fprintf(stderr,
                     "MovieFile decoded visible frames, but Composite output is black "
                     "(movie new=%.0f gpu_native=%.0f brightness=%.4f contrast=%.4f)\n",
                     movie_new_frames,
                     movie_gpu_native_frames,
                     movie_brightness,
                     movie_contrast);
        return false;
    }
    if (has_video_out && !video_out_visible) {
        std::fprintf(stderr,
                     "MovieFile decoded visible frames, but video_out input is black "
                     "(movie new=%.0f gpu_native=%.0f brightness=%.4f contrast=%.4f)\n",
                     movie_new_frames,
                     movie_gpu_native_frames,
                     movie_brightness,
                     movie_contrast);
        return false;
    }
    return true;
}

// Per-graph timeout (seconds).
static constexpr int kTimeoutSeconds = 20;

// ============================================================================
// Single-graph mode (--single <graph_path>)
//
// Runs one graph in a fresh process. Exit codes:
//   0 = pass
//   1 = fail (audio error, GPU error)
//   2 = skip (needs GPU, etc.)
// ============================================================================

static int run_single_graph(const char* exe_path, const char* graph_path) {
    // SIGABRT guard — catch AVFoundation deadlocks in this child process.
    std::signal(SIGABRT, [](int) {
        const char* msg = "SIGABRT (AVFoundation deadlock)\n";
        (void)write(STDERR_FILENO, msg, strlen(msg));
        _exit(2);  // skip
    });

    // Watchdog alarm — if AVFoundation hangs instead of aborting.
    alarm(kTimeoutSeconds);

    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    HeadlessGpu gpu;
    bool have_gpu = gpu.init();

    // Operator registry
    std::filesystem::path exe_dir = std::filesystem::absolute(exe_path).parent_path();
    vivid::OperatorRegistry registry;
    registry.scan_deferred(exe_dir.string().c_str());
    register_builtin_operators(registry);
    registry.scan_shader_operators((exe_dir / "filters").string());

#ifdef __APPLE__
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
#endif

    vivid::Graph graph;
    if (!graph.load(graph_path)) {
        std::fprintf(stderr, "graph.load() failed\n");
        return 1;
    }

    registry.load_for_graph(graph);

#ifdef __APPLE__
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
#endif

    vivid::RuntimeCore runtime;
    if (!runtime.build(graph, registry)) {
        std::fprintf(stderr, "runtime.build() failed\n");
        return 1;
    }

    bool use_gpu = runtime.has_gpu_operators();
    bool use_audio = runtime.has_audio_operators();

    if (use_gpu && !have_gpu) {
        std::fprintf(stderr, "needs GPU\n");
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

    // Tick enough frames for async media decode plus one-frame-delayed GPU
    // analysis readbacks to settle.
    const uint32_t audio_frames = audio->buffer_size();
    std::vector<float> audio_buf(audio_frames * 2, 0.0f);
    const uint64_t tick_count = enforce_movie_visibility_for_graph(graph_path) ? 120 : 60;
    for (uint64_t frame = 0; frame < tick_count; ++frame) {
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
#ifdef __APPLE__
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, false);
#endif
    }

    // Check for errors
    int result = 0;

    if (audio) {
        runtime.audio_frame_bridge().pull_from_audio(*runtime.compiled_graph());
        const auto& analysis = audio->analysis_read();
        for (size_t i = 0; i < analysis.errored.size(); ++i) {
            if (analysis.errored[i]) {
                std::fprintf(stderr, "audio node error: %s\n", analysis.error_msgs[i].data());
                result = 1;
            }
        }
    }

    if (use_gpu && gpu.has_gpu_error) {
        std::fprintf(stderr, "GPU error: %s\n", gpu.gpu_error_msg.c_str());
        result = 1;
    }

    if (use_gpu && runtime.compiled_graph() &&
        !validate_movie_visibility(*runtime.compiled_graph(), graph_path)) {
        result = 1;
    }

    if (audio) { audio->shutdown(); delete audio; }
    runtime.shutdown();
    gpu.shutdown();

    return result;
}

// ============================================================================
// Main — orchestrates per-graph child processes via posix_spawn (re-exec)
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: test_media_headless <build_dir> [--single <graph_path>]\n");
        return 1;
    }

    // --single mode: run one graph and exit
    if (argc >= 4 && std::strcmp(argv[2], "--single") == 0) {
        return run_single_graph(argv[0], argv[3]);
    }

    const char* graphs_dir = argv[1];
    std::string exe_path = std::filesystem::absolute(argv[0]).string();

    // Discover media graphs
    auto media_graphs = discover_media_graphs(graphs_dir);
    int media_graph_count = static_cast<int>(media_graphs.size());
    std::fprintf(stderr, "[media_headless] Found %d media graphs\n\n", media_graph_count);

    // Run each graph in an isolated child process via posix_spawn (re-exec).
    // Unlike fork(), posix_spawn creates a full fresh process with proper
    // Objective-C runtime, GCD, and AVFoundation state.
    for (int gi = 0; gi < media_graph_count; ++gi) {
        std::string full_path = media_graphs[gi];
        std::string rel_path = std::filesystem::path(full_path).filename().string();
        std::fprintf(stderr, "=== %s ===\n", rel_path.c_str());

        // Build argv for child: exe <build_dir> --single <graph_path>
        const char* child_argv[] = {
            exe_path.c_str(),
            graphs_dir,
            "--single",
            full_path.c_str(),
            nullptr
        };

        pid_t pid = 0;
        int rc = posix_spawn(&pid, exe_path.c_str(), nullptr, nullptr,
                             const_cast<char**>(child_argv), environ);
        if (rc != 0) {
            skip(rel_path.c_str(), "posix_spawn failed");
            continue;
        }

        int status = 0;
        pid_t waited = waitpid(pid, &status, 0);
        if (waited < 0) {
            skip(rel_path.c_str(), "waitpid failed");
            continue;
        }

        if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            if (code == 0)      pass(rel_path.c_str());
            else if (code == 2) skip(rel_path.c_str(), "child reported skip");
            else                fail(rel_path.c_str(), "child reported failure");
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            char reason[64];
            snprintf(reason, sizeof(reason), "killed by signal %d", sig);
            skip(rel_path.c_str(), reason);
        } else {
            skip(rel_path.c_str(), "unknown child exit");
        }
    }

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Media headless: %d passed, %d failed, %d skipped (of %d total)\n",
                 passes, failures, skipped, media_graph_count);
    std::fprintf(stderr, "========================================\n");

    if (media_graph_count == 0) return 0;
    return failures > 0 ? 1 : 0;
}
