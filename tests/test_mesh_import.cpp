#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

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
        dev_desc.label = vivid::to_sv("MeshImport Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[mesh_import_test] WebGPU error (%d): %.*s\n",
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
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    // =====================================================================
    // CPU Test: OBJ loading
    // =====================================================================

    std::fprintf(stderr, "\n=== CPU Test: Load tetrahedron.obj ===\n");
    {
        // Find the test OBJ — try both source tree and build dir locations
        std::string obj_path;
        for (auto& candidate : { "tests/data/tetrahedron.obj",
                                  "../tests/data/tetrahedron.obj",
                                  "../../tests/data/tetrahedron.obj" }) {
            if (std::filesystem::exists(candidate)) {
                obj_path = candidate;
                break;
            }
        }

        if (obj_path.empty()) {
            skip("tetrahedron.obj not found");
        } else {
            tinyobj::attrib_t attrib;
            std::vector<tinyobj::shape_t> shapes;
            std::vector<tinyobj::material_t> materials;
            std::string warn, err;
            bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, obj_path.c_str());
            check(ret, "tinyobj::LoadObj succeeds");
            check(!shapes.empty(), "at least one shape");
            check(attrib.vertices.size() == 12, "4 vertices (12 floats)");
            check(attrib.normals.size() == 12, "4 normals (12 floats)");

            // Count triangles
            uint32_t tri_count = 0;
            for (auto& s : shapes) {
                for (auto fv : s.mesh.num_face_vertices) {
                    tri_count += (fv - 2);
                }
            }
            check(tri_count == 4, "4 triangular faces");

            // Check normals are roughly unit length
            bool normals_ok = true;
            for (size_t i = 0; i < attrib.normals.size(); i += 3) {
                float len = std::sqrt(attrib.normals[i]*attrib.normals[i] +
                                      attrib.normals[i+1]*attrib.normals[i+1] +
                                      attrib.normals[i+2]*attrib.normals[i+2]);
                if (std::fabs(len - 1.0f) > 0.01f) { normals_ok = false; break; }
            }
            check(normals_ok, "normals are approximately unit length");
        }
    }

    // =====================================================================
    // GPU integration tests
    // =====================================================================

    std::string staging = "./.test_mesh_import_staging";
    std::filesystem::create_directories(staging);

    const char* dylibs[] = { "mesh_import.dylib", "render_3d.dylib" };
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
        skip("mesh_import.dylib or render_3d.dylib not found — skipping GPU tests");
        std::filesystem::remove_all(staging);
        std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                     failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
        return failures > 0 ? 1 : 0;
    }

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan() succeeds");
    check(registry.find("MeshImport") != nullptr, "MeshImport registered");
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

    // Find test OBJ path (from build dir)
    std::string obj_path;
    for (auto& candidate : { "tests/data/tetrahedron.obj",
                              "../tests/data/tetrahedron.obj",
                              "../../tests/data/tetrahedron.obj" }) {
        if (std::filesystem::exists(candidate)) {
            obj_path = std::filesystem::absolute(candidate).string();
            break;
        }
    }

    // -----------------------------------------------------------------
    // GPU Test: MeshImport → Render3D — visible output
    // -----------------------------------------------------------------
    if (!obj_path.empty()) {
        std::fprintf(stderr, "\n=== GPU Test: MeshImport(tetrahedron.obj) -> Render3D ===\n");
        {
            constexpr uint32_t W = 64, H = 64;

            vivid::Graph g;
            g.add_node("m1", "MeshImport", {},
                       {{"file", obj_path}});
            g.add_node("r1", "Render3D");
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
                check(rv > 0 || gv > 0 || bv > 0, "center pixel is non-black (mesh visible)");
            }

            sched.shutdown();
        }
    } else {
        skip("tetrahedron.obj not found — skipping GPU mesh load test");
    }

    // -----------------------------------------------------------------
    // GPU Test: MeshImport with empty path — no crash, no output
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: MeshImport(empty path) -> Render3D ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("m1", "MeshImport");
        g.add_node("r1", "Render3D");
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
            check(rv == 0 && gv == 0 && bv == 0,
                  "center pixel is black (empty path = no geometry)");
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
