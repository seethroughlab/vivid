#include "sample_hold.h"
#include "operator_api/thumbnail.h"

struct SampleHoldThumbState {
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

SampleHold::~SampleHold() {
    if (thumb_state_) {
        thumb_state_->release_all();
        delete thumb_state_;
    }
}

void SampleHold::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx) return;
    if (!thumb_state_) thumb_state_ = new SampleHoldThumbState();

    if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
        rebuild_thumb_pipeline(ctx);
    }
    if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
        vivid_report_thumbnail_error(ctx, "sample_hold thumbnail pipeline init failed");
        return;
    }

    struct Uniforms { float held_value, mode, pad0, pad1; } u{};
    u.held_value = (ctx->output_count > 0) ? ctx->output_values[0] : 0.0f;
    u.mode = ctx->param_values[0];
    wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, &u, sizeof(u));
    vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group, "SampleHold Thumb Pass");
}

void SampleHold::rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
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

// Deterministic hash for decorative staircase pattern
fn pcg_hash(input: u32) -> u32 {
    let state = input * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let held = clamp(uniforms.data.x, 0.0, 1.0);
    let mode = uniforms.data.y;

    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let step_col = vec4f(80.0/255.0, 130.0/255.0, 190.0/255.0, 160.0/255.0);
    let line_col = vec4f(160.0/255.0, 200.0/255.0, 240.0/255.0, 220.0/255.0);
    let held_col = vec4f(255.0/255.0, 200.0/255.0, 80.0/255.0, 220.0/255.0);

    let pad = 0.08;
    let plot_y = (uv.y - pad) / (1.0 - 2.0 * pad);

    // Decorative staircase: 6 steps across the width
    let num_steps = 6u;
    let step_idx = u32(uv.x * f32(num_steps));
    let h = f32(pcg_hash(step_idx + 42u) % 1000u) / 1000.0;
    let step_level = 1.0 - (h * 0.7 + 0.15);

    // Draw step fills
    if (plot_y > step_level) {
        let dist = abs(plot_y - step_level);
        if (dist < 0.025) {
            return line_col;
        }
        return step_col;
    }

    // Step line
    let dist = abs(plot_y - step_level);
    if (dist < 0.025) {
        return line_col;
    }

    // Vertical transitions between steps
    let step_frac = fract(uv.x * f32(num_steps));
    if (step_frac < 0.04 || step_frac > 0.96) {
        let prev_idx = select(step_idx - 1u, num_steps - 1u, step_idx == 0u);
        let prev_h = f32(pcg_hash(prev_idx + 42u) % 1000u) / 1000.0;
        let prev_level = 1.0 - (prev_h * 0.7 + 0.15);
        let lo = min(step_level, prev_level);
        let hi = max(step_level, prev_level);
        if (plot_y >= lo && plot_y <= hi) {
            return line_col;
        }
    }

    // Current held value indicator
    let held_y = 1.0 - (held * 0.7 + 0.15);
    let held_dist = abs(plot_y - held_y);
    if (held_dist < 0.02) {
        return held_col;
    }

    return bg;
}
)";

    thumb_state_->shader = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "SampleHold Thumb Shader");
    thumb_state_->uniform_buf =
        vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "SampleHold Thumb Uniforms");
    thumb_state_->bind_layout =
        vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "SampleHold Thumb BGL");
    thumb_state_->pipe_layout =
        vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_state_->bind_layout, "SampleHold Thumb Layout");
    thumb_state_->bind_group = vivid::thumbnail::create_uniform_bind_group(
        ctx->device, thumb_state_->bind_layout, thumb_state_->uniform_buf, sizeof(float) * 4, "SampleHold Thumb BG");
    thumb_state_->pipeline = vivid::thumbnail::create_pipeline(
        ctx->device, thumb_state_->shader, thumb_state_->pipe_layout, ctx->thumbnail_format, "SampleHold Thumb Pipeline");
    thumb_state_->pipeline_format = ctx->thumbnail_format;
}

// Shared implementation only; public registration lives in _fr/_au wrappers.
