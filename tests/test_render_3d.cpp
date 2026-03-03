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
        dev_desc.label = vivid::to_sv("Render3D Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[render3d_test] WebGPU error (%d): %.*s\n",
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

    std::fprintf(stderr, "\n=== CPU Test: VIVID_PORT_GPU_SCENE enum ===\n");
    {
        check(VIVID_PORT_GPU_SCENE == 6, "VIVID_PORT_GPU_SCENE == 6");
    }

    std::fprintf(stderr, "\n=== CPU Test: VividSceneFragment defaults ===\n");
    {
        VividSceneFragment frag{};
        scene_fragment_identity(frag);

        // Check identity matrix
        bool is_identity = true;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (std::fabs(frag.model_matrix[i][j] - ((i == j) ? 1.f : 0.f)) > 1e-6f)
                    is_identity = false;
        check(is_identity, "scene_fragment_identity() produces identity matrix");

        check(frag.vertex_buffer == nullptr, "default vertex_buffer is nullptr");
        check(frag.index_count == 0, "default index_count is 0");
        check(frag.color[0] == 1.0f && frag.color[1] == 1.0f &&
              frag.color[2] == 1.0f && frag.color[3] == 1.0f,
              "default color is (1,1,1,1)");
        check(frag.pipeline == nullptr, "default pipeline is nullptr");
        check(frag.children == nullptr, "default children is nullptr");
        check(frag.child_count == 0, "default child_count is 0");
        check(frag.depth_write == true, "default depth_write is true");
    }

    std::fprintf(stderr, "\n=== CPU Test: port_type_compatible ===\n");
    {
        using vivid::ui::port_type_compatible;
        check(port_type_compatible(VIVID_PORT_GPU_SCENE, VIVID_PORT_GPU_SCENE) == true,
              "GPU_SCENE <-> GPU_SCENE compatible");
        check(port_type_compatible(VIVID_PORT_GPU_SCENE, VIVID_PORT_GPU_TEXTURE) == false,
              "GPU_SCENE <-> GPU_TEXTURE incompatible");
        check(port_type_compatible(VIVID_PORT_GPU_SCENE, VIVID_PORT_CONTROL_FLOAT) == false,
              "GPU_SCENE <-> CONTROL_FLOAT incompatible");
        check(port_type_compatible(VIVID_PORT_GPU_SCENE, VIVID_PORT_AUDIO_FLOAT) == false,
              "GPU_SCENE <-> AUDIO_FLOAT incompatible");
    }

    // =====================================================================
    // Scheduler tests (need registry but no GPU)
    // =====================================================================
    std::fprintf(stderr, "\n=== Scheduler tests ===\n");

    // Set up operator registry with Render3D
    std::string staging = "./.test_render3d_staging";
    std::filesystem::create_directories(staging);
    bool has_render3d = false;
    if (std::filesystem::exists("render_3d.dylib")) {
        std::filesystem::copy_file("render_3d.dylib", staging + "/render_3d.dylib",
            std::filesystem::copy_options::overwrite_existing);
        has_render3d = true;
    }

    if (!has_render3d) {
        skip("render_3d.dylib not found — skipping scheduler and GPU tests");
        std::filesystem::remove_all(staging);
        std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                     failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
        return failures > 0 ? 1 : 0;
    }

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan() succeeds");
    check(registry.find("Render3D") != nullptr, "Render3D registered");

    // Test: scene-to-scene wire
    std::fprintf(stderr, "\n=== Scheduler Test: scene wire ===\n");
    {
        // Build a graph with two Render3D nodes connected scene->scene
        // (in practice this wouldn't make sense, but it validates wire typing)
        vivid::Graph g;
        g.add_node("r1", "Render3D");
        // Note: Render3D has scene INPUT and texture OUTPUT.
        // We can't wire scene->scene between two Render3D nodes because
        // Render3D has no scene output. So instead we just verify the
        // build succeeds with a single Render3D node.
        vivid::Scheduler sched;
        check(sched.build(g, registry), "build with Render3D succeeds");

        // Verify the node has expected port configuration
        auto& ns = sched.nodes_mut()[0];
        check(ns.is_gpu, "Render3D is GPU domain");
        check(ns.has_texture_output, "Render3D has texture output");
        check(ns.scene_input_port_indices.size() == 1,
              "Render3D has 1 scene input port");
        check(ns.texture_input_port_indices.empty(),
              "Render3D has 0 texture input ports");

        sched.shutdown();
    }

    // =====================================================================
    // GPU tests (skip if no adapter)
    // =====================================================================
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

    // Test: Render3D with no scene input → black background
    std::fprintf(stderr, "\n=== GPU Test: Render3D empty scene (black bg) ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("r1", "Render3D");  // default bg_r/g/b = 0

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto& ns = sched.nodes_mut()[0];
        auto pixels = readback_texture(gpu.device, gpu.queue, ns.gpu_texture, W, H);
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t r = pixels[idx], g_ = pixels[idx+1], b = pixels[idx+2], a = pixels[idx+3];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n", r, g_, b, a);
            check(r == 0 && g_ == 0 && b == 0 && a == 255,
                  "center pixel is black (0,0,0,255) with default background");
        }

        sched.shutdown();
    }

    // Test: Render3D with custom red background
    std::fprintf(stderr, "\n=== GPU Test: Render3D custom bg (red) ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("r1", "Render3D", {{"bg_r", 1.0f}, {"bg_g", 0.0f}, {"bg_b", 0.0f}, {"bg_a", 1.0f}});

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto& ns = sched.nodes_mut()[0];
        auto pixels = readback_texture(gpu.device, gpu.queue, ns.gpu_texture, W, H);
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t r = pixels[idx], g_ = pixels[idx+1], b = pixels[idx+2], a = pixels[idx+3];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n", r, g_, b, a);
            check(r == 255, "center pixel red channel == 255");
            check(g_ == 0, "center pixel green channel == 0");
            check(b == 0, "center pixel blue channel == 0");
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
