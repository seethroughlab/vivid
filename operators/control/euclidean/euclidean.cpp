#include "euclidean_core.h"
#include "operator_api/thumbnail.h"

struct EuclideanThumbState {
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

EuclideanCore::~EuclideanCore() {
    if (thumb_state_) {
        thumb_state_->release_all();
        delete thumb_state_;
    }
}

// Uniform layout: 48 floats (192 bytes)
//   [0]  steps, [1] current_step, [2] gate, [3] pad
//   [4..35] pattern (32 floats), [36..47] pad
static constexpr uint64_t kUniformSize = 48 * sizeof(float);

void EuclideanCore::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx) return;
    if (!thumb_state_) thumb_state_ = new EuclideanThumbState();

    if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
        rebuild_thumb_pipeline(ctx);
    }
    if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
        vivid_report_thumbnail_error(ctx, "euclidean thumbnail pipeline init failed");
        return;
    }

    // Read live state from output values
    int n = (ctx->param_count > 1) ? static_cast<int>(ctx->param_values[1]) : 8;
    float cur_step = (ctx->output_count > 2) ? ctx->output_values[2] : 0.0f;
    float gate     = (ctx->output_count > 1) ? ctx->output_values[1] : 0.0f;

    float uniforms[48] = {};
    uniforms[0] = static_cast<float>(n);
    uniforms[1] = cur_step;
    uniforms[2] = gate;
    const int* pat = current_pattern();
    for (int i = 0; i < 32 && i < n; ++i)
        uniforms[4 + i] = static_cast<float>(pat[i]);

    wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, uniforms, sizeof(uniforms));
    vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group,
                               "Euclidean Thumb Pass");
}

void EuclideanCore::rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
    thumb_state_->release_all();

    static const char* kFragment = R"(
struct Uniforms {
    steps:        f32,
    current_step: f32,
    gate:         f32,
    pad0:         f32,
    pattern:      array<vec4f, 8>,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

fn get_pattern(i: i32) -> f32 {
    let vec_idx = i / 4;
    let comp = i % 4;
    let v = u.pattern[vec_idx];
    return select(select(select(v.w, v.z, comp == 2), v.y, comp == 1), v.x, comp == 0);
}

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
    let steps = i32(u.steps);
    if (steps <= 0) { return vec4f(0.0, 0.0, 0.0, 0.0); }

    let uv = input.uv;
    // Pixel size for anti-aliasing (in UV space)
    let px = fwidth(uv.x);
    let py = fwidth(uv.y);

    // Layout: horizontal row of cells with margins for label space
    let margin_x = 0.06;
    let margin_top = 0.28;
    let margin_bot = 0.08;

    let cell_area_x = margin_x;
    let cell_area_w = 1.0 - 2.0 * margin_x;
    let cell_area_y = margin_top;
    let cell_area_h = 1.0 - margin_top - margin_bot;

    let local_x = (uv.x - cell_area_x) / cell_area_w;
    let local_y = (uv.y - cell_area_y) / cell_area_h;

    if (local_x < 0.0 || local_x > 1.0 || local_y < 0.0 || local_y > 1.0) {
        return vec4f(0.0, 0.0, 0.0, 0.0);
    }

    let cell_idx_f = local_x * f32(steps);
    let cell_idx = i32(floor(cell_idx_f));
    let in_cell_x = fract(cell_idx_f);

    // Gap between cells — scale with pixel size so gaps are always >= 1px
    let min_gap = px / cell_area_w * f32(steps);  // 1 pixel in cell-fraction space
    let gap_frac = max(select(0.08, 0.03, steps > 16), min_gap);
    let half_gap = gap_frac * 0.5;

    // Reject gap areas
    if (in_cell_x < half_gap || in_cell_x > (1.0 - half_gap)) {
        return vec4f(0.0, 0.0, 0.0, 0.0);
    }
    if (local_y < 0.04 || local_y > 0.96) {
        return vec4f(0.0, 0.0, 0.0, 0.0);
    }

    // Remap to [0,1] within cell
    let cx = (in_cell_x - half_gap) / (1.0 - 2.0 * half_gap);
    let cy = (local_y - 0.04) / 0.92;

    // Rounded rect SDF in cell-local space
    let corner_r = 0.15;
    let p = vec2f(cx, cy) * 2.0 - vec2f(1.0);
    let q = abs(p) - vec2f(1.0 - corner_r);
    let d = length(max(q, vec2f(0.0))) - corner_r;

    // Anti-aliased edge: compute pixel width in SDF space
    let sdf_px = length(fwidth(p)) * 0.5;
    let edge_alpha = 1.0 - smoothstep(-sdf_px, sdf_px, d);
    if (edge_alpha < 0.01) {
        return vec4f(0.0, 0.0, 0.0, 0.0);
    }

    let is_hit = get_pattern(cell_idx) > 0.5;
    let is_current = cell_idx == i32(u.current_step);
    let is_gating = is_current && u.gate > 0.5;

    // Accent color (matches control-cadence node tint)
    let accent = vec3f(0.45, 0.55, 0.65);
    let dim    = vec3f(0.15, 0.17, 0.2);

    var color: vec3f;
    var alpha: f32;

    if (is_hit) {
        color = accent;
        alpha = select(0.6, 1.0, is_gating);
    } else {
        color = dim;
        alpha = select(0.25, 0.45, is_current);
    }

    // Current-step highlight ring: 2px border using SDF
    if (is_current) {
        let ring_width = sdf_px * 4.0;  // ~2 pixels thick
        let ring = smoothstep(-ring_width - sdf_px, -ring_width, d);
        color = mix(color, accent * 1.4, ring);
        alpha = mix(alpha, 0.95, ring);
    }

    return vec4f(color * alpha * edge_alpha, alpha * edge_alpha);
}
)";

    thumb_state_->shader = vivid::thumbnail::create_shader(ctx->device, kFragment,
                                                            "Euclidean Thumb Shader");
    thumb_state_->uniform_buf = vivid::thumbnail::create_uniform_buffer(ctx->device,
                                    kUniformSize, "Euclidean Thumb Uniforms");
    thumb_state_->bind_layout = vivid::thumbnail::create_uniform_bind_layout(ctx->device,
                                    kUniformSize, "Euclidean Thumb BGL");
    thumb_state_->pipe_layout = vivid::thumbnail::create_pipeline_layout(ctx->device,
                                    thumb_state_->bind_layout, "Euclidean Thumb Layout");
    thumb_state_->bind_group = vivid::thumbnail::create_uniform_bind_group(ctx->device,
                                   thumb_state_->bind_layout, thumb_state_->uniform_buf,
                                   kUniformSize, "Euclidean Thumb BG");
    thumb_state_->pipeline = vivid::thumbnail::create_pipeline(ctx->device, thumb_state_->shader,
                                 thumb_state_->pipe_layout, ctx->thumbnail_format,
                                 "Euclidean Thumb Pipeline");
    thumb_state_->pipeline_format = ctx->thumbnail_format;
}
