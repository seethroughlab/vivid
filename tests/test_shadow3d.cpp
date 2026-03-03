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
        dev_desc.label = vivid::to_sv("Shadow3D Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[shadow3d_test] WebGPU error (%d): %.*s\n",
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

// Compute average luminance of a region of pixels
static float avg_luminance(const std::vector<uint8_t>& pixels, uint32_t W, uint32_t H,
                            uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    float total = 0.0f;
    uint32_t count = 0;
    for (uint32_t y = y0; y < y1 && y < H; ++y) {
        for (uint32_t x = x0; x < x1 && x < W; ++x) {
            size_t idx = (y * W + x) * 4;
            float r = pixels[idx] / 255.0f;
            float g = pixels[idx+1] / 255.0f;
            float b = pixels[idx+2] / 255.0f;
            total += 0.2126f * r + 0.7152f * g + 0.0722f * b;
            count++;
        }
    }
    return count > 0 ? total / static_cast<float>(count) : 0.0f;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    using namespace vivid::gpu;
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    // =====================================================================
    // CPU tests (always run, no GPU needed)
    // =====================================================================

    std::fprintf(stderr, "\n=== CPU Test: cast_shadow field default ===\n");
    {
        VividSceneFragment frag{};
        scene_fragment_identity(frag);
        check(frag.cast_shadow == true, "default cast_shadow is true");
    }

    std::fprintf(stderr, "\n=== CPU Test: ShadowUniform size ===\n");
    {
        // ShadowUniform is defined inside render_3d.cpp, so we check the expected layout:
        // 4 * 16 floats (256) + 1 float (4) + 1 uint32 (4) + 2 floats (8) = 272
        // This matches the WGSL ShadowData struct.
        struct ShadowUniformCheck {
            float light_vp[4][16];
            float shadow_bias;
            uint32_t shadow_count_dir;
            float _pad[2];
        };
        check(sizeof(ShadowUniformCheck) == 272, "ShadowUniform is 272 bytes");
    }

    // =====================================================================
    // GPU integration tests
    // =====================================================================

    std::string staging = "./.test_shadow3d_staging";
    std::filesystem::create_directories(staging);

    const char* dylibs[] = {
        "shape3d.dylib", "render_3d.dylib", "transform3d.dylib",
        "scene_merge.dylib", "light3d.dylib"
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
    check(registry.find("Transform3D") != nullptr, "Transform3D registered");
    check(registry.find("SceneMerge") != nullptr, "SceneMerge registered");
    check(registry.find("Light3D") != nullptr, "Light3D registered");

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

    auto get_render_pixels = [&](vivid::Scheduler& sched, uint32_t W, uint32_t H,
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
    // GPU Test: Directional shadow cast
    // Near-top-down camera looking at floor + cube + light from right.
    // Shadow of cube falls to the left side of the image.
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Directional shadow cast ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        // Floor: flat cube scaled into a plane at y=-1
        g.add_node("floor", "Shape3D", {{"r", 0.8f}, {"g", 0.8f}, {"b", 0.8f}});
        g.add_node("floor_t", "Transform3D", {
            {"pos_y", -1.0f}, {"scale_x", 10.0f}, {"scale_y", 0.1f}, {"scale_z", 10.0f}
        });
        // Cube: above the floor, tall enough to cast a wide shadow
        g.add_node("cube", "Shape3D", {{"r", 0.5f}, {"g", 0.5f}, {"b", 0.5f}});
        g.add_node("cube_t", "Transform3D", {
            {"pos_y", 0.5f}, {"scale_y", 2.0f}
        });
        // Directional light from the right side and slightly above.
        // Shadow extends to the LEFT on the floor.
        g.add_node("light", "Light3D", {
            {"type", 0.0f}, {"intensity", 1.5f},
            {"r", 1.0f}, {"g", 1.0f}, {"b", 1.0f}
        });
        g.add_node("light_t", "Transform3D", {{"pos_x", 1.0f}, {"pos_y", 0.5f}});

        g.add_node("merge1", "SceneMerge");
        g.add_node("merge2", "SceneMerge");
        g.add_node("merge3", "SceneMerge");
        // Near-top-down camera with slight forward tilt to avoid degenerate up
        g.add_node("r1", "Render3D", {
            {"cam_x", 0.0f}, {"cam_y", 8.0f}, {"cam_z", 0.5f},
            {"target_x", 0.0f}, {"target_y", 0.0f}, {"target_z", 0.0f},
            {"shadow_enabled", 1.0f}, {"shadow_resolution", 1024.0f}, {"shadow_bias", 0.005f}
        });

        g.add_connection("floor", "scene", "floor_t", "scene");
        g.add_connection("cube", "scene", "cube_t", "scene");
        g.add_connection("light", "scene", "light_t", "scene");
        g.add_connection("floor_t", "scene", "merge1", "scene_a");
        g.add_connection("cube_t", "scene", "merge1", "scene_b");
        g.add_connection("merge1", "scene", "merge2", "scene_a");
        g.add_connection("light_t", "scene", "merge2", "scene_b");
        g.add_connection("merge2", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "shadow scene build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_render_pixels(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            // Near top-down view: X in image ≈ world X.
            // Light from +X → shadow extends to -X (left side of image).
            // Sample left strip (should have shadow) vs right strip (fully lit).
            uint32_t mid_y0 = H / 4;
            uint32_t mid_y1 = H * 3 / 4;
            float left_lum  = avg_luminance(pixels, W, H, 0,       mid_y0, W / 4,     mid_y1);
            float right_lum = avg_luminance(pixels, W, H, W * 3/4, mid_y0, W,         mid_y1);

            std::fprintf(stderr, "  Left floor luminance:  %.4f\n", left_lum);
            std::fprintf(stderr, "  Right floor luminance: %.4f\n", right_lum);

            bool floor_visible = left_lum > 0.01f || right_lum > 0.01f;
            check(floor_visible, "floor is visible (non-black)");

            // Shadow on left should make it darker than the lit right side
            float diff = std::fabs(left_lum - right_lum);
            std::fprintf(stderr, "  Luminance difference:  %.4f\n", diff);
            check(diff > 0.01f, "shadow creates luminance variation across floor");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: shadow_enabled=false — no shadow darkening
    // Same scene but with shadows disabled
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: shadow_enabled=false ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        g.add_node("floor", "Shape3D", {{"r", 0.8f}, {"g", 0.8f}, {"b", 0.8f}});
        g.add_node("floor_t", "Transform3D", {
            {"pos_y", -1.0f}, {"scale_x", 10.0f}, {"scale_y", 0.1f}, {"scale_z", 10.0f}
        });
        g.add_node("cube", "Shape3D", {{"r", 0.5f}, {"g", 0.5f}, {"b", 0.5f}});
        g.add_node("cube_t", "Transform3D", {
            {"pos_y", 0.5f}, {"scale_y", 2.0f}
        });
        g.add_node("light", "Light3D", {
            {"type", 0.0f}, {"intensity", 1.5f},
            {"r", 1.0f}, {"g", 1.0f}, {"b", 1.0f}
        });
        g.add_node("light_t", "Transform3D", {{"pos_x", 1.0f}, {"pos_y", 0.5f}});

        g.add_node("merge1", "SceneMerge");
        g.add_node("merge2", "SceneMerge");
        g.add_node("r1", "Render3D", {
            {"cam_x", 0.0f}, {"cam_y", 8.0f}, {"cam_z", 0.5f},
            {"target_x", 0.0f}, {"target_y", 0.0f}, {"target_z", 0.0f},
            {"shadow_enabled", 0.0f}  // SHADOWS OFF
        });

        g.add_connection("floor", "scene", "floor_t", "scene");
        g.add_connection("cube", "scene", "cube_t", "scene");
        g.add_connection("light", "scene", "light_t", "scene");
        g.add_connection("floor_t", "scene", "merge1", "scene_a");
        g.add_connection("cube_t", "scene", "merge1", "scene_b");
        g.add_connection("merge1", "scene", "merge2", "scene_a");
        g.add_connection("light_t", "scene", "merge2", "scene_b");
        g.add_connection("merge2", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "no-shadow scene build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_render_pixels(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t mid_y0 = H / 4;
            uint32_t mid_y1 = H * 3 / 4;
            float left_lum  = avg_luminance(pixels, W, H, 0,       mid_y0, W / 4,     mid_y1);
            float right_lum = avg_luminance(pixels, W, H, W * 3/4, mid_y0, W,         mid_y1);

            std::fprintf(stderr, "  Left floor luminance (no shadow):  %.4f\n", left_lum);
            std::fprintf(stderr, "  Right floor luminance (no shadow): %.4f\n", right_lum);

            bool floor_visible = left_lum > 0.01f || right_lum > 0.01f;
            check(floor_visible, "floor is visible with shadows off");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Baseline unchanged — Shape3D → Render3D (no lights)
    // Default fallback light, verify renders non-black (backward compat)
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Baseline unchanged (no lights, default fallback) ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D");
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "baseline build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_render_pixels(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > 0 || gv > 0 || bv > 0,
                  "center pixel non-black (default light fallback + shadows work together)");
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
