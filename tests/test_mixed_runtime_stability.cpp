#include "runtime/audio_engine.h"
#include "runtime/builtin_operators.h"
#include "runtime/cadence_bridge.h"
#include "runtime/compiled_graph.h"
#include "runtime/graph.h"
#include "runtime/operator_registry.h"
#include "runtime/scheduler.h"
#include "common/gpu_util.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <vector>

static int failures = 0;
static int skipped = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void skip(const char* msg) {
    std::fprintf(stderr, "  SKIP: %s\n", msg);
    skipped++;
}

struct HeadlessGpu {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;

    bool init() {
        WGPUInstanceDescriptor inst_desc{};
        instance = wgpuCreateInstance(&inst_desc);
        if (!instance) return false;

        struct AdapterData { WGPUAdapter adapter = nullptr; bool done = false; } ad;
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
        opts.compatibleSurface = nullptr;
        opts.powerPreference = WGPUPowerPreference_HighPerformance;
        wgpuInstanceRequestAdapter(instance, &opts, acb);
        if (!ad.done || !ad.adapter) return false;
        adapter = ad.adapter;

        struct DeviceData { WGPUDevice device = nullptr; bool done = false; } dd;
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
        dev_desc.label = vivid::to_sv("Phase6 Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
                std::fprintf(stderr, "[mixed-runtime] WebGPU error (%d): %.*s\n",
                             static_cast<int>(type), static_cast<int>(message.length),
                             message.data ? message.data : "");
            };
        wgpuAdapterRequestDevice(adapter, &dev_desc, dcb);
        if (!dd.done || !dd.device) return false;
        device = dd.device;
        queue = wgpuDeviceGetQueue(device);
        return true;
    }

    void shutdown() {
        if (queue) { wgpuQueueRelease(queue); queue = nullptr; }
        if (device) { wgpuDeviceRelease(device); device = nullptr; }
        if (adapter) { wgpuAdapterRelease(adapter); adapter = nullptr; }
        if (instance) { wgpuInstanceRelease(instance); instance = nullptr; }
    }
};

static void tick_with_gpu(vivid::Scheduler& scheduler, HeadlessGpu& gpu,
                          uint64_t frame, double time, double delta,
                          WGPUTextureFormat format) {
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Phase6 Tick Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

    VividGpuContext gpu_state{};
    gpu_state.device = gpu.device;
    gpu_state.queue = gpu.queue;
    gpu_state.command_encoder = encoder;
    gpu_state.output_format = format;

    scheduler.tick(time, delta, frame, &gpu_state);

    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.label = vivid::to_sv("Phase6 Tick Commands");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(gpu.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    bool soak = false;
    if (argc > 1) build_dir = argv[1];
    if (argc > 2 && std::string(argv[2]) == "soak") soak = true;

    const std::string staging = build_dir + "/.test_mixed_runtime_stability_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/gpu_fill_op.dylib", staging + "/gpu_fill_op.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/lfo.dylib", staging + "/lfo.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/oscillator.dylib", staging + "/oscillator.dylib",
                               std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Mixed Runtime Stability%s ===\n\n",
                 soak ? " (soak)" : "");

    HeadlessGpu gpu;
    if (!gpu.init()) {
        skip("headless WebGPU unavailable");
        std::fprintf(stderr, "\n=== SKIPPED (%d skips) ===\n\n", skipped);
        return 0;
    }

    vivid::OperatorRegistry registry;
    register_builtin_operators(registry);
    check(registry.scan(staging.c_str()), "registry.scan(gpu_fill_op)");

    vivid::Graph graph;
    check(graph.load((build_dir + "/test_mixed_runtime_stability.json").c_str()),
          "graph.load(test_mixed_runtime_stability.json)");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");
    scheduler.allocate_gpu_textures(gpu.device, 64, 64, WGPUTextureFormat_RGBA8Unorm);

    vivid::AudioEngine audio_engine;
    check(audio_engine.build(graph, registry, scheduler), "audio_engine.build()");

    const int iterations = soak ? 1200 : 240;
    const uint32_t underruns_before = audio_engine.underrun_count();
    double time = 0.0;

    for (int i = 0; i < iterations; ++i) {
        scheduler.cadence_bridge().push_to_audio(*scheduler.compiled_graph());
        scheduler.cadence_bridge().update_sources(time, *scheduler.compiled_graph());
        tick_with_gpu(scheduler, gpu, static_cast<uint64_t>(i), time, 1.0 / 60.0,
                      WGPUTextureFormat_RGBA8Unorm);
        float output[vivid::AudioEngine::kBufferSize * 2] = {};
        audio_engine.process_audio_for_test(output, vivid::AudioEngine::kBufferSize);
        scheduler.cadence_bridge().pull_from_audio(*scheduler.compiled_graph());

        for (const auto& node : scheduler.compiled_graph()->nodes) {
            if (node.errored) {
                std::string msg = "scheduler node errored during mixed-runtime loop: " + node.node_id;
                check(false, msg.c_str());
                break;
            }
        }

        if ((i % 40) == 0) {
            const auto* fill = scheduler.compiled_graph()->find_node("fill");
            check(fill != nullptr, "fill node present during mixed-runtime loop");
            if (fill) check(fill->gpu_texture != nullptr, "fill node keeps an allocated gpu texture");
        }
        time += 1.0 / 60.0;
    }

    const uint32_t underruns_after = audio_engine.underrun_count();
    check(underruns_after == underruns_before,
          "headless mixed-runtime stability run does not grow underrun count");

    audio_engine.shutdown();
    scheduler.shutdown();
    gpu.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures, %d skips) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
    return failures == 0 ? 0 : 1;
}
