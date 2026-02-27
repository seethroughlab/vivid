#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/builtin_operators.h"
#include "operator_api/gpu_operator.h"
#include "common/gpu_util.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

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
            [](WGPUDevice const*, WGPUErrorType, WGPUStringView, void*, void*) {};
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

// Helper: run one scheduler tick with a GPU command encoder, then submit + wait.
static void tick_gpu(vivid::Scheduler& sched, HeadlessGpu& gpu,
                     WGPUTextureFormat format, double time, uint64_t frame) {
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Tick Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

    VividGpuState gpu_state{};
    gpu_state.device          = gpu.device;
    gpu_state.queue           = gpu.queue;
    gpu_state.command_encoder = encoder;
    gpu_state.output_format   = format;

    sched.tick(time, 0.016, frame, &gpu_state);

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
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: test_demo_graphs <graphs_dir>\n");
        return 1;
    }
    const char* graphs_dir = argv[1];
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    // --- One-time GPU setup ---
    HeadlessGpu gpu;
    bool have_gpu = gpu.init();
    if (have_gpu) {
        std::fprintf(stderr, "[demo_graphs] GPU available\n");
    } else {
        std::fprintf(stderr, "[demo_graphs] No GPU — GPU-only graphs will be skipped\n");
    }

    // --- Operator registry (scan build dir for all .dylib) ---
    vivid::OperatorRegistry registry;
    registry.scan_deferred(".");
    register_builtin_operators(registry);

    // --- Collect graph files ---
    std::vector<std::string> graph_files;
    for (auto& entry : std::filesystem::directory_iterator(graphs_dir)) {
        if (entry.path().extension() == ".json") {
            graph_files.push_back(entry.path().string());
        }
    }
    std::sort(graph_files.begin(), graph_files.end());

    if (graph_files.empty()) {
        std::fprintf(stderr, "No .json files found in %s\n", graphs_dir);
        gpu.shutdown();
        return 1;
    }
    std::fprintf(stderr, "[demo_graphs] Found %zu graph files\n\n", graph_files.size());

    // --- Iterate graphs ---
    for (const auto& path : graph_files) {
        std::string filename = std::filesystem::path(path).filename().string();
        std::fprintf(stderr, "=== %s ===\n", filename.c_str());

        // Load graph
        vivid::Graph graph;
        if (!graph.load(path.c_str())) {
            fail(filename.c_str(), "graph.load() failed");
            continue;
        }

        // Load operators needed by this graph
        registry.load_for_graph(graph);

        // Build scheduler
        vivid::Scheduler sched;
        if (!sched.build(graph, registry)) {
            fail(filename.c_str(), "scheduler.build() failed");
            continue;
        }

        bool use_gpu = sched.has_gpu_operators();
        bool use_audio = sched.has_audio_operators();

        // GPU setup
        if (use_gpu && !have_gpu) {
            skip(filename.c_str(), "needs GPU");
            sched.shutdown();
            continue;
        }
        if (use_gpu) {
            sched.allocate_gpu_textures(gpu.device, 64, 64, kFormat);
        }

        // Audio setup
        vivid::AudioEngine* audio = nullptr;
        if (use_audio) {
            audio = new vivid::AudioEngine();
            audio->build(graph, registry, sched);
            audio->start(true);  // null device
        }

        // Tick 5 frames
        bool tick_ok = true;
        for (uint64_t frame = 0; frame < 5; ++frame) {
            double time = frame * 0.016;
            if (use_gpu) {
                tick_gpu(sched, gpu, kFormat, time, frame);
            } else {
                sched.tick(time, 0.016, frame);
            }
            if (audio) {
                audio->push_params(sched);
            }
        }

        // Cleanup
        if (audio) {
            audio->shutdown();
            delete audio;
        }
        sched.shutdown();

        if (tick_ok) {
            pass(filename.c_str());
        }
    }

    gpu.shutdown();

    // Summary
    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Demo graphs: %d passed, %d failed, %d skipped (of %zu total)\n",
                 passes, failures, skipped, graph_files.size());
    std::fprintf(stderr, "========================================\n");

    return failures > 0 ? 1 : 0;
}
