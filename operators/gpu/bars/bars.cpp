#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <string>
#include <cstring>

// =============================================================================
// Bars WGSL Fragment Shader
// =============================================================================

static const char* kBarsFragment = R"(

struct Uniforms {
    resolution: vec2f,
    bar_count: f32,
    scale: f32,
    gap: f32,
    color_r: f32,
    color_g: f32,
    color_b: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(1) @binding(0) var<storage, read> bars: array<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let count = u32(uniforms.bar_count);
    if (count == 0u) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    let uv = input.uv;
    let bar_w = 1.0 / f32(count);
    let bar_idx = u32(floor(uv.x / bar_w));
    let bar_local = fract(uv.x / bar_w);

    if (bar_idx >= count) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    // Gap: skip rendering in the gap region between bars
    if (bar_local > (1.0 - uniforms.gap)) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    let value = bars[bar_idx] * uniforms.scale;
    let bar_height = clamp(value, 0.0, 1.0);

    // Bars grow upward from bottom (uv.y=1 is bottom after flip)
    let y = 1.0 - uv.y;
    if (y < bar_height) {
        // Slight gradient: brighter at top
        let brightness = 0.7 + 0.3 * (y / max(bar_height, 0.001));
        let color = vec3f(uniforms.color_r, uniforms.color_g, uniforms.color_b) * brightness;
        return vec4f(color, 1.0);
    }

    return vec4f(0.0, 0.0, 0.0, 1.0);
}
)";

// =============================================================================
// Uniform struct matching the WGSL Uniforms
// =============================================================================

struct BarsUniforms {
    float resolution[2];
    float bar_count;
    float scale;
    float gap;
    float color_r;
    float color_g;
    float color_b;
};

// =============================================================================
// Bars Operator
// =============================================================================

struct Bars : vivid::OperatorBase {
    static constexpr const char* kName   = "Bars";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> scale {"scale", 4.0f, 0.1f, 20.0f};
    vivid::Param<float> gap   {"gap",   0.2f, 0.0f, 0.9f};
    vivid::Param<float> r     {"r",     0.3f, 0.0f, 1.0f};
    vivid::Param<float> g     {"g",     0.8f, 0.0f, 1.0f};
    vivid::Param<float> b     {"b",     0.77f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
        out.push_back(&gap);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"values", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
        out.push_back({"output", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[bars] lazy_init FAILED\n");
                return;
            }
        }

        // Read spread input
        uint32_t bar_count = 0;
        const float* bar_data = nullptr;
        if (ctx->input_spreads && ctx->input_spreads[0].length > 0) {
            bar_count = ctx->input_spreads[0].length;
            bar_data = ctx->input_spreads[0].data;
        }


        // Resize storage buffer if bar count changed
        if (bar_count != current_bar_count_) {
            rebuild_storage(gpu, bar_count);
        }

        // Upload bar data to storage buffer
        if (bar_count > 0 && bar_data && storage_buf_) {
            wgpuQueueWriteBuffer(gpu->queue, storage_buf_, 0,
                                 bar_data, bar_count * sizeof(float));
        }

        // Update uniforms
        BarsUniforms u{};
        u.resolution[0] = static_cast<float>(gpu->output_width);
        u.resolution[1] = static_cast<float>(gpu->output_height);
        u.bar_count = static_cast<float>(bar_count);
        u.scale     = scale.value;
        u.gap       = gap.value;
        u.color_r   = r.value;
        u.color_g   = g.value;
        u.color_b   = b.value;

        wgpuQueueWriteBuffer(gpu->queue, uniform_buf_, 0, &u, sizeof(u));

        // Render pass
        WGPURenderPassColorAttachment color_att{};
        color_att.view = gpu->output_texture_view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.resolveTarget = nullptr;
        color_att.loadOp  = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = { 0.0, 0.0, 0.0, 1.0 };

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid_sv("Bars Pass");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            gpu->command_encoder, &rp_desc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, uniform_bind_group_, 0, nullptr);
        if (storage_bind_group_)
            wgpuRenderPassEncoderSetBindGroup(pass, 1, storage_bind_group_, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    ~Bars() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(uniform_bind_group_);
        vivid::gpu::release(storage_bind_group_);
        vivid::gpu::release(uniform_layout_);
        vivid::gpu::release(storage_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(storage_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
    }

private:
    WGPURenderPipeline  pipeline_           = nullptr;
    WGPUBindGroup       uniform_bind_group_ = nullptr;
    WGPUBindGroup       storage_bind_group_ = nullptr;
    WGPUBindGroupLayout uniform_layout_     = nullptr;
    WGPUBindGroupLayout storage_layout_     = nullptr;
    WGPUBuffer          uniform_buf_        = nullptr;
    WGPUBuffer          storage_buf_        = nullptr;
    WGPUShaderModule    shader_             = nullptr;
    WGPUPipelineLayout  pipe_layout_        = nullptr;
    WGPUDevice          device_             = nullptr;
    uint32_t            current_bar_count_  = 0;

    void rebuild_storage(VividGpuState* gpu, uint32_t bar_count) {
        vivid::gpu::release(storage_buf_);
        vivid::gpu::release(storage_bind_group_);
        current_bar_count_ = bar_count;

        if (bar_count == 0) return;

        // Create storage buffer (minimum 4 bytes for WebGPU)
        uint32_t buf_size = bar_count * sizeof(float);
        if (buf_size < 4) buf_size = 4;

        WGPUBufferDescriptor buf_desc{};
        buf_desc.label = vivid_sv("Bars Storage");
        buf_desc.size  = buf_size;
        buf_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        storage_buf_ = wgpuDeviceCreateBuffer(gpu->device, &buf_desc);

        // Recreate bind group for storage
        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer  = storage_buf_;
        bg_entry.offset  = 0;
        bg_entry.size    = buf_size;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Bars Storage BG");
        bg_desc.layout = storage_layout_;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        storage_bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);
    }

    bool lazy_init(VividGpuState* gpu) {
        device_ = gpu->device;

        shader_ = vivid::gpu::create_shader(gpu->device, kBarsFragment, "Bars Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(BarsUniforms), "Bars Uniforms");

        // Bind group layout 0: uniforms
        WGPUBindGroupLayoutEntry uni_entry{};
        uni_entry.binding = 0;
        uni_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        uni_entry.buffer.type = WGPUBufferBindingType_Uniform;
        uni_entry.buffer.minBindingSize = sizeof(BarsUniforms);

        WGPUBindGroupLayoutDescriptor uni_layout_desc{};
        uni_layout_desc.label = vivid_sv("Bars Uniform BGL");
        uni_layout_desc.entryCount = 1;
        uni_layout_desc.entries = &uni_entry;
        uniform_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &uni_layout_desc);

        // Bind group layout 1: storage
        WGPUBindGroupLayoutEntry sto_entry{};
        sto_entry.binding = 0;
        sto_entry.visibility = WGPUShaderStage_Fragment;
        sto_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        sto_entry.buffer.minBindingSize = 0;  // dynamic size

        WGPUBindGroupLayoutDescriptor sto_layout_desc{};
        sto_layout_desc.label = vivid_sv("Bars Storage BGL");
        sto_layout_desc.entryCount = 1;
        sto_layout_desc.entries = &sto_entry;
        storage_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &sto_layout_desc);

        // Pipeline layout with two bind groups
        WGPUBindGroupLayout layouts[2] = { uniform_layout_, storage_layout_ };
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Bars Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 2;
        pl_desc.bindGroupLayouts = layouts;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // Uniform bind group
        WGPUBindGroupEntry uni_bg_entry{};
        uni_bg_entry.binding = 0;
        uni_bg_entry.buffer  = uniform_buf_;
        uni_bg_entry.offset  = 0;
        uni_bg_entry.size    = sizeof(BarsUniforms);

        WGPUBindGroupDescriptor uni_bg_desc{};
        uni_bg_desc.label = vivid_sv("Bars Uniform BG");
        uni_bg_desc.layout = uniform_layout_;
        uni_bg_desc.entryCount = 1;
        uni_bg_desc.entries = &uni_bg_entry;
        uniform_bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &uni_bg_desc);

        // Create initial storage buffer with 1 element (WebGPU requires bound storage)
        rebuild_storage(gpu, 1);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_, gpu->output_format, "Bars Pipeline");
        if (!pipeline_) return false;

        return true;
    }
};

VIVID_REGISTER(Bars)
