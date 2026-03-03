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
        dev_desc.label = vivid::to_sv("Scene3D Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[scene3d_test] WebGPU error (%d): %.*s\n",
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

// ============================================================================
// Main
// ============================================================================

int main() {
    using namespace vivid::gpu;
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    // =====================================================================
    // CPU tests (always run, no GPU needed)
    // =====================================================================

    std::fprintf(stderr, "\n=== CPU Test: Transform composition (identity) ===\n");
    {
        // Identity Transform3D preserves input model_matrix
        VividSceneFragment input{};
        scene_fragment_identity(input);
        // Set a known translation on input
        input.model_matrix[3][0] = 1.0f;
        input.model_matrix[3][1] = 2.0f;
        input.model_matrix[3][2] = 3.0f;

        // Identity transform wrapper
        VividSceneFragment wrapper{};
        scene_fragment_identity(wrapper);
        VividSceneFragment* child = &input;
        wrapper.children    = &child;
        wrapper.child_count = 1;

        // Compose: identity * input = input
        mat4x4 composed;
        mat4x4_mul(composed, wrapper.model_matrix, input.model_matrix);

        check(std::fabs(composed[3][0] - 1.0f) < 1e-5f, "composed X = 1.0 (identity pass-through)");
        check(std::fabs(composed[3][1] - 2.0f) < 1e-5f, "composed Y = 2.0 (identity pass-through)");
        check(std::fabs(composed[3][2] - 3.0f) < 1e-5f, "composed Z = 3.0 (identity pass-through)");
    }

    std::fprintf(stderr, "\n=== CPU Test: Transform composition (translation) ===\n");
    {
        // Transform3D at (10,0,0) wrapping input at (1,2,3) → composed at (11,2,3)
        VividSceneFragment input{};
        scene_fragment_identity(input);
        input.model_matrix[3][0] = 1.0f;
        input.model_matrix[3][1] = 2.0f;
        input.model_matrix[3][2] = 3.0f;

        VividSceneFragment wrapper{};
        mat4x4_translate(wrapper.model_matrix, 10.0f, 0.0f, 0.0f);

        mat4x4 composed;
        mat4x4_mul(composed, wrapper.model_matrix, input.model_matrix);

        check(std::fabs(composed[3][0] - 11.0f) < 1e-5f, "composed X = 11.0 (10 + 1)");
        check(std::fabs(composed[3][1] - 2.0f) < 1e-5f,  "composed Y = 2.0");
        check(std::fabs(composed[3][2] - 3.0f) < 1e-5f,  "composed Z = 3.0");
    }

    std::fprintf(stderr, "\n=== CPU Test: SceneMerge collection ===\n");
    {
        VividSceneFragment a{}, b{};
        scene_fragment_identity(a);
        scene_fragment_identity(b);
        a.model_matrix[3][0] = 1.0f;
        b.model_matrix[3][0] = -1.0f;

        VividSceneFragment* children[4]{};
        children[0] = &a;
        children[1] = &b;
        uint32_t count = 2;

        VividSceneFragment merger{};
        scene_fragment_identity(merger);
        merger.children    = children;
        merger.child_count = count;

        check(merger.child_count == 2, "merged child_count == 2");
        check(merger.children[0] == &a, "children[0] points to a");
        check(merger.children[1] == &b, "children[1] points to b");
    }

    std::fprintf(stderr, "\n=== CPU Test: Light fragment fields ===\n");
    {
        VividSceneFragment light{};
        scene_fragment_identity(light);
        light.fragment_type   = VividSceneFragment::LIGHT;
        light.light_type      = 0.0f;  // directional
        light.light_color[0]  = 1.0f;
        light.light_color[1]  = 0.5f;
        light.light_color[2]  = 0.0f;
        light.light_intensity = 2.0f;
        light.light_radius    = 15.0f;

        check(light.fragment_type == VividSceneFragment::LIGHT, "fragment_type == LIGHT");
        check(light.light_type == 0.0f, "light_type == 0 (directional)");
        check(std::fabs(light.light_color[0] - 1.0f) < 1e-6f, "light_color.r == 1.0");
        check(std::fabs(light.light_color[1] - 0.5f) < 1e-6f, "light_color.g == 0.5");
        check(std::fabs(light.light_color[2] - 0.0f) < 1e-6f, "light_color.b == 0.0");
        check(std::fabs(light.light_intensity - 2.0f) < 1e-6f, "light_intensity == 2.0");
        check(std::fabs(light.light_radius - 15.0f) < 1e-6f, "light_radius == 15.0");
    }

    // =====================================================================
    // GPU integration tests (skip if no dylibs or no adapter)
    // =====================================================================

    std::string staging = "./.test_scene3d_staging";
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
    // GPU Test: Shape3D → Transform3D (identity) → Render3D — visible
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D -> Transform3D(identity) -> Render3D ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D");       // default cube at origin
        g.add_node("t1", "Transform3D");   // identity transform
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "t1", "scene");
        g.add_connection("t1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > 0 || gv > 0 || bv > 0, "center pixel is non-black (transform pass-through)");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Transform3D off-screen (pos_x=100) → center is black
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Transform3D off-screen ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D");
        g.add_node("t1", "Transform3D", {{"pos_x", 100.0f}});
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "t1", "scene");
        g.add_connection("t1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv == 0 && gv == 0 && bv == 0, "center pixel is black (off-screen)");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: SceneMerge two shapes → both visible
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: SceneMerge two shapes ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D");  // cube at origin
        g.add_node("s2", "Shape3D", {{"shape", 1.0f}});  // sphere at origin
        g.add_node("m1", "SceneMerge");
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "m1", "scene_a");
        g.add_connection("s2", "scene", "m1", "scene_b");
        g.add_connection("m1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > 0 || gv > 0 || bv > 0, "center pixel non-black (merged scene visible)");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Light3D red light → red dominates
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Light3D red light ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        // White shape so light color shows through
        g.add_node("s1", "Shape3D", {{"r", 1.0f}, {"g", 1.0f}, {"b", 1.0f}});
        g.add_node("l1", "Light3D", {{"r", 1.0f}, {"g", 0.0f}, {"b", 0.0f}, {"intensity", 1.0f}});
        g.add_node("m1", "SceneMerge");
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "m1", "scene_a");
        g.add_connection("l1", "scene", "m1", "scene_b");
        g.add_connection("m1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > gv && rv > bv, "red channel dominates with red Light3D");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Backward compat — Shape3D → Render3D (no Light3D) still works
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Backward compat (no Light3D) ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D");
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > 0 || gv > 0 || bv > 0,
                  "center pixel non-black (default light fallback works)");
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
