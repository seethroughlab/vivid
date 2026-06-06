// Test: the GPU value-view API (lane-value clean-break, Phase 4c).
//
// GpuValueFillOp obtains its output render target via ctx->value_outputs (resize →
// the runtime texture) and reads its texture input via ctx->values — the value-model
// API — instead of output_texture_view / input_texture_views. It runs through real
// (headless) GPU dispatch, proving the texture OUTPUT value API end-to-end (readback)
// and the texture INPUT value view (a downstream op detects the input via ctx->values).
//
// Headless WebGPU helpers (HeadlessGpu / readback_texture / tick_and_submit) are
// copied from tests/gpu/test_gpu_operators.cpp to keep this test self-contained.

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "operator_api/gpu_operator.h"
#include "common/gpu_util.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>
#include "test_helpers.h"

// ---- Headless WebGPU (no window, no surface) -------------------------------
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
        acb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a,
                          WGPUStringView, void* ud1, void*) {
            auto* d = static_cast<AdapterData*>(ud1);
            if (status == WGPURequestAdapterStatus_Success) d->adapter = a;
            d->done = true;
        };
        acb.userdata1 = &ad;
        WGPURequestAdapterOptions adapter_opts{};
        adapter_opts.compatibleSurface = nullptr;
        adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
        wgpuInstanceRequestAdapter(instance, &adapter_opts, acb);
        if (!ad.done || !ad.adapter) return false;
        adapter = ad.adapter;

        struct DeviceData { WGPUDevice device = nullptr; bool done = false; };
        DeviceData dd;
        WGPURequestDeviceCallbackInfo dcb{};
        dcb.mode = WGPUCallbackMode_AllowSpontaneous;
        dcb.callback = [](WGPURequestDeviceStatus status, WGPUDevice dev,
                          WGPUStringView, void* ud1, void*) {
            auto* d = static_cast<DeviceData*>(ud1);
            if (status == WGPURequestDeviceStatus_Success) d->device = dev;
            d->done = true;
        };
        dcb.userdata1 = &dd;
        WGPUDeviceDescriptor dev_desc{};
        dev_desc.label = vivid::to_sv("Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[headless] WebGPU error (%d): %.*s\n",
                             (int)type, (int)msg.length, msg.data ? msg.data : "");
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

static const uint32_t kRowAlignment = 256;
static uint32_t aligned_bytes_per_row(uint32_t width) {
    uint32_t unpadded = width * 4;
    return (unpadded + kRowAlignment - 1) & ~(kRowAlignment - 1);
}

static std::vector<uint8_t> readback_texture(WGPUDevice device, WGPUQueue queue,
                                             WGPUTexture texture, uint32_t width, uint32_t height) {
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
    src.texture = texture; src.mipLevel = 0; src.origin = {0,0,0}; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = staging; dst.layout.offset = 0;
    dst.layout.bytesPerRow = padded_row; dst.layout.rowsPerImage = height;
    WGPUExtent3D extent = { width, height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.label = vivid::to_sv("Readback Commands");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    {
        struct WorkDone { bool done = false; } wd;
        WGPUQueueWorkDoneCallbackInfo wcb{};
        wcb.mode = WGPUCallbackMode_AllowSpontaneous;
        wcb.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* ud1, void*) {
            static_cast<WorkDone*>(ud1)->done = true; };
        wcb.userdata1 = &wd;
        wgpuQueueOnSubmittedWorkDone(queue, wcb);
        while (!wd.done) wgpuDevicePoll(device, true, nullptr);
    }

    struct MapData { bool done = false; WGPUMapAsyncStatus status; } md;
    WGPUBufferMapCallbackInfo mcb{};
    mcb.mode = WGPUCallbackMode_AllowSpontaneous;
    mcb.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* ud1, void*) {
        auto* d = static_cast<MapData*>(ud1); d->status = status; d->done = true; };
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

static void tick_and_submit(vivid::RuntimeCore& runtime, HeadlessGpu& gpu, WGPUTextureFormat format) {
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Tick Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);
    VividGpuContext gpu_state{};
    gpu_state.device = gpu.device; gpu_state.queue = gpu.queue;
    gpu_state.command_encoder = encoder; gpu_state.output_format = format;
    runtime.tick(0.0, 0.016, 0, &gpu_state);
    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.label = vivid::to_sv("Tick Commands");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(gpu.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
    struct WorkDone { bool done = false; } wd;
    WGPUQueueWorkDoneCallbackInfo wcb{};
    wcb.mode = WGPUCallbackMode_AllowSpontaneous;
    wcb.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* ud1, void*) {
        static_cast<WorkDone*>(ud1)->done = true; };
    wcb.userdata1 = &wd;
    wgpuQueueOnSubmittedWorkDone(gpu.queue, wcb);
    while (!wd.done) wgpuDevicePoll(gpu.device, true, nullptr);
}

static const vivid::CompiledNode* find_node(const vivid::CompiledGraph* cg, const char* id) {
    for (const auto& n : cg->nodes) if (n.node_id == id) return &n;
    return nullptr;
}

int main() {
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;
    static constexpr uint32_t W = 64, H = 64;

    std::fprintf(stderr, "\n=== Test: GPU Value-View API ===\n\n");

    HeadlessGpu gpu;
    if (!gpu.init()) {
        std::fprintf(stderr, "  SKIP: no GPU adapter available (headless)\n");
        return 0;  // environments without a GPU skip cleanly
    }

    std::string staging = "./.test_gpu_value_api_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file("gpu_value_fill_op.dylib", staging + "/gpu_value_fill_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");
    check(registry.find("GpuValueFillOp") != nullptr, "GpuValueFillOp registered");

    // ---- Output value API: a value-API fill (no input) → param color (red) ----
    {
        std::fprintf(stderr, "--- texture OUTPUT via the value API ---\n");
        vivid::Graph g;
        g.add_node("fill", "GpuValueFillOp", {{"r", 1.0f}, {"g", 0.0f}, {"b", 0.0f}});
        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build (output)");
        runtime.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(runtime, gpu, kFormat);

        const auto* fill = find_node(runtime.compiled_graph(), "fill");
        check(fill && fill->gpu && fill->gpu->texture, "fill node has a texture");
        if (fill && fill->gpu && fill->gpu->texture) {
            auto px = readback_texture(gpu.device, gpu.queue, fill->gpu->texture, W, H);
            check(!px.empty(), "readback returned pixels");
            if (!px.empty()) {
                size_t c = ((H/2) * W + (W/2)) * 4;
                std::fprintf(stderr, "  center: (%u,%u,%u)\n", px[c], px[c+1], px[c+2]);
                check(px[c] > 200 && px[c+1] < 60 && px[c+2] < 60,
                      "fill rendered RED into the value-API output target");
            }
        }
        runtime.shutdown();
    }

    // ---- Input value API + interop: fill_a (red) → fill_b sees input → green ----
    {
        std::fprintf(stderr, "\n--- texture INPUT via the value API (+ interop) ---\n");
        vivid::Graph g;
        g.add_node("fill_a", "GpuValueFillOp", {{"r", 1.0f}, {"g", 0.0f}, {"b", 0.0f}});
        g.add_node("fill_b", "GpuValueFillOp", {{"r", 1.0f}, {"g", 0.0f}, {"b", 0.0f}});
        g.add_connection("fill_a", "texture", "fill_b", "in");
        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build (input)");
        runtime.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(runtime, gpu, kFormat);

        const auto* fb = find_node(runtime.compiled_graph(), "fill_b");
        check(fb && fb->gpu && fb->gpu->texture, "fill_b has a texture");
        if (fb && fb->gpu && fb->gpu->texture) {
            auto px = readback_texture(gpu.device, gpu.queue, fb->gpu->texture, W, H);
            check(!px.empty(), "readback returned pixels");
            if (!px.empty()) {
                size_t c = ((H/2) * W + (W/2)) * 4;
                std::fprintf(stderr, "  center: (%u,%u,%u)\n", px[c], px[c+1], px[c+2]);
                // fill_b detected its texture input via ctx->values → filled GREEN.
                check(px[c] < 60 && px[c+1] > 200 && px[c+2] < 60,
                      "fill_b saw its texture input via the value API (rendered GREEN)");
            }
        }
        runtime.shutdown();
    }

    gpu.shutdown();
    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
