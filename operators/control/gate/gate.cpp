#include "gate.h"
#include "operator_api/thumbnail.h"

struct GateThumbState {
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroup bind_group = nullptr;
    WGPUBindGroupLayout bind_layout = nullptr;
    WGPUBuffer uniform_buf = nullptr;
    WGPUShaderModule shader = nullptr;
    WGPUPipelineLayout pipe_layout = nullptr;
    WGPUTextureFormat pipeline_format = WGPUTextureFormat_Undefined;

    void release_all() {
        vivid::gpu::release(pipeline);
        vivid::gpu::release(bind_group);
        vivid::gpu::release(bind_layout);
        vivid::gpu::release(uniform_buf);
        vivid::gpu::release(shader);
        vivid::gpu::release(pipe_layout);
    }
};

Gate::~Gate() {
    if (thumb_state_) {
        thumb_state_->release_all();
        delete thumb_state_;
    }
}

void Gate::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx) return;
    if (!thumb_state_) thumb_state_ = new GateThumbState();

    if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
        rebuild_thumb_pipeline(ctx);
    }
    if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
        vivid_report_thumbnail_error(ctx, "gate thumbnail pipeline init failed");
        return;
    }

    struct Uniforms { float threshold, invert, gate_open, pad; } u{};
    u.threshold = ctx->param_values[0];
    u.invert = (ctx->param_count > 1) ? ctx->param_values[1] : 0.0f;
    u.gate_open = (ctx->output_count > 1) ? ctx->output_values[1] : 0.0f;
    wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, &u, sizeof(u));
    vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group, "Gate Thumb Pass");
}

void Gate::rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
    thumb_state_->release_all();

    static const char* kThumbFragment = R"(
struct Uniforms {
    data: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

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
    let uv = input.uv;
    let threshold = clamp(uniforms.data.x, 0.0, 1.0);
    let invert = uniforms.data.y;
    let gate_open = uniforms.data.z;

    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let open_col = vec4f(80.0/255.0, 190.0/255.0, 120.0/255.0, 200.0/255.0);
    let closed_col = vec4f(60.0/255.0, 65.0/255.0, 75.0/255.0, 200.0/255.0);
    let line_col = vec4f(200.0/255.0, 210.0/255.0, 220.0/255.0, 220.0/255.0);

    let pad = 0.08;
    let plot_y = (uv.y - pad) / (1.0 - 2.0 * pad);
    let thresh_y = 1.0 - threshold;

    // Threshold line
    let thresh_dist = abs(plot_y - thresh_y);
    if (thresh_dist < 0.02) {
        return line_col;
    }

    // Fill region: above threshold if not inverted, below if inverted
    var in_active_region = false;
    if (invert < 0.5) {
        in_active_region = plot_y > thresh_y;
    } else {
        in_active_region = plot_y < thresh_y;
    }

    if (in_active_region) {
        if (gate_open > 0.5) {
            return open_col;
        } else {
            return closed_col;
        }
    }

    return bg;
}
)";

    thumb_state_->shader = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "Gate Thumb Shader");
    thumb_state_->uniform_buf =
        vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Gate Thumb Uniforms");
    thumb_state_->bind_layout =
        vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Gate Thumb BGL");
    thumb_state_->pipe_layout =
        vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_state_->bind_layout, "Gate Thumb Layout");
    thumb_state_->bind_group = vivid::thumbnail::create_uniform_bind_group(
        ctx->device, thumb_state_->bind_layout, thumb_state_->uniform_buf, sizeof(float) * 4, "Gate Thumb BG");
    thumb_state_->pipeline = vivid::thumbnail::create_pipeline(
        ctx->device, thumb_state_->shader, thumb_state_->pipe_layout, ctx->thumbnail_format, "Gate Thumb Pipeline");
    thumb_state_->pipeline_format = ctx->thumbnail_format;
}

// Shared implementation only; public registration lives in _fr/_au wrappers.
