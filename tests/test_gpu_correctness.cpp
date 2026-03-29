// GPU output correctness tests.
// Verifies that GPU operators produce expected visual properties using
// analyze_frame() and compute_motion() — property-based, no golden files.

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/runtime_core.h"
#include "runtime/compiled_graph.h"
#include "runtime/output_analyzer.h"
#include "operator_api/gpu_operator.h"
#include "common/gpu_util.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <filesystem>

// ============================================================================
// Test infrastructure
// ============================================================================

static int failures = 0;
static int skipped  = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_float(float actual, float expected, float tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (expected %.4f, got %.4f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%.4f)\n", msg, actual);
    }
}

static void skip(const char* msg) {
    std::fprintf(stderr, "  SKIP: %s\n", msg);
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
        dev_desc.label = vivid::to_sv("Test Device");
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
        if (queue)    { wgpuQueueRelease(queue);    queue    = nullptr; }
        if (device)   { wgpuDeviceRelease(device);  device   = nullptr; }
        if (adapter)  { wgpuAdapterRelease(adapter); adapter = nullptr; }
        if (instance) { wgpuInstanceRelease(instance); instance = nullptr; }
    }
};

// ============================================================================
// GPU readback utility
// ============================================================================

static const uint32_t kRowAlignment = 256;

static uint32_t aligned_bytes_per_row(uint32_t width) {
    uint32_t unpadded = width * 4;
    return (unpadded + kRowAlignment - 1) & ~(kRowAlignment - 1);
}

static std::vector<uint8_t> readback_texture(WGPUDevice device, WGPUQueue queue,
                                              WGPUTexture texture,
                                              uint32_t width, uint32_t height) {
    uint32_t padded_row = aligned_bytes_per_row(width);
    uint64_t buf_size = static_cast<uint64_t>(padded_row) * height;

    WGPUBufferDescriptor buf_desc{};
    buf_desc.label = vivid::to_sv("Readback Buffer");
    buf_desc.size  = buf_size;
    buf_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(device, &buf_desc);

    WGPUCommandEncoderDescriptor enc_desc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &enc_desc);

    WGPUTexelCopyTextureInfo src{};
    src.texture = texture;
    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = staging;
    dst.layout.bytesPerRow = padded_row;
    dst.layout.rowsPerImage = height;
    WGPUExtent3D extent = { width, height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

    WGPUCommandBufferDescriptor cmd_desc{};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(queue, 1, &cmd);
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
    wgpuQueueOnSubmittedWorkDone(queue, wcb);
    while (!wd.done) wgpuDevicePoll(device, true, nullptr);

    struct MapData { bool done = false; WGPUMapAsyncStatus status; };
    MapData md;
    WGPUBufferMapCallbackInfo mcb{};
    mcb.mode = WGPUCallbackMode_AllowSpontaneous;
    mcb.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* ud1, void*) {
        auto* d = static_cast<MapData*>(ud1);
        d->status = status;
        d->done = true;
    };
    mcb.userdata1 = &md;
    wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, buf_size, mcb);
    while (!md.done) wgpuDevicePoll(device, true, nullptr);

    std::vector<uint8_t> pixels;
    if (md.status == WGPUMapAsyncStatus_Success) {
        const uint8_t* mapped = static_cast<const uint8_t*>(
            wgpuBufferGetConstMappedRange(staging, 0, buf_size));
        uint32_t dense_row = width * 4;
        pixels.resize(static_cast<size_t>(dense_row) * height);
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(pixels.data() + y * dense_row, mapped + y * padded_row, dense_row);
        wgpuBufferUnmap(staging);
    }
    wgpuBufferRelease(staging);
    return pixels;
}

static void tick_and_submit(vivid::RuntimeCore& runtime, HeadlessGpu& gpu,
                            WGPUTextureFormat format, double time = 0.0, uint64_t frame = 0) {
    WGPUCommandEncoderDescriptor enc_desc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

    VividGpuContext gpu_state{};
    gpu_state.device          = gpu.device;
    gpu_state.queue           = gpu.queue;
    gpu_state.command_encoder = encoder;
    gpu_state.output_format   = format;

    runtime.tick(time, 0.016, frame, &gpu_state);

    WGPUCommandBufferDescriptor cmd_desc{};
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
    while (!wd.done) wgpuDevicePoll(gpu.device, true, nullptr);
}

// ============================================================================
// Tests
// ============================================================================

int main() {
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;
    static constexpr uint32_t W = 64, H = 64;

    std::fprintf(stderr, "\n=== Test: GPU Output Correctness ===\n");

    HeadlessGpu gpu;
    if (!gpu.init()) {
        skip("No GPU available — skipping all GPU correctness tests");
        std::fprintf(stderr, "\n0 passed, 0 failed, 1 skipped\n");
        return 0;
    }

    // Set up operators
    std::string staging = "./.test_gpu_correctness_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    const char* ops[] = {"gpu_fill_op", "shape", "noise"};
    for (const char* op : ops) {
        std::string src = std::string(".") + "/" + op + ".dylib";
        std::string dst = staging + "/" + op + ".dylib";
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
        else
            std::fprintf(stderr, "  WARN: %s.dylib not found\n", op);
    }

    vivid::OperatorRegistry registry;
    registry.scan(staging.c_str());

    // =================================================================
    // Test 1: Red fill — brightness and contrast
    // =================================================================
    {
        std::fprintf(stderr, "\n--- Red fill: brightness/contrast ---\n");
        vivid::Graph g;
        g.add_node("fill", "GpuFillOp", {{"r", 1.0f}, {"g", 0.0f}, {"b", 0.0f}});

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build red fill");
        runtime.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(runtime, gpu, kFormat);

        auto pixels = readback_texture(gpu.device, gpu.queue,
                                        runtime.compiled_graph()->nodes[0].gpu->texture, W, H);
        check(!pixels.empty(), "readback ok");

        if (!pixels.empty()) {
            auto vm = vivid::analyze_frame(pixels.data(), W, H);
            std::fprintf(stderr, "  brightness=%.4f contrast=%.4f\n",
                         vm.mean_brightness, vm.contrast);
            // Red luminance: 0.2126 * 1.0 + 0.7152 * 0.0 + 0.0722 * 0.0 = 0.2126
            check_float(vm.mean_brightness, 0.2126f, 0.02f, "red fill brightness ≈ 0.2126");
            check_float(vm.contrast, 0.0f, 0.01f, "solid fill has zero contrast");
        }
        runtime.shutdown();
    }

    // =================================================================
    // Test 2: White fill — brightness near 1.0
    // =================================================================
    {
        std::fprintf(stderr, "\n--- White fill: brightness ---\n");
        vivid::Graph g;
        g.add_node("fill", "GpuFillOp", {{"r", 1.0f}, {"g", 1.0f}, {"b", 1.0f}});

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build white fill");
        runtime.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(runtime, gpu, kFormat);

        auto pixels = readback_texture(gpu.device, gpu.queue,
                                        runtime.compiled_graph()->nodes[0].gpu->texture, W, H);
        if (!pixels.empty()) {
            auto vm = vivid::analyze_frame(pixels.data(), W, H);
            std::fprintf(stderr, "  brightness=%.4f\n", vm.mean_brightness);
            check_float(vm.mean_brightness, 1.0f, 0.02f, "white fill brightness ≈ 1.0");
        }
        runtime.shutdown();
    }

    // =================================================================
    // Test 3: Shape — non-zero brightness and contrast
    // =================================================================
    if (registry.find("Shape")) {
        std::fprintf(stderr, "\n--- Shape: geometry produces brightness/contrast ---\n");
        vivid::Graph g;
        g.add_node("shape", "Shape", {});

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build shape");
        runtime.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(runtime, gpu, kFormat);

        auto pixels = readback_texture(gpu.device, gpu.queue,
                                        runtime.compiled_graph()->nodes[0].gpu->texture, W, H);
        if (!pixels.empty()) {
            auto vm = vivid::analyze_frame(pixels.data(), W, H);
            std::fprintf(stderr, "  brightness=%.4f contrast=%.4f\n",
                         vm.mean_brightness, vm.contrast);
            check(vm.mean_brightness > 0.05f, "shape: non-trivial brightness");
            check(vm.contrast > 0.0f, "shape: non-zero contrast (geometry vs background)");
        }
        runtime.shutdown();
    }

    // =================================================================
    // Test 4: GPU Noise — variance (high contrast)
    // =================================================================
    if (registry.find("Noise")) {
        std::fprintf(stderr, "\n--- GPU Noise: visual variance ---\n");
        vivid::Graph g;
        g.add_node("noise", "Noise", {});

        vivid::RuntimeCore runtime;
        if (runtime.build(g, registry)) {
            runtime.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
            tick_and_submit(runtime, gpu, kFormat);

            auto pixels = readback_texture(gpu.device, gpu.queue,
                                            runtime.compiled_graph()->nodes[0].gpu->texture, W, H);
            if (!pixels.empty()) {
                auto vm = vivid::analyze_frame(pixels.data(), W, H);
                std::fprintf(stderr, "  brightness=%.4f contrast=%.4f\n",
                             vm.mean_brightness, vm.contrast);
                check(vm.contrast > 0.05f, "noise: high contrast (varied pixels)");
                check(vm.mean_brightness > 0.1f && vm.mean_brightness < 0.9f,
                      "noise: brightness in mid-range");
            }
            runtime.shutdown();
        } else {
            skip("Noise build failed (may be GPU-only)");
        }
    }

    // =================================================================
    // Test 5: Param change produces motion
    // =================================================================
    {
        std::fprintf(stderr, "\n--- Param change: red→green produces motion ---\n");
        vivid::Graph g;
        g.add_node("fill", "GpuFillOp", {{"r", 1.0f}, {"g", 0.0f}, {"b", 0.0f}});

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build for motion test");
        runtime.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        // Frame A: red
        tick_and_submit(runtime, gpu, kFormat);
        auto pixels_a = readback_texture(gpu.device, gpu.queue,
                                          runtime.compiled_graph()->nodes[0].gpu->texture, W, H);

        // Change to green
        auto* ns = runtime.compiled_graph()->find_node("fill");
        if (ns) {
            auto pi_r = ns->param_indices.find("r");
            auto pi_g = ns->param_indices.find("g");
            if (pi_r != ns->param_indices.end()) ns->param_values[pi_r->second] = 0.0f;
            if (pi_g != ns->param_indices.end()) ns->param_values[pi_g->second] = 1.0f;
            ns->dirty = true;
        }

        // Frame B: green
        tick_and_submit(runtime, gpu, kFormat, 0.016, 1);
        auto pixels_b = readback_texture(gpu.device, gpu.queue,
                                          runtime.compiled_graph()->nodes[0].gpu->texture, W, H);

        if (!pixels_a.empty() && !pixels_b.empty()) {
            float motion = vivid::compute_motion(pixels_a.data(), pixels_b.data(), W, H);
            std::fprintf(stderr, "  motion=%.4f\n", motion);
            check(motion > 0.3f, "red→green produces significant motion");
        }
        runtime.shutdown();
    }

    // Clean up
    gpu.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures, %d skipped) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
    return failures > 0 ? 1 : 0;
}
