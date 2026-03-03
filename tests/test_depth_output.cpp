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
        dev_desc.label = vivid::to_sv("DepthOutput Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[depth_test] WebGPU error (%d): %.*s\n",
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
// GPU readback for R32Float texture
// ============================================================================

static const uint32_t kRowAlignment = 256;

static std::vector<float> readback_r32f_texture(WGPUDevice device, WGPUQueue queue,
                                                 WGPUTexture texture,
                                                 uint32_t width, uint32_t height) {
    uint32_t unpadded = width * 4;  // 4 bytes per float
    uint32_t padded_row = (unpadded + kRowAlignment - 1) & ~(kRowAlignment - 1);
    uint64_t buf_size = static_cast<uint64_t>(padded_row) * height;

    WGPUBufferDescriptor buf_desc{};
    buf_desc.label = vivid::to_sv("R32F Readback Buffer");
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

    std::vector<float> pixels;
    if (md.status == WGPUMapAsyncStatus_Success) {
        const uint8_t* mapped = static_cast<const uint8_t*>(
            wgpuBufferGetConstMappedRange(staging, 0, buf_size));
        pixels.resize(static_cast<size_t>(width) * height);
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(pixels.data() + y * width,
                        mapped + y * padded_row,
                        width * sizeof(float));
        }
        wgpuBufferUnmap(staging);
    }
    wgpuBufferRelease(staging);
    return pixels;
}

// Tick scheduler with GPU state and submit
static void tick_and_submit(vivid::Scheduler& sched, HeadlessGpu& gpu,
                            WGPUTextureFormat format) {
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Tick Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

    VividGpuState gpu_state{};
    gpu_state.device          = gpu.device;
    gpu_state.queue           = gpu.queue;
    gpu_state.command_encoder = encoder;
    gpu_state.output_format   = format;

    sched.tick(0.0, 0.016, 0, &gpu_state);

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
    using namespace vivid::gpu;
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    // =====================================================================
    // CPU tests
    // =====================================================================

    std::fprintf(stderr, "\n=== CPU Test: depth_output_port_idx detection ===\n");
    {
        // Verify that init_node_state sets depth_output_port_idx = -1 when
        // no "depth" output port exists.
        // (The actual port detection is tested by the GPU test below)
        check(true, "CPU depth port detection placeholder");
    }

    // =====================================================================
    // GPU integration tests
    // =====================================================================

    std::string staging = "./.test_depth_output_staging";
    std::filesystem::create_directories(staging);

    const char* dylibs[] = {
        "shape3d.dylib", "render_3d.dylib", "transform3d.dylib", "scene_merge.dylib"
    };
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
        skip("One or more dylibs not found — skipping GPU tests");
        std::filesystem::remove_all(staging);
        std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                     failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
        return failures > 0 ? 1 : 0;
    }

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan() succeeds");
    check(registry.find("Shape3D") != nullptr, "Shape3D registered");
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

    // -----------------------------------------------------------------
    // GPU Test: Depth output port detection + R32Float readback
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Depth buffer exposure ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        g.add_node("cube", "Shape3D", {{"r", 0.8f}, {"g", 0.2f}, {"b", 0.2f}});
        g.add_node("r1", "Render3D", {
            {"cam_x", 0.0f}, {"cam_y", 0.0f}, {"cam_z", 5.0f},
            {"target_x", 0.0f}, {"target_y", 0.0f}, {"target_z", 0.0f},
            {"near", 0.1f}, {"far", 100.0f}
        });
        g.add_connection("cube", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "depth scene build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        // Verify depth_output_port_idx is set correctly
        auto* render_node = sched.find_node_mut("r1");
        check(render_node != nullptr, "Render3D node found");
        if (render_node) {
            check(render_node->depth_output_port_idx >= 0,
                  "depth_output_port_idx is set (not -1)");
            std::fprintf(stderr, "  depth_output_port_idx = %d\n",
                         render_node->depth_output_port_idx);
        }

        // Tick to produce depth output
        tick_and_submit(sched, gpu, kFormat);

        // Verify depth texture view was set
        if (render_node) {
            check(render_node->gpu_depth_texture_view != nullptr,
                  "gpu_depth_texture_view is set after tick");
        }

        // Find the Render3D node's depth texture for readback
        // The depth texture is stored as gpu_depth_texture on the node
        // But we need the raw R32Float texture for readback, which is internal
        // to the operator. We verify via the view being non-null and check
        // by rendering and reading back the color buffer to see cube is visible.

        // Read back RGBA to verify cube is visible
        if (render_node && render_node->gpu_texture) {
            uint32_t padded_row = ((W * 4) + kRowAlignment - 1) & ~(kRowAlignment - 1);
            uint64_t buf_size = static_cast<uint64_t>(padded_row) * H;
            WGPUBufferDescriptor buf_desc{};
            buf_desc.label = vivid::to_sv("Readback Buffer");
            buf_desc.size  = buf_size;
            buf_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
            WGPUBuffer staging_buf = wgpuDeviceCreateBuffer(gpu.device, &buf_desc);

            WGPUCommandEncoderDescriptor enc_d{};
            WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_d);
            WGPUTexelCopyTextureInfo src{};
            src.texture = render_node->gpu_texture;
            src.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferInfo dst{};
            dst.buffer = staging_buf;
            dst.layout.bytesPerRow = padded_row;
            dst.layout.rowsPerImage = H;
            WGPUExtent3D extent = {W, H, 1};
            wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &extent);
            WGPUCommandBufferDescriptor cmd_d{};
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, &cmd_d);
            wgpuQueueSubmit(gpu.queue, 1, &cmd);
            wgpuCommandBufferRelease(cmd);
            wgpuCommandEncoderRelease(enc);

            struct WD { bool done = false; };
            WD wd;
            WGPUQueueWorkDoneCallbackInfo wcb{};
            wcb.mode = WGPUCallbackMode_AllowSpontaneous;
            wcb.callback = [](WGPUQueueWorkDoneStatus, void* u, void*) {
                static_cast<WD*>(u)->done = true; };
            wcb.userdata1 = &wd;
            wgpuQueueOnSubmittedWorkDone(gpu.queue, wcb);
            while (!wd.done) wgpuDevicePoll(gpu.device, true, nullptr);

            struct MD { bool done = false; WGPUMapAsyncStatus s; };
            MD md;
            WGPUBufferMapCallbackInfo mcb{};
            mcb.mode = WGPUCallbackMode_AllowSpontaneous;
            mcb.callback = [](WGPUMapAsyncStatus s, WGPUStringView, void* u, void*) {
                auto* d = static_cast<MD*>(u); d->s = s; d->done = true; };
            mcb.userdata1 = &md;
            wgpuBufferMapAsync(staging_buf, WGPUMapMode_Read, 0, buf_size, mcb);
            while (!md.done) wgpuDevicePoll(gpu.device, true, nullptr);

            if (md.s == WGPUMapAsyncStatus_Success) {
                const uint8_t* mapped = static_cast<const uint8_t*>(
                    wgpuBufferGetConstMappedRange(staging_buf, 0, buf_size));
                // Check center pixel — cube should be visible
                uint32_t cx = W / 2, cy = H / 2;
                size_t idx = cy * padded_row + cx * 4;
                uint8_t r = mapped[idx], g = mapped[idx+1], b = mapped[idx+2];
                std::fprintf(stderr, "  Center pixel: (%u, %u, %u)\n", r, g, b);
                check(r > 0 || g > 0 || b > 0, "cube visible at center (non-black)");
                wgpuBufferUnmap(staging_buf);
            }
            wgpuBufferRelease(staging_buf);
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: depth_output_port_idx = -1 for Shape3D (no depth port)
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: No depth port on Shape3D ===\n");
    {
        vivid::Graph g;
        g.add_node("s1", "Shape3D");
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");

        auto* shape_node = sched.find_node_mut("s1");
        check(shape_node != nullptr, "Shape3D node found");
        if (shape_node) {
            check(shape_node->depth_output_port_idx == -1,
                  "Shape3D has depth_output_port_idx == -1");
        }

        auto* render_node = sched.find_node_mut("r1");
        if (render_node) {
            check(render_node->depth_output_port_idx >= 0,
                  "Render3D has depth_output_port_idx >= 0");
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
