#include "operator_api/gpu_3d.h"
#include "operator_api/gpu_operator.h"
#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "common/gpu_util.h"
#include "ui/node_graph_util.h"
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

static void skip(const char* msg) {
    std::fprintf(stderr, "  SKIP: %s\n", msg);
    skipped++;
}

// ============================================================================
// Headless WebGPU init
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
        dev_desc.label = vivid::to_sv("Deformer Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[deformer_test] WebGPU error (%d): %.*s\n",
                             static_cast<int>(type), static_cast<int>(msg.length),
                             msg.data ? msg.data : "");
            };
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
    enc_desc.label = vivid::to_sv("Readback Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &enc_desc);

    WGPUTexelCopyTextureInfo src{};
    src.texture = texture;
    src.aspect  = WGPUTextureAspect_All;
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

static void tick_and_submit(vivid::Scheduler& sched, HeadlessGpu& gpu,
                            WGPUTextureFormat format, double time = 0.0) {
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Tick Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

    VividGpuState gpu_state{};
    gpu_state.device          = gpu.device;
    gpu_state.queue           = gpu.queue;
    gpu_state.command_encoder = encoder;
    gpu_state.output_format   = format;

    sched.tick(time, 0.016, 0, &gpu_state);

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
    while (!wd.done) wgpuDevicePoll(gpu.device, true, nullptr);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    // =====================================================================
    // GPU integration tests
    // =====================================================================

    std::string staging = "./.test_deformer_staging";
    std::filesystem::create_directories(staging);

    const char* dylibs[] = { "shape3d.dylib", "deformer.dylib", "render_3d.dylib" };
    bool all_dylibs = true;
    for (auto* name : dylibs) {
        if (std::filesystem::exists(name)) {
            std::filesystem::copy_file(name, staging + "/" + name,
                std::filesystem::copy_options::overwrite_existing);
        } else {
            all_dylibs = false;
        }
    }

    if (!all_dylibs) {
        skip("Required dylibs not found — skipping all tests");
        std::filesystem::remove_all(staging);
        std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                     failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
        return failures > 0 ? 1 : 0;
    }

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan() succeeds");
    check(registry.find("Shape3D") != nullptr, "Shape3D registered");
    check(registry.find("Deformer") != nullptr, "Deformer registered");
    check(registry.find("Render3D") != nullptr, "Render3D registered");

    std::fprintf(stderr, "\n=== GPU init ===\n");
    HeadlessGpu gpu;
    if (!gpu.init()) {
        skip("No GPU available — skipping GPU tests");
        std::filesystem::remove_all(staging);
        std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                     failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
        return failures > 0 ? 1 : 0;
    }
    check(gpu.device != nullptr, "Device created");

    auto get_center_pixel = [&](vivid::Scheduler& sched, uint32_t W, uint32_t H,
                                const char* render_node_id) -> std::vector<uint8_t> {
        auto& nodes = sched.nodes_mut();
        for (auto& ns : nodes) {
            if (ns.node_id == render_node_id && ns.gpu_texture) {
                return readback_texture(gpu.device, gpu.queue, ns.gpu_texture, W, H);
            }
        }
        return {};
    };

    // -----------------------------------------------------------------
    // GPU Test: Shape3D → Deformer(noise) → Render3D — visible
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D -> Deformer(noise) -> Render3D ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D", {{"shape", 1.0f}});  // sphere
        g.add_node("d1", "Deformer", {{"mode", 0.0f}, {"amplitude", 0.3f}});
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "d1", "scene");
        g.add_connection("d1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat, 1.0);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > 0 || gv > 0 || bv > 0, "center pixel is non-black (deformed visible)");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Deformer with amplitude=0 — shape unchanged
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Deformer amplitude=0 (pass-through) ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        // First: Shape3D → Render3D (no deformer)
        vivid::Graph g1;
        g1.add_node("s1", "Shape3D");
        g1.add_node("r1", "Render3D");
        g1.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched1;
        check(sched1.build(g1, registry), "build (no deformer) succeeds");
        sched1.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(sched1, gpu, kFormat);
        auto pixels1 = get_center_pixel(sched1, W, H, "r1");
        sched1.shutdown();

        // Second: Shape3D → Deformer(amplitude=0) → Render3D
        vivid::Graph g2;
        g2.add_node("s1", "Shape3D");
        g2.add_node("d1", "Deformer", {{"amplitude", 0.0f}});
        g2.add_node("r1", "Render3D");
        g2.add_connection("s1", "scene", "d1", "scene");
        g2.add_connection("d1", "scene", "r1", "scene");

        vivid::Scheduler sched2;
        check(sched2.build(g2, registry), "build (with deformer) succeeds");
        sched2.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(sched2, gpu, kFormat);
        auto pixels2 = get_center_pixel(sched2, W, H, "r1");
        sched2.shutdown();

        check(!pixels1.empty() && !pixels2.empty(), "both readbacks returned pixels");
        if (!pixels1.empty() && !pixels2.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            // Both should have same center pixel (amplitude=0 means no deformation)
            bool match = (pixels1[idx] == pixels2[idx] &&
                          pixels1[idx+1] == pixels2[idx+1] &&
                          pixels1[idx+2] == pixels2[idx+2]);
            std::fprintf(stderr, "  Without deformer: (%u, %u, %u)\n",
                         pixels1[idx], pixels1[idx+1], pixels1[idx+2]);
            std::fprintf(stderr, "  With deformer(amp=0): (%u, %u, %u)\n",
                         pixels2[idx], pixels2[idx+1], pixels2[idx+2]);
            check(match, "center pixel matches (amplitude=0 = no change)");
        }
    }

    // -----------------------------------------------------------------
    // GPU Test: Shape3D → Deformer(sine) → Render3D — visible
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D -> Deformer(sine) -> Render3D ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D", {{"shape", 3.0f}});  // plane
        g.add_node("d1", "Deformer", {{"mode", 1.0f}, {"amplitude", 0.5f}, {"frequency", 5.0f}});
        g.add_node("r1", "Render3D", {{"cam_y", 3.0f}});
        g.add_connection("s1", "scene", "d1", "scene");
        g.add_connection("d1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat, 0.5);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > 0 || gv > 0 || bv > 0, "center pixel is non-black (sine deform visible)");
        }

        sched.shutdown();
    }

    // Cleanup
    gpu.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
    return failures > 0 ? 1 : 0;
}
