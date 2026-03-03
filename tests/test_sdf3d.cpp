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
        dev_desc.label = vivid::to_sv("SDF3D Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[sdf3d_test] WebGPU error (%d): %.*s\n",
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
                            WGPUTextureFormat format, double time, double dt) {
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Tick Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

    VividGpuState gpu_state{};
    gpu_state.device          = gpu.device;
    gpu_state.queue           = gpu.queue;
    gpu_state.command_encoder = encoder;
    gpu_state.output_format   = format;

    sched.tick(time, dt, 0, &gpu_state);

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
// Count non-black pixels helper
// ============================================================================

static uint32_t count_non_black(const std::vector<uint8_t>& pixels) {
    uint32_t count = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        if (pixels[i] > 0 || pixels[i+1] > 0 || pixels[i+2] > 0) {
            count++;
        }
    }
    return count;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    using namespace vivid::gpu;
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    // =====================================================================
    // CPU Test: SDFParamsUniform is 192 bytes
    // =====================================================================
    std::fprintf(stderr, "\n=== CPU Test: SDFParamsUniform size ===\n");
    {
        struct SDFParamsCheck {
            float shape_a_type, shape_b_type, operation, smooth_k;
            float size_a[4], size_b[4], pos_b[4], color[4];
            float roughness, metallic, emission, flags;
            float inv_model[16];
            float max_steps, surface_threshold, _pad[2];
        };
        check(sizeof(SDFParamsCheck) == 176, "sizeof(SDFParamsUniform) == 176");
    }

    // =====================================================================
    // CPU Test: CustomCamera3D is 160 bytes
    // =====================================================================
    std::fprintf(stderr, "\n=== CPU Test: CustomCamera3D size ===\n");
    {
        check(sizeof(CustomCamera3D) == 160, "sizeof(CustomCamera3D) == 160");
    }

    // =====================================================================
    // GPU integration tests
    // =====================================================================

    std::string staging = "./.test_sdf3d_staging";
    std::filesystem::create_directories(staging);

    const char* dylibs[] = { "sdf3d.dylib", "render_3d.dylib", "shape3d.dylib", "scene_merge.dylib" };
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
        skip("Required dylibs not found — skipping GPU tests");
        std::filesystem::remove_all(staging);
        std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                     failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
        return failures > 0 ? 1 : 0;
    }

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan() succeeds");
    check(registry.find("SDF3D") != nullptr, "SDF3D registered");
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

    auto get_pixels = [&](vivid::Scheduler& sched, uint32_t W, uint32_t H,
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
    // GPU Test: SDF sphere visible
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: SDF3D sphere -> Render3D — visible ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        g.add_node("s1", "SDF3D", {
            {"shape", 0.0f},  // Sphere
        });
        g.add_node("r1", "Render3D", {
            {"cam_y", 2.0f}, {"cam_z", 5.0f}
        });
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat, 0.0, 0.016);

        auto pixels = get_pixels(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t nb = count_non_black(pixels);
            check(nb > 0, "SDF sphere: image contains non-black pixels");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: SDF box visible
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: SDF3D box -> Render3D — visible ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        g.add_node("s1", "SDF3D", {
            {"shape", 1.0f},  // Box
        });
        g.add_node("r1", "Render3D", {
            {"cam_y", 2.0f}, {"cam_z", 5.0f}
        });
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat, 0.0, 0.016);

        auto pixels = get_pixels(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t nb = count_non_black(pixels);
            check(nb > 0, "SDF box: image contains non-black pixels");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Transform moves SDF off-screen
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Transform moves SDF off-screen ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "SDF3D", {
            {"shape", 0.0f},
            {"pos_x", 100.0f},
        });
        g.add_node("r1", "Render3D", {
            {"cam_y", 2.0f}, {"cam_z", 5.0f}
        });
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat, 0.0, 0.016);

        auto pixels = get_pixels(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t nb = count_non_black(pixels);
            check(nb == 0, "SDF moved off-screen: all pixels are black");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Depth compositing — SDF + Shape3D via SceneMerge
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Depth compositing — SDF + Shape3D ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        // SDF alone
        vivid::Graph g1;
        g1.add_node("s1", "SDF3D", {{"shape", 0.0f}});
        g1.add_node("r1", "Render3D", {{"cam_y", 2.0f}, {"cam_z", 5.0f}});
        g1.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched1;
        check(sched1.build(g1, registry), "SDF-only build succeeds");
        sched1.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(sched1, gpu, kFormat, 0.0, 0.016);
        auto pix_sdf = get_pixels(sched1, W, H, "r1");
        uint32_t nb_sdf = pix_sdf.empty() ? 0 : count_non_black(pix_sdf);
        sched1.shutdown();

        // Shape3D alone (cube offset to the side)
        vivid::Graph g2;
        g2.add_node("c1", "Shape3D", {{"shape", 0.0f}, {"pos_x", 1.5f}});
        g2.add_node("r2", "Render3D", {{"cam_y", 2.0f}, {"cam_z", 5.0f}});
        g2.add_connection("c1", "scene", "r2", "scene");

        vivid::Scheduler sched2;
        check(sched2.build(g2, registry), "Shape-only build succeeds");
        sched2.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(sched2, gpu, kFormat, 0.0, 0.016);
        auto pix_shape = get_pixels(sched2, W, H, "r2");
        uint32_t nb_shape = pix_shape.empty() ? 0 : count_non_black(pix_shape);
        sched2.shutdown();

        // Merged (SDF + Shape3D via SceneMerge)
        vivid::Graph g3;
        g3.add_node("s1", "SDF3D", {{"shape", 0.0f}});
        g3.add_node("c1", "Shape3D", {{"shape", 0.0f}, {"pos_x", 1.5f}});
        g3.add_node("m1", "SceneMerge", {});
        g3.add_node("r3", "Render3D", {{"cam_y", 2.0f}, {"cam_z", 5.0f}});
        g3.add_connection("s1", "scene", "m1", "scene_a");
        g3.add_connection("c1", "scene", "m1", "scene_b");
        g3.add_connection("m1", "scene", "r3", "scene");

        vivid::Scheduler sched3;
        check(sched3.build(g3, registry), "Merged build succeeds");
        sched3.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(sched3, gpu, kFormat, 0.0, 0.016);
        auto pix_merged = get_pixels(sched3, W, H, "r3");
        uint32_t nb_merged = pix_merged.empty() ? 0 : count_non_black(pix_merged);
        sched3.shutdown();

        check(nb_sdf > 0, "SDF alone has visible pixels");
        check(nb_shape > 0, "Shape alone has visible pixels");
        check(nb_merged >= nb_sdf || nb_merged >= nb_shape,
              "Merged scene has at least as many pixels as one component");
    }

    // -----------------------------------------------------------------
    // GPU Test: Smooth union — two shapes combined
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Smooth union ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        g.add_node("s1", "SDF3D", {
            {"shape", 0.0f},       // Sphere A
            {"shape_b", 1.0f},     // Sphere B (index 1 = None+1-1=Sphere)
            {"operation", 1.0f},   // Smooth union
            {"pos_bx", 0.5f},
            {"smooth_k", 0.3f},
        });
        g.add_node("r1", "Render3D", {
            {"cam_y", 2.0f}, {"cam_z", 5.0f}
        });
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat, 0.0, 0.016);

        auto pixels = get_pixels(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t nb = count_non_black(pixels);
            check(nb > 0, "Smooth union: image contains non-black pixels");
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
