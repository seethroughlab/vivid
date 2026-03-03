#include "operator_api/gpu_3d.h"
#include "common/gpu_util.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

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

static bool approx(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) < eps;
}

// ============================================================================
// Headless WebGPU init (same pattern as test_gpu_operators.cpp)
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
        dev_desc.label = vivid::to_sv("3D Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[gpu3d] WebGPU error (%d): %.*s\n",
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

    // Wait for GPU
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

    // Map
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

// ============================================================================
// Main
// ============================================================================

int main() {
    using namespace vivid::gpu;
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    // =====================================================================
    // CPU-only tests (always run, no GPU needed)
    // =====================================================================
    std::fprintf(stderr, "\n=== CPU Test: Vertex3D layout ===\n");
    {
        check(sizeof(Vertex3D) == 48, "sizeof(Vertex3D) == 48");
        Vertex3D v{};
        auto base = reinterpret_cast<uintptr_t>(&v);
        check(reinterpret_cast<uintptr_t>(&v.position) - base == 0,
              "position offset == 0");
        check(reinterpret_cast<uintptr_t>(&v.normal) - base == 12,
              "normal offset == 12");
        check(reinterpret_cast<uintptr_t>(&v.tangent) - base == 24,
              "tangent offset == 24");
        check(reinterpret_cast<uintptr_t>(&v.uv) - base == 40,
              "uv offset == 40");
    }

    std::fprintf(stderr, "\n=== CPU Test: perspective_wgpu ===\n");
    {
        mat4x4 m;
        float y_fov = 1.0f; // ~57 degrees
        float aspect = 1.0f;
        float near = 0.1f;
        float far = 100.0f;
        perspective_wgpu(m, y_fov, aspect, near, far);

        // Transform a point on the near plane: (0, 0, -near, 1)
        vec4 p_near = {0.f, 0.f, -near, 1.f};
        vec4 clip_near;
        mat4x4_mul_vec4(clip_near, m, p_near);
        float ndc_z_near = clip_near[2] / clip_near[3];
        std::fprintf(stderr, "  near plane NDC Z = %f (expect 0.0)\n", ndc_z_near);
        check(approx(ndc_z_near, 0.0f, 1e-4f), "near plane maps to NDC Z=0");

        // Transform a point on the far plane: (0, 0, -far, 1)
        vec4 p_far = {0.f, 0.f, -far, 1.f};
        vec4 clip_far;
        mat4x4_mul_vec4(clip_far, m, p_far);
        float ndc_z_far = clip_far[2] / clip_far[3];
        std::fprintf(stderr, "  far plane NDC Z = %f (expect 1.0)\n", ndc_z_far);
        check(approx(ndc_z_far, 1.0f, 1e-4f), "far plane maps to NDC Z=1");

        // Center of view should map to X=0, Y=0
        check(approx(clip_near[0] / clip_near[3], 0.0f, 1e-5f), "center X=0");
        check(approx(clip_near[1] / clip_near[3], 0.0f, 1e-5f), "center Y=0");
    }

    std::fprintf(stderr, "\n=== CPU Test: ortho_wgpu ===\n");
    {
        mat4x4 m;
        ortho_wgpu(m, -1.f, 1.f, -1.f, 1.f, 0.1f, 100.f);

        // Near plane point: (0, 0, -0.1, 1)
        vec4 p_near = {0.f, 0.f, -0.1f, 1.f};
        vec4 clip_near;
        mat4x4_mul_vec4(clip_near, m, p_near);
        float ndc_z_near = clip_near[2] / clip_near[3];
        std::fprintf(stderr, "  near plane NDC Z = %f (expect 0.0)\n", ndc_z_near);
        check(approx(ndc_z_near, 0.0f, 1e-4f), "near plane maps to NDC Z=0");

        // Far plane point: (0, 0, -100, 1)
        vec4 p_far = {0.f, 0.f, -100.f, 1.f};
        vec4 clip_far;
        mat4x4_mul_vec4(clip_far, m, p_far);
        float ndc_z_far = clip_far[2] / clip_far[3];
        std::fprintf(stderr, "  far plane NDC Z = %f (expect 1.0)\n", ndc_z_far);
        check(approx(ndc_z_far, 1.0f, 1e-4f), "far plane maps to NDC Z=1");

        // Boundary: left edge
        vec4 p_left = {-1.f, 0.f, -0.1f, 1.f};
        vec4 clip_left;
        mat4x4_mul_vec4(clip_left, m, p_left);
        check(approx(clip_left[0] / clip_left[3], -1.0f, 1e-4f), "left edge maps to NDC X=-1");
    }

    std::fprintf(stderr, "\n=== CPU Test: normal_matrix ===\n");
    {
        // Identity model → identity normal matrix
        mat4x4 model_id;
        mat4x4_identity(model_id);
        mat4x4 nm;
        normal_matrix(nm, model_id);
        bool is_identity = true;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (!approx(nm[i][j], (i == j) ? 1.f : 0.f, 1e-5f))
                    is_identity = false;
        check(is_identity, "identity model → identity normal matrix");

        // Non-uniform scale: scale(2, 1, 0.5)
        mat4x4 model_scaled;
        mat4x4_identity(model_scaled);
        model_scaled[0][0] = 2.0f;
        model_scaled[1][1] = 1.0f;
        model_scaled[2][2] = 0.5f;
        normal_matrix(nm, model_scaled);
        // Expected: diag(0.5, 1.0, 2.0, 1.0) — inverse of scale, transposed
        check(approx(nm[0][0], 0.5f, 1e-5f), "scaled normal matrix [0][0] = 0.5");
        check(approx(nm[1][1], 1.0f, 1e-5f), "scaled normal matrix [1][1] = 1.0");
        check(approx(nm[2][2], 2.0f, 1e-5f), "scaled normal matrix [2][2] = 2.0");
        check(approx(nm[3][3], 1.0f, 1e-5f), "scaled normal matrix [3][3] = 1.0");
    }

    // =====================================================================
    // GPU tests (skip if no adapter)
    // =====================================================================
    std::fprintf(stderr, "\n=== GPU init ===\n");
    HeadlessGpu gpu;
    if (!gpu.init()) {
        skip("No GPU available — skipping GPU tests");
        std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                     failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
        return failures > 0 ? 1 : 0;
    }
    check(gpu.device != nullptr, "Device created");

    // --- Depth texture + view ---
    std::fprintf(stderr, "\n=== GPU Test: Depth texture ===\n");
    {
        WGPUTexture depth_tex = create_depth_texture(gpu.device, 64, 64, "Test Depth");
        check(depth_tex != nullptr, "depth texture created");
        WGPUTextureView depth_view = create_depth_view(depth_tex, "Test Depth View");
        check(depth_view != nullptr, "depth view created");
        if (depth_view) wgpuTextureViewRelease(depth_view);
        if (depth_tex) wgpuTextureRelease(depth_tex);
    }

    // --- Vertex buffer ---
    std::fprintf(stderr, "\n=== GPU Test: Vertex buffer ===\n");
    {
        Vertex3D tri[] = {
            {{ 0.0f,  0.5f, 0.0f}, {0,0,1}, {1,0,0,1}, {0.5f, 0.0f}},
            {{-0.5f, -0.5f, 0.0f}, {0,0,1}, {1,0,0,1}, {0.0f, 1.0f}},
            {{ 0.5f, -0.5f, 0.0f}, {0,0,1}, {1,0,0,1}, {1.0f, 1.0f}},
        };
        WGPUBuffer vb = create_vertex_buffer(gpu.device, gpu.queue,
                                              tri, sizeof(tri), "Test VB");
        check(vb != nullptr, "vertex buffer created");
        if (vb) wgpuBufferRelease(vb);
    }

    // --- Index buffer ---
    std::fprintf(stderr, "\n=== GPU Test: Index buffer ===\n");
    {
        uint32_t indices[] = {0, 1, 2};
        WGPUBuffer ib = create_index_buffer(gpu.device, gpu.queue,
                                             indices, 3, "Test IB");
        check(ib != nullptr, "index buffer created");
        if (ib) wgpuBufferRelease(ib);
    }

    // --- WGSL shader compilation with VERTEX_3D_WGSL preamble ---
    std::fprintf(stderr, "\n=== GPU Test: Shader compilation ===\n");
    WGPUShaderModule shader = nullptr;
    {
        std::string src = std::string(VERTEX_3D_WGSL) + R"(
            @group(0) @binding(0) var<uniform> camera: Camera3D;

            @vertex
            fn vs_main(@location(0) pos: vec3f,
                       @location(1) normal: vec3f,
                       @location(2) tangent: vec4f,
                       @location(3) uv: vec2f) -> Vertex3DOutput {
                return transform3d(camera, pos, normal, tangent, uv);
            }

            @fragment
            fn fs_main(in: Vertex3DOutput) -> @location(0) vec4f {
                return vec4f(1.0, 0.0, 0.0, 1.0);
            }
        )";
        WGPUShaderSourceWGSL wgsl_src{};
        wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl_src.code = vivid_sv(src.c_str());
        WGPUShaderModuleDescriptor desc{};
        desc.nextInChain = &wgsl_src.chain;
        desc.label = vivid_sv("Test 3D Shader");
        shader = wgpuDeviceCreateShaderModule(gpu.device, &desc);
        check(shader != nullptr, "3D shader compiled");
    }

    // --- 3D pipeline creation ---
    std::fprintf(stderr, "\n=== GPU Test: 3D pipeline ===\n");
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroupLayout bgl = nullptr;
    WGPUPipelineLayout pipe_layout = nullptr;
    {
        // Create bind group layout with a single uniform buffer
        WGPUBindGroupLayoutEntry bgl_entry{};
        bgl_entry.binding = 0;
        bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entry.buffer.minBindingSize = 0;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Test 3D BGL");
        bgl_desc.entryCount = 1;
        bgl_desc.entries = &bgl_entry;
        bgl = wgpuDeviceCreateBindGroupLayout(gpu.device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Test 3D PipeLayout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bgl;
        pipe_layout = wgpuDeviceCreatePipelineLayout(gpu.device, &pl_desc);

        WGPUVertexBufferLayout vbl = vertex3d_layout();
        Pipeline3DDesc pd{};
        pd.shader = shader;
        pd.layout = pipe_layout;
        pd.color_format = kFormat;
        pd.vertex_layouts = &vbl;
        pd.vertex_layout_count = 1;
        pipeline = create_3d_pipeline(gpu.device, pd);
        check(pipeline != nullptr, "3D pipeline created");
    }

    // --- run_3d_pass renders a triangle → readback non-black center pixel ---
    std::fprintf(stderr, "\n=== GPU Test: run_3d_pass render ===\n");
    if (pipeline && shader) {
        constexpr uint32_t W = 64, H = 64;

        // Create color target texture
        WGPUTextureDescriptor tex_desc{};
        tex_desc.label = vivid_sv("3D Color Target");
        tex_desc.size = { W, H, 1 };
        tex_desc.mipLevelCount = 1;
        tex_desc.sampleCount = 1;
        tex_desc.dimension = WGPUTextureDimension_2D;
        tex_desc.format = kFormat;
        tex_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture color_tex = wgpuDeviceCreateTexture(gpu.device, &tex_desc);

        WGPUTextureViewDescriptor tv_desc{};
        tv_desc.format = kFormat;
        tv_desc.dimension = WGPUTextureViewDimension_2D;
        tv_desc.mipLevelCount = 1;
        tv_desc.arrayLayerCount = 1;
        WGPUTextureView color_view = wgpuTextureCreateView(color_tex, &tv_desc);

        // Depth
        WGPUTexture depth_tex = create_depth_texture(gpu.device, W, H, "3D Depth");
        WGPUTextureView depth_view = create_depth_view(depth_tex, "3D Depth View");

        // Triangle vertices (in clip space: identity MVP)
        Vertex3D tri[] = {
            {{ 0.0f,  0.5f, 0.5f}, {0,0,1}, {1,0,0,1}, {0.5f, 0.0f}},
            {{-0.5f, -0.5f, 0.5f}, {0,0,1}, {1,0,0,1}, {0.0f, 1.0f}},
            {{ 0.5f, -0.5f, 0.5f}, {0,0,1}, {1,0,0,1}, {1.0f, 1.0f}},
        };
        uint32_t indices[] = {0, 1, 2};

        WGPUBuffer vb = create_vertex_buffer(gpu.device, gpu.queue,
                                              tri, sizeof(tri), "Tri VB");
        WGPUBuffer ib = create_index_buffer(gpu.device, gpu.queue,
                                             indices, 3, "Tri IB");

        // Uniform buffer: identity MVP (Camera3D struct)
        // Camera3D = mvp(64) + model(64) + normal_matrix(64) + camera_pos(12) + _pad(4) = 208 bytes
        float camera_data[52] = {}; // 208 / 4
        // Set mvp to identity
        camera_data[0]  = 1.f; // mvp[0][0]
        camera_data[5]  = 1.f; // mvp[1][1]
        camera_data[10] = 1.f; // mvp[2][2]
        camera_data[15] = 1.f; // mvp[3][3]
        // Set model to identity
        camera_data[16] = 1.f;
        camera_data[21] = 1.f;
        camera_data[26] = 1.f;
        camera_data[31] = 1.f;
        // Set normal_matrix to identity
        camera_data[32] = 1.f;
        camera_data[37] = 1.f;
        camera_data[42] = 1.f;
        camera_data[47] = 1.f;

        WGPUBuffer ubo = vivid::gpu::create_uniform_buffer(gpu.device, sizeof(camera_data),
                                                             "Camera UBO");
        wgpuQueueWriteBuffer(gpu.queue, ubo, 0, camera_data, sizeof(camera_data));

        // Bind group
        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer = ubo;
        bg_entry.size = sizeof(camera_data);
        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Test 3D BindGroup");
        bg_desc.layout = bgl;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(gpu.device, &bg_desc);

        // Encode and submit
        WGPUCommandEncoderDescriptor enc_desc{};
        enc_desc.label = vivid::to_sv("3D Test Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

        run_3d_pass(encoder, pipeline, bind_group,
                    vb, sizeof(tri), ib, 3,
                    color_view, depth_view, "Test 3D Pass");

        WGPUCommandBufferDescriptor cmd_desc{};
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
        wgpuQueueSubmit(gpu.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd);
        wgpuCommandEncoderRelease(encoder);

        // Readback
        auto pixels = readback_texture(gpu.device, gpu.queue, color_tex, W, H);
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t r = pixels[idx], g = pixels[idx+1], b = pixels[idx+2], a = pixels[idx+3];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n", r, g, b, a);
            check(r > 0 || g > 0 || b > 0,
                  "center pixel is non-black (3D triangle rendered)");
        }

        // Cleanup
        wgpuBindGroupRelease(bind_group);
        wgpuBufferRelease(ubo);
        wgpuBufferRelease(ib);
        wgpuBufferRelease(vb);
        wgpuTextureViewRelease(depth_view);
        wgpuTextureRelease(depth_tex);
        wgpuTextureViewRelease(color_view);
        wgpuTextureRelease(color_tex);
    }

    // Cleanup global GPU resources
    if (pipeline) wgpuRenderPipelineRelease(pipeline);
    if (pipe_layout) wgpuPipelineLayoutRelease(pipe_layout);
    if (bgl) wgpuBindGroupLayoutRelease(bgl);
    if (shader) wgpuShaderModuleRelease(shader);
    gpu.shutdown();

    std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
    return failures > 0 ? 1 : 0;
}
