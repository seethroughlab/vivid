#include "envelope.h"
#include "operator_api/thumbnail.h"

struct EnvelopeThumbState {
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

Envelope::~Envelope() {
    if (thumb_state_) {
        thumb_state_->release_all();
        delete thumb_state_;
    }
}

void Envelope::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx) return;
    if (!thumb_state_) thumb_state_ = new EnvelopeThumbState();

    if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
        rebuild_thumb_pipeline(ctx);
    }
    if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
        vivid_report_thumbnail_error(ctx, "envelope thumbnail pipeline init failed");
        return;
    }

    struct Uniforms { float attack, decay, sustain, release_val, current_value, curve_type; float pad[2]; } u{};
    u.attack = ctx->param_values[0];
    u.decay = ctx->param_values[1];
    u.sustain = ctx->param_values[2];
    u.release_val = ctx->param_values[3];
    u.current_value = (ctx->output_count > 0) ? ctx->output_values[0] : 0.0f;
    u.curve_type = (ctx->param_count > 6) ? ctx->param_values[6] : 1.0f;
    wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, &u, sizeof(u));
    vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group, "Envelope Thumb Pass");
}

void Envelope::rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
    thumb_state_->release_all();

    static const char* kThumbFragment = R"(
struct Uniforms {
    data: vec4f,
    data2: vec4f,
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

fn shape_attack_wgsl(t: f32, curve: f32) -> f32 {
    let tc = clamp(t, 0.0, 1.0);
    if (curve < 0.5) { return tc; }                          // linear
    if (curve < 1.5) { return 1.0 - exp(-4.0 * tc); }       // exponential
    return tc * tc;                                           // logarithmic
}

fn shape_decay_wgsl(t: f32, curve: f32) -> f32 {
    let tc = clamp(t, 0.0, 1.0);
    if (curve < 0.5) { return tc; }
    if (curve < 1.5) { return 1.0 - exp(-4.0 * tc); }
    return tc * tc;
}

fn envelope_at(x: f32, x_a: f32, x_d: f32, x_s: f32, s: f32) -> f32 {
    let curve = uniforms.data2.y;
    if (x < x_a) {
        return shape_attack_wgsl(x / x_a, curve);
    } else if (x < x_d) {
        let t = (x - x_a) / (x_d - x_a);
        return 1.0 - (1.0 - s) * shape_decay_wgsl(t, curve);
    } else if (x < x_s) {
        return s;
    } else {
        let t = (x - x_s) / (1.0 - x_s);
        return s * (1.0 - shape_decay_wgsl(t, curve));
    }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let a = max(uniforms.data.x, 0.001);
    let d = max(uniforms.data.y, 0.001);
    let s = clamp(uniforms.data.z, 0.0, 1.0);
    let r = max(uniforms.data.w, 0.001);
    let current_value = clamp(uniforms.data2.x, 0.0, 1.0);

    // Normalised X positions for each segment
    let total = a + d + (a + d + r) * 0.43 + r;
    let x_a = a / total;
    let x_d = (a + d) / total;
    let x_s = 1.0 - r / total;

    let env = envelope_at(uv.x, x_a, x_d, x_s, s);

    // Y axis: 0 at bottom, 1 at top (with padding)
    let pad = 0.08;
    let plot_y = (uv.y - pad) / (1.0 - 2.0 * pad);
    let curve_y = 1.0 - env;

    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let fill_col = vec4f(80.0/255.0, 130.0/255.0, 190.0/255.0, 180.0/255.0);
    let line_col = vec4f(160.0/255.0, 200.0/255.0, 240.0/255.0, 240.0/255.0);
    let playhead_col = vec4f(255.0/255.0, 200.0/255.0, 80.0/255.0, 220.0/255.0);

    // Playhead: find X where envelope matches current_value (scan attack/decay)
    // Draw a horizontal line at y = current_value with a bright dot on the curve
    let playhead_y = 1.0 - current_value;
    let playhead_dist = abs(plot_y - playhead_y);

    // Fill below curve
    if (plot_y > curve_y) {
        let dist = abs(plot_y - curve_y);
        if (dist < 0.02) {
            return line_col;
        }
        // Draw playhead horizontal line (thin dashed)
        if (current_value > 0.01 && playhead_dist < 0.015) {
            return playhead_col;
        }
        return fill_col;
    }

    // Line on curve
    let dist = abs(plot_y - curve_y);
    if (dist < 0.02) {
        // Bright dot where playhead meets curve
        if (current_value > 0.01 && playhead_dist < 0.04) {
            return playhead_col;
        }
        return line_col;
    }

    // Draw playhead horizontal line above curve
    if (current_value > 0.01 && playhead_dist < 0.015) {
        return vec4f(playhead_col.rgb, 100.0/255.0);
    }

    return bg;
}
)";

    thumb_state_->shader = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "Envelope Thumb Shader");
    thumb_state_->uniform_buf =
        vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 8, "Envelope Thumb Uniforms");
    thumb_state_->bind_layout =
        vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 8, "Envelope Thumb BGL");
    thumb_state_->pipe_layout =
        vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_state_->bind_layout, "Envelope Thumb Layout");
    thumb_state_->bind_group = vivid::thumbnail::create_uniform_bind_group(
        ctx->device, thumb_state_->bind_layout, thumb_state_->uniform_buf, sizeof(float) * 8, "Envelope Thumb BG");
    thumb_state_->pipeline = vivid::thumbnail::create_pipeline(
        ctx->device, thumb_state_->shader, thumb_state_->pipe_layout, ctx->thumbnail_format, "Envelope Thumb Pipeline");
    thumb_state_->pipeline_format = ctx->thumbnail_format;
}

// Shared implementation only; public registration lives in _fr/_au wrappers.
