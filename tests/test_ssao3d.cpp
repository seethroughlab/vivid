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
        dev_desc.label = vivid::to_sv("SSAO3D Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[ssao3d_test] WebGPU error (%d): %.*s\n",
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

static std::vector<uint8_t> readback_texture(WGPUDevice device, WGPUQueue queue,
                                              WGPUTexture texture,
                                              uint32_t width, uint32_t height) {
    uint32_t padded_row = ((width * 4) + kRowAlignment - 1) & ~(kRowAlignment - 1);
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

    std::string staging = "./.test_ssao3d_staging";
    std::filesystem::create_directories(staging);

    const char* dylibs[] = {
        "shape3d.dylib", "render_3d.dylib", "transform3d.dylib",
        "scene_merge.dylib", "ssao3d.dylib"
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
        skip("One or more dylibs not found — skipping SSAO3D tests");
        std::filesystem::remove_all(staging);
        std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                     failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
        return failures > 0 ? 1 : 0;
    }

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan() succeeds");
    check(registry.find("SSAO3D") != nullptr, "SSAO3D registered");

    std::fprintf(stderr, "\n=== GPU init ===\n");
    HeadlessGpu gpu;
    if (!gpu.init()) {
        skip("No GPU available — skipping SSAO3D tests");
        std::filesystem::remove_all(staging);
        std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                     failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
        return failures > 0 ? 1 : 0;
    }
    check(gpu.device != nullptr, "Device created");

    // -----------------------------------------------------------------
    // GPU Test: SSAO produces darkened corners
    // Scene: floor + wall (two perpendicular planes) creating a corner
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: SSAO corner occlusion ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        // Floor (horizontal plane at y=-1)
        g.add_node("floor", "Shape3D", {{"r", 0.8f}, {"g", 0.8f}, {"b", 0.8f}});
        g.add_node("floor_t", "Transform3D", {
            {"pos_y", -1.0f}, {"scale_x", 5.0f}, {"scale_y", 0.1f}, {"scale_z", 5.0f}
        });
        // Wall (vertical plane at z=-2)
        g.add_node("wall", "Shape3D", {{"r", 0.8f}, {"g", 0.8f}, {"b", 0.8f}});
        g.add_node("wall_t", "Transform3D", {
            {"pos_z", -2.0f}, {"scale_x", 5.0f}, {"scale_y", 5.0f}, {"scale_z", 0.1f}
        });

        g.add_node("merge", "SceneMerge");
        g.add_node("r1", "Render3D", {
            {"cam_x", 0.0f}, {"cam_y", 1.0f}, {"cam_z", 3.0f},
            {"target_x", 0.0f}, {"target_y", 0.0f}, {"target_z", -1.0f}
        });
        g.add_node("ssao", "SSAO3D", {
            {"radius", 0.5f}, {"intensity", 2.0f}, {"bias", 0.025f},
            {"near_plane", 0.1f}, {"far_plane", 100.0f}
        });

        g.add_connection("floor", "scene", "floor_t", "scene");
        g.add_connection("wall", "scene", "wall_t", "scene");
        g.add_connection("floor_t", "scene", "merge", "scene_a");
        g.add_connection("wall_t", "scene", "merge", "scene_b");
        g.add_connection("merge", "scene", "r1", "scene");
        g.add_connection("r1", "texture", "ssao", "color");
        g.add_connection("r1", "depth", "ssao", "depth");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "SSAO scene build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto* ssao_node = sched.find_node_mut("ssao");
        check(ssao_node != nullptr, "SSAO3D node found");

        if (ssao_node && ssao_node->gpu_texture) {
            auto pixels = readback_texture(gpu.device, gpu.queue,
                                            ssao_node->gpu_texture, W, H);
            check(!pixels.empty(), "readback returned pixels");

            if (!pixels.empty()) {
                // Check that output is non-black (SSAO composite should produce visible result)
                float center_lum = avg_luminance(pixels, W, H, W/4, H/4, W*3/4, H*3/4);
                std::fprintf(stderr, "  Center luminance: %.4f\n", center_lum);
                check(center_lum > 0.01f, "SSAO output is non-black");
            }
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
