#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/thumbnail.h"
#include "ui/rendering/renderer_2d.h"
#include "common/gpu_util.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
// Headless WebGPU init (no window, no surface)
// ============================================================================

struct HeadlessGpu {
    WGPUInstance instance = nullptr;
    WGPUAdapter  adapter  = nullptr;
    WGPUDevice   device   = nullptr;
    WGPUQueue    queue    = nullptr;

    bool init() {
        // 1. Instance
        WGPUInstanceDescriptor inst_desc{};
        instance = wgpuCreateInstance(&inst_desc);
        if (!instance) {
            std::fprintf(stderr, "[headless] Failed to create WebGPU instance\n");
            return false;
        }

        // 2. Adapter (no surface)
        struct AdapterData { WGPUAdapter adapter = nullptr; bool done = false; };
        AdapterData ad;

        WGPURequestAdapterCallbackInfo acb{};
        acb.mode = WGPUCallbackMode_AllowSpontaneous;
        acb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                          WGPUStringView message, void* ud1, void* /*ud2*/) {
            auto* d = static_cast<AdapterData*>(ud1);
            if (status == WGPURequestAdapterStatus_Success) {
                d->adapter = adapter;
            } else {
                std::fprintf(stderr, "[headless] Adapter request failed: %.*s\n",
                             static_cast<int>(message.length), message.data ? message.data : "");
            }
            d->done = true;
        };
        acb.userdata1 = &ad;

        WGPURequestAdapterOptions adapter_opts{};
        adapter_opts.compatibleSurface = nullptr;
        adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
        wgpuInstanceRequestAdapter(instance, &adapter_opts, acb);
        if (!ad.done || !ad.adapter) {
            std::fprintf(stderr, "[headless] No GPU adapter available\n");
            return false;
        }
        adapter = ad.adapter;

        // 3. Device
        struct DeviceData { WGPUDevice device = nullptr; bool done = false; };
        DeviceData dd;

        WGPURequestDeviceCallbackInfo dcb{};
        dcb.mode = WGPUCallbackMode_AllowSpontaneous;
        dcb.callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                          WGPUStringView message, void* ud1, void* /*ud2*/) {
            auto* d = static_cast<DeviceData*>(ud1);
            if (status == WGPURequestDeviceStatus_Success) {
                d->device = device;
            } else {
                std::fprintf(stderr, "[headless] Device request failed: %.*s\n",
                             static_cast<int>(message.length), message.data ? message.data : "");
            }
            d->done = true;
        };
        dcb.userdata1 = &dd;

        WGPUDeviceDescriptor dev_desc{};
        dev_desc.label = vivid::to_sv("Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
                std::fprintf(stderr, "[headless] WebGPU error (%d): %.*s\n",
                             static_cast<int>(type), static_cast<int>(message.length),
                             message.data ? message.data : "");
            };

        wgpuAdapterRequestDevice(adapter, &dev_desc, dcb);
        if (!dd.done || !dd.device) {
            std::fprintf(stderr, "[headless] Failed to get device\n");
            return false;
        }
        device = dd.device;

        // 4. Queue
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
    uint32_t unpadded = width * 4;  // RGBA8 = 4 bytes/pixel
    return (unpadded + kRowAlignment - 1) & ~(kRowAlignment - 1);
}

// Read back an RGBA8 texture to CPU. Returns dense RGBA pixels (no padding).
static std::vector<uint8_t> readback_texture(WGPUDevice device, WGPUQueue queue,
                                              WGPUTexture texture,
                                              uint32_t width, uint32_t height) {
    uint32_t padded_row = aligned_bytes_per_row(width);
    uint64_t buf_size = static_cast<uint64_t>(padded_row) * height;

    // Staging buffer
    WGPUBufferDescriptor buf_desc{};
    buf_desc.label = vivid::to_sv("Readback Buffer");
    buf_desc.size  = buf_size;
    buf_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(device, &buf_desc);

    // Encode copy
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Readback Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &enc_desc);

    WGPUTexelCopyTextureInfo src{};
    src.texture  = texture;
    src.mipLevel = 0;
    src.origin   = { 0, 0, 0 };
    src.aspect   = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = staging;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = padded_row;
    dst.layout.rowsPerImage = height;

    WGPUExtent3D extent = { width, height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.label = vivid::to_sv("Readback Commands");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    // Wait for GPU work to complete
    {
        struct WorkDone { bool done = false; };
        WorkDone wd;
        WGPUQueueWorkDoneCallbackInfo wcb{};
        wcb.mode = WGPUCallbackMode_AllowSpontaneous;
        wcb.callback = [](WGPUQueueWorkDoneStatus, void* ud1, void*) {
            static_cast<WorkDone*>(ud1)->done = true;
        };
        wcb.userdata1 = &wd;
        wgpuQueueOnSubmittedWorkDone(queue, wcb);
        while (!wd.done) {
            wgpuDevicePoll(device, true, nullptr);
        }
    }

    // Map and read
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
    while (!md.done) {
        wgpuDevicePoll(device, true, nullptr);
    }

    std::vector<uint8_t> pixels;
    if (md.status == WGPUMapAsyncStatus_Success) {
        const uint8_t* mapped = static_cast<const uint8_t*>(
            wgpuBufferGetConstMappedRange(staging, 0, buf_size));
        // Strip row padding
        uint32_t dense_row = width * 4;
        pixels.resize(static_cast<size_t>(dense_row) * height);
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(pixels.data() + y * dense_row,
                        mapped + y * padded_row,
                        dense_row);
        }
        wgpuBufferUnmap(staging);
    }

    wgpuBufferRelease(staging);
    return pixels;
}

// Helper: run one runtime tick with a GPU command encoder, then submit.
// Returns the encoder so the caller can record additional commands (like readback)
// before finishing.
static void tick_and_submit(vivid::RuntimeCore& runtime, HeadlessGpu& gpu,
                            WGPUTextureFormat format) {
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Tick Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

    VividGpuContext gpu_state{};
    gpu_state.device          = gpu.device;
    gpu_state.queue           = gpu.queue;
    gpu_state.command_encoder = encoder;
    gpu_state.output_format   = format;

    runtime.tick(0.0, 0.016, 0, &gpu_state);

    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.label = vivid::to_sv("Tick Commands");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(gpu.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    // Wait for completion
    struct WorkDone { bool done = false; };
    WorkDone wd;
    WGPUQueueWorkDoneCallbackInfo wcb{};
    wcb.mode = WGPUCallbackMode_AllowSpontaneous;
    wcb.callback = [](WGPUQueueWorkDoneStatus, void* ud1, void*) {
        static_cast<WorkDone*>(ud1)->done = true;
    };
    wcb.userdata1 = &wd;
    wgpuQueueOnSubmittedWorkDone(gpu.queue, wcb);
    while (!wd.done) {
        wgpuDevicePoll(gpu.device, true, nullptr);
    }
}

struct RenderTarget {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
};

static RenderTarget make_render_target(WGPUDevice device, uint32_t width, uint32_t height,
                                       WGPUTextureFormat format) {
    RenderTarget rt{};
    WGPUTextureDescriptor td{};
    td.label = vivid::to_sv("Thumb Target");
    td.size = { width, height, 1 };
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.dimension = WGPUTextureDimension_2D;
    td.format = format;
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    rt.texture = wgpuDeviceCreateTexture(device, &td);

    WGPUTextureViewDescriptor vd{};
    vd.label = vivid::to_sv("Thumb Target View");
    vd.format = format;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_All;
    rt.view = wgpuTextureCreateView(rt.texture, &vd);
    return rt;
}

static void release_render_target(RenderTarget& rt) {
    if (rt.view) wgpuTextureViewRelease(rt.view);
    if (rt.texture) wgpuTextureRelease(rt.texture);
    rt.view = nullptr;
    rt.texture = nullptr;
}

static std::vector<uint8_t> render_custom_thumbnail(vivid::OperatorLoader& loader,
                                                    void* instance,
                                                    HeadlessGpu& gpu,
                                                    WGPUTextureFormat format,
                                                    const float* params,
                                                    uint32_t param_count,
                                                    const float* outputs,
                                                    uint32_t output_count,
                                                    uint32_t width,
                                                    uint32_t height) {
    RenderTarget rt = make_render_target(gpu.device, width, height, format);
    vivid::ui::Renderer2D draw_renderer;
    bool have_draw_api = draw_renderer.init(gpu.device, format, "../fonts/JetBrainsMono-Regular.ttf", 16.0f, 1.0f);

    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Thumb Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

    {
        WGPURenderPassColorAttachment color_att{};
        color_att.view = rt.view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.loadOp = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = {0.0, 0.0, 0.0, 0.0};

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid::to_sv("Thumb Clear");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp_desc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    VividThumbnailContext ctx{};
    ctx.time = 0.0;
    ctx.delta_time = 0.016;
    ctx.frame = 0;
    ctx.param_values = params;
    ctx.param_count = param_count;
    ctx.output_values = outputs;
    ctx.output_count = output_count;
    ctx.string_param_values = nullptr;
    ctx.string_param_count = 0;
    ctx.file_param_values = nullptr;
    ctx.file_param_count = 0;
    ctx.device = gpu.device;
    ctx.queue = gpu.queue;
    ctx.command_encoder = encoder;
    ctx.thumbnail_texture = rt.texture;
    ctx.thumbnail_texture_view = rt.view;
    ctx.thumbnail_width = width;
    ctx.thumbnail_height = height;
    ctx.thumbnail_format = format;
    ctx.thumbnail_logical_width = width;
    ctx.thumbnail_logical_height = height;
    if (have_draw_api) {
        vivid::ui::populate_draw_api(ctx.draw, draw_renderer);
    }

    loader.draw_thumbnail(instance, &ctx);
    if (have_draw_api) {
        draw_renderer.flush(encoder, rt.view, width, height);
    }

    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.label = vivid::to_sv("Thumb Commands");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(gpu.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    {
        struct WorkDone { bool done = false; };
        WorkDone wd;
        WGPUQueueWorkDoneCallbackInfo wcb{};
        wcb.mode = WGPUCallbackMode_AllowSpontaneous;
        wcb.callback = [](WGPUQueueWorkDoneStatus, void* ud1, void*) {
            static_cast<WorkDone*>(ud1)->done = true;
        };
        wcb.userdata1 = &wd;
        wgpuQueueOnSubmittedWorkDone(gpu.queue, wcb);
        while (!wd.done) {
            wgpuDevicePoll(gpu.device, true, nullptr);
        }
    }

    auto pixels = readback_texture(gpu.device, gpu.queue, rt.texture, width, height);
    release_render_target(rt);
    return pixels;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    // =====================================================================
    // Test 1: Headless GPU init
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 1: Headless GPU init ===\n");
    HeadlessGpu gpu;
    if (!gpu.init()) {
        skip("No GPU available — skipping all GPU tests");
        std::fprintf(stderr, "\n%d passed, %d failed, %d skipped\n", 0, 0, 1);
        return 0;  // Graceful skip, not a failure
    }
    check(gpu.instance != nullptr, "WebGPU instance created");
    check(gpu.adapter  != nullptr, "Adapter obtained (no surface)");
    check(gpu.device   != nullptr, "Device created");
    check(gpu.queue    != nullptr, "Queue obtained");

    // Set up operator registry
    std::string staging = "./.test_gpu_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file("gpu_fill_op.dylib", staging + "/gpu_fill_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file("shape.dylib", staging + "/shape.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file("control_thumb_op.dylib", staging + "/control_thumb_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan() succeeds");
    check(registry.find("GpuFillOp") != nullptr, "GpuFillOp registered");
    check(registry.find("Shape") != nullptr, "Shape registered");
    check(registry.find("ControlThumbOp") != nullptr, "ControlThumbOp registered");

    // =====================================================================
    // Test 2: Solid red fill
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: Solid red fill ===\n");
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("fill", "GpuFillOp", {{"r", 1.0f}, {"g", 0.0f}, {"b", 0.0f}});

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");
        runtime.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(runtime, gpu, kFormat);

        // Readback
        auto& ns = runtime.compiled_graph()->nodes[0];
        auto pixels = readback_texture(gpu.device, gpu.queue, ns.gpu->texture, W, H);
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            // Check center pixel
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t r = pixels[idx], g_ = pixels[idx+1], b = pixels[idx+2], a = pixels[idx+3];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n", r, g_, b, a);
            check(r == 255 && g_ == 0 && b == 0 && a == 255, "center pixel is (255,0,0,255)");
        }

        runtime.shutdown();
    }

    // =====================================================================
    // Test 3: Param change to green
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: Param change to green ===\n");
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("fill", "GpuFillOp", {{"r", 1.0f}, {"g", 0.0f}, {"b", 0.0f}});

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");
        runtime.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        // First tick: red
        tick_and_submit(runtime, gpu, kFormat);

        // Change params to green
        auto* ns = runtime.compiled_graph()->find_node("fill");
        check(ns != nullptr, "found fill node");
        if (ns) {
            auto pi_r = ns->param_indices.find("r");
            auto pi_g = ns->param_indices.find("g");
            if (pi_r != ns->param_indices.end()) ns->param_values[pi_r->second] = 0.0f;
            if (pi_g != ns->param_indices.end()) ns->param_values[pi_g->second] = 1.0f;
            // Mark dirty so the node re-processes
            ns->dirty = true;
        }

        // Second tick: should be green now
        tick_and_submit(runtime, gpu, kFormat);

        auto pixels = readback_texture(gpu.device, gpu.queue,
                                        runtime.compiled_graph()->nodes[0].gpu->texture, W, H);
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t r = pixels[idx], g_ = pixels[idx+1], b = pixels[idx+2], a = pixels[idx+3];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n", r, g_, b, a);
            check(r == 0 && g_ == 255 && b == 0 && a == 255, "center pixel is (0,255,0,255)");
        }

        runtime.shutdown();
    }

    // =====================================================================
    // Test 4: Shape operator renders non-black
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: Shape operator render ===\n");
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("shape", "Shape", {});

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");
        runtime.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(runtime, gpu, kFormat);

        auto pixels = readback_texture(gpu.device, gpu.queue,
                                        runtime.compiled_graph()->nodes[0].gpu->texture, W, H);
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            // Center pixel should be non-black (Shape renders white geometry at center by default)
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t r = pixels[idx], g_ = pixels[idx+1], b = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         r, g_, b, pixels[idx+3]);
            check(r > 0 || g_ > 0 || b > 0,
                  "center pixel is non-black (Shape produces visible geometry)");
        }

        runtime.shutdown();
    }

    // =====================================================================
    // Test 5: custom GPU thumbnail for control operator
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: Control custom GPU thumbnail ===\n");
        vivid::OperatorLoader loader;
        check(loader.load((staging + "/control_thumb_op.dylib").c_str()), "load control_thumb_op");
        check(loader.has_draw_thumbnail(), "control_thumb_op exposes draw_thumbnail");

        void* instance = loader.create_instance();
        check(instance != nullptr, "create control_thumb_op instance");

        float params[] = {0.75f};
        float outputs[] = {0.75f};
        auto pixels = render_custom_thumbnail(loader, instance, gpu, kFormat, params, 1, outputs, 1, 64, 64);
        check(!pixels.empty(), "control thumbnail rendered");
        if (!pixels.empty()) {
            size_t idx = ((32u * 64u) + 40u) * 4u;
            check(pixels[idx] > 0 || pixels[idx + 1] > 0 || pixels[idx + 2] > 0,
                  "control thumbnail contains visible content");
        }

        loader.destroy_instance(instance);
    }

    // =====================================================================
    // Test 6: custom GPU thumbnail for GPU operator
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: GPU custom thumbnail override ===\n");
        vivid::OperatorLoader loader;
        check(loader.load((staging + "/gpu_fill_op.dylib").c_str()), "load gpu_fill_op");
        check(loader.has_draw_thumbnail(), "gpu_fill_op exposes draw_thumbnail");

        void* instance = loader.create_instance();
        check(instance != nullptr, "create gpu_fill_op instance");

        float params[] = {0.2f, 0.7f, 0.9f};
        float outputs[] = {0.0f};
        auto pixels = render_custom_thumbnail(loader, instance, gpu, kFormat, params, 3, outputs, 0, 64, 64);
        check(!pixels.empty(), "gpu thumbnail rendered");
        if (!pixels.empty()) {
            size_t idx = ((32u * 64u) + 32u) * 4u;
            check(pixels[idx + 1] > 0 && pixels[idx + 2] > 0,
                  "gpu thumbnail picked up custom fill color");
        }

        loader.destroy_instance(instance);
    }

    // =====================================================================
    // Test 6b: draw-only thumbnail for waveform operator
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 6b: Draw-only waveform thumbnail ===\n");
        vivid::OperatorLoader loader;
        check(loader.load("lfo_fr.dylib"), "load lfo_fr");
        check(loader.has_draw_thumbnail(), "lfo_fr exposes draw_thumbnail");

        void* instance = loader.create_instance();
        check(instance != nullptr, "create lfo_fr instance");

        float params[] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        float outputs[] = {0.25f};
        auto pixels = render_custom_thumbnail(loader, instance, gpu, kFormat, params, 11, outputs, 1, 64, 64);
        check(!pixels.empty(), "lfo thumbnail rendered");
        if (!pixels.empty()) {
            size_t idx = ((32u * 64u) + 32u) * 4u;
            check(pixels[idx] > 0 || pixels[idx + 1] > 0 || pixels[idx + 2] > 0,
                  "lfo draw-only thumbnail contains visible content");
        }

        loader.destroy_instance(instance);
    }

    // =====================================================================
    // Test 6c: draw-only thumbnail for envelope operator
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 6c: Draw-only envelope thumbnail ===\n");
        vivid::OperatorLoader loader;
        check(loader.load("envelope_fr.dylib"), "load envelope_fr");
        check(loader.has_draw_thumbnail(), "envelope_fr exposes draw_thumbnail");

        void* instance = loader.create_instance();
        check(instance != nullptr, "create envelope_fr instance");

        float params[] = {0.05f, 0.2f, 0.7f, 0.3f, 1.0f, 0.0f, 1.0f};
        float outputs[] = {0.6f};
        auto pixels = render_custom_thumbnail(loader, instance, gpu, kFormat, params, 7, outputs, 1, 64, 64);
        check(!pixels.empty(), "envelope thumbnail rendered");
        if (!pixels.empty()) {
            size_t idx = ((24u * 64u) + 28u) * 4u;
            check(pixels[idx] > 0 || pixels[idx + 1] > 0 || pixels[idx + 2] > 0,
                  "envelope draw-only thumbnail contains visible content");
        }

        loader.destroy_instance(instance);
    }

    // =====================================================================
    // Test 6d: draw-only thumbnail for meter operator
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 6d: Draw-only meter thumbnail ===\n");
        vivid::OperatorLoader loader;
        check(loader.load("gain.dylib"), "load gain");
        check(loader.has_draw_thumbnail(), "gain exposes draw_thumbnail");

        void* instance = loader.create_instance();
        check(instance != nullptr, "create gain instance");

        float params[] = {1.5f};
        float outputs[] = {0.0f};
        auto pixels = render_custom_thumbnail(loader, instance, gpu, kFormat, params, 1, outputs, 1, 64, 64);
        check(!pixels.empty(), "gain thumbnail rendered");
        if (!pixels.empty()) {
            size_t idx = ((40u * 64u) + 32u) * 4u;
            check(pixels[idx] > 0 || pixels[idx + 1] > 0 || pixels[idx + 2] > 0,
                  "gain draw-only thumbnail contains visible content");
        }

        loader.destroy_instance(instance);
    }

    // =====================================================================
    // Test 7: Resolution propagation (128x128)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: Resolution propagation ===\n");
        constexpr uint32_t W = 128, H = 128;

        vivid::Graph g;
        // Add node with explicit tex_width/height
        g.add_node("fill", "GpuFillOp", {{"r", 0.0f}, {"g", 0.0f}, {"b", 1.0f}});
        // Set resolution on the node definition
        auto& nodes = g.nodes();
        for (auto& n : nodes) {
            if (n.id == "fill") {
                const_cast<vivid::NodeDef&>(n).tex_width  = W;
                const_cast<vivid::NodeDef&>(n).tex_height = H;
            }
        }

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");
        runtime.allocate_gpu_textures(gpu.device, 64, 64, kFormat, WGPUTextureUsage_CopySrc);

        // Verify the node got 128×128, not the 64×64 default
        auto& ns = runtime.compiled_graph()->nodes[0];
        check(ns.gpu->tex_width == W, "texture width is 128");
        check(ns.gpu->tex_height == H, "texture height is 128");

        tick_and_submit(runtime, gpu, kFormat);

        auto pixels = readback_texture(gpu.device, gpu.queue, ns.gpu->texture, W, H);
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            // Should be a 128×128 blue fill
            check(pixels.size() == W * H * 4, "pixel count matches 128x128");
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t r = pixels[idx], g_ = pixels[idx+1], b = pixels[idx+2], a = pixels[idx+3];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n", r, g_, b, a);
            check(r == 0 && g_ == 0 && b == 255 && a == 255, "center pixel is (0,0,255,255)");
        }

        runtime.shutdown();
    }

    // Clean up
    gpu.shutdown();
    std::filesystem::remove_all(staging);

    int passed = (failures == 0) ? 1 : 0;
    std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
    return failures > 0 ? 1 : 0;
}
