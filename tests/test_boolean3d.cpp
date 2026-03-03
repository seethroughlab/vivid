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
#include <unordered_map>
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
// Merge vector computation (duplicated from boolean3d.cpp for CPU testing)
// ============================================================================

struct MergeVectors {
    std::vector<uint32_t> from;
    std::vector<uint32_t> to;
};

static MergeVectors compute_merge_vectors(const float* positions, uint32_t vert_count,
                                           uint32_t stride_floats, float epsilon = 1e-5f) {
    MergeVectors mv;
    if (vert_count == 0) return mv;

    float inv_cell = 1.0f / (epsilon * 4.0f);

    auto hash_pos = [&](float x, float y, float z) -> uint64_t {
        auto ix = static_cast<int64_t>(std::floor(x * inv_cell));
        auto iy = static_cast<int64_t>(std::floor(y * inv_cell));
        auto iz = static_cast<int64_t>(std::floor(z * inv_cell));
        uint64_t h = static_cast<uint64_t>(ix * 73856093LL ^ iy * 19349663LL ^ iz * 83492791LL);
        return h;
    };

    std::unordered_map<uint64_t, std::vector<uint32_t>> cells;
    cells.reserve(vert_count);

    for (uint32_t i = 0; i < vert_count; ++i) {
        const float* p = positions + i * stride_floats;
        uint64_t h = hash_pos(p[0], p[1], p[2]);
        cells[h].push_back(i);
    }

    float eps2 = epsilon * epsilon;
    std::vector<uint32_t> canonical(vert_count);
    for (uint32_t i = 0; i < vert_count; ++i) canonical[i] = i;

    for (auto& [h, bucket] : cells) {
        for (size_t a = 0; a < bucket.size(); ++a) {
            uint32_t ia = bucket[a];
            if (canonical[ia] != ia) continue;
            const float* pa = positions + ia * stride_floats;
            for (size_t b = a + 1; b < bucket.size(); ++b) {
                uint32_t ib = bucket[b];
                if (canonical[ib] != ib) continue;
                const float* pb = positions + ib * stride_floats;
                float dx = pa[0] - pb[0], dy = pa[1] - pb[1], dz = pa[2] - pb[2];
                if (dx*dx + dy*dy + dz*dz < eps2) {
                    canonical[ib] = ia;
                }
            }
        }
    }

    for (uint32_t i = 0; i < vert_count; ++i) {
        if (canonical[i] != i) {
            mv.from.push_back(i);
            mv.to.push_back(canonical[i]);
        }
    }
    return mv;
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
        dev_desc.label = vivid::to_sv("Boolean3D Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[boolean3d_test] WebGPU error (%d): %.*s\n",
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
    // CPU Test: Merge vector computation on a cube
    // A cube has 24 vertices (4 per face for flat normals) but only 8
    // unique positions. So we expect 16 merge pairs (24 - 8 = 16).
    // =====================================================================
    std::fprintf(stderr, "\n=== CPU Test: Merge vectors for cube ===\n");
    {
        // Build cube positions (same as Shape3D's generate_cube)
        // 6 faces, 4 verts each = 24 verts. Unit cube corners at +/-0.5
        float positions[24 * 3];
        struct Face { float v[4][3]; };
        static const Face faces[] = {
            // +Z
            { { {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f} } },
            // -Z
            { { { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f} } },
            // +X
            { { { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f} } },
            // -X
            { { {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f} } },
            // +Y
            { { {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f} } },
            // -Y
            { { {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f} } },
        };

        int idx = 0;
        for (int f = 0; f < 6; ++f) {
            for (int v = 0; v < 4; ++v) {
                positions[idx++] = faces[f].v[v][0];
                positions[idx++] = faces[f].v[v][1];
                positions[idx++] = faces[f].v[v][2];
            }
        }

        auto mv = compute_merge_vectors(positions, 24, 3);
        check(mv.from.size() == 16, "cube: 16 merge pairs (24 verts - 8 unique positions)");
        check(mv.from.size() == mv.to.size(), "from and to vectors same length");

        // Verify that merged vertices are at the same position
        bool all_match = true;
        for (size_t i = 0; i < mv.from.size(); ++i) {
            const float* pf = positions + mv.from[i] * 3;
            const float* pt = positions + mv.to[i] * 3;
            float dx = pf[0]-pt[0], dy = pf[1]-pt[1], dz = pf[2]-pt[2];
            if (dx*dx + dy*dy + dz*dz > 1e-8f) {
                all_match = false;
                break;
            }
        }
        check(all_match, "all merge pairs connect vertices at the same position");
    }

    // =====================================================================
    // GPU integration tests
    // =====================================================================

    std::string staging = "./.test_boolean3d_staging";
    std::filesystem::create_directories(staging);

    const char* dylibs[] = { "shape3d.dylib", "boolean3d.dylib", "render_3d.dylib" };
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
    check(registry.find("Shape3D") != nullptr, "Shape3D registered");
    check(registry.find("Boolean3D") != nullptr, "Boolean3D registered");
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

    auto has_non_black = [](const std::vector<uint8_t>& pixels) -> bool {
        for (size_t i = 0; i < pixels.size(); i += 4) {
            if (pixels[i] > 0 || pixels[i+1] > 0 || pixels[i+2] > 0)
                return true;
        }
        return false;
    };

    // -----------------------------------------------------------------
    // GPU Test: Union — Shape3D(cube) + Shape3D(sphere) → Boolean3D(union) → Render3D
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Boolean3D Union ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        g.add_node("cube", "Shape3D", {{"shape", 0.0f}});  // cube
        g.add_node("sphere", "Shape3D", {{"shape", 1.0f}, {"detail", 16.0f}});  // sphere
        g.add_node("bool", "Boolean3D", {{"operation", 0.0f}});  // union
        g.add_node("r1", "Render3D", {{"cam_z", 3.0f}});
        g.add_connection("cube", "scene", "bool", "scene_a");
        g.add_connection("sphere", "scene", "bool", "scene_b");
        g.add_connection("bool", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "union: build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_pixels(sched, W, H, "r1");
        check(!pixels.empty(), "union: readback returned pixels");
        if (!pixels.empty())
            check(has_non_black(pixels), "union: center pixel non-black");

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Subtract — Shape3D(cube) - Shape3D(sphere, shifted)
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Boolean3D Subtract ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        g.add_node("cube", "Shape3D", {{"shape", 0.0f}});
        g.add_node("sphere", "Shape3D", {{"shape", 1.0f}, {"detail", 16.0f},
                                          {"pos_x", 0.3f}});  // shifted
        g.add_node("bool", "Boolean3D", {{"operation", 1.0f}});  // subtract
        g.add_node("r1", "Render3D", {{"cam_z", 3.0f}});
        g.add_connection("cube", "scene", "bool", "scene_a");
        g.add_connection("sphere", "scene", "bool", "scene_b");
        g.add_connection("bool", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "subtract: build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_pixels(sched, W, H, "r1");
        check(!pixels.empty(), "subtract: readback returned pixels");
        if (!pixels.empty())
            check(has_non_black(pixels), "subtract: rendered geometry visible");

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Intersect — Shape3D(cube) ^ Shape3D(sphere)
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Boolean3D Intersect ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        g.add_node("cube", "Shape3D", {{"shape", 0.0f}});
        g.add_node("sphere", "Shape3D", {{"shape", 1.0f}, {"detail", 16.0f}});
        g.add_node("bool", "Boolean3D", {{"operation", 2.0f}});  // intersect
        g.add_node("r1", "Render3D", {{"cam_z", 3.0f}});
        g.add_connection("cube", "scene", "bool", "scene_a");
        g.add_connection("sphere", "scene", "bool", "scene_b");
        g.add_connection("bool", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "intersect: build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_pixels(sched, W, H, "r1");
        check(!pixels.empty(), "intersect: readback returned pixels");
        if (!pixels.empty())
            check(has_non_black(pixels), "intersect: rendered geometry visible");

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Disconnected input B — only scene_a wired
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Boolean3D pass-through (no scene_b) ===\n");
    {
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        g.add_node("cube", "Shape3D", {{"shape", 0.0f}});
        g.add_node("bool", "Boolean3D", {{"operation", 0.0f}});
        g.add_node("r1", "Render3D", {{"cam_z", 3.0f}});
        g.add_connection("cube", "scene", "bool", "scene_a");
        g.add_connection("bool", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "passthrough: build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_pixels(sched, W, H, "r1");
        check(!pixels.empty(), "passthrough: readback returned pixels");
        if (!pixels.empty())
            check(has_non_black(pixels), "passthrough: cube passes through (visible)");

        sched.shutdown();
    }

    // Cleanup
    gpu.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
    return failures > 0 ? 1 : 0;
}
