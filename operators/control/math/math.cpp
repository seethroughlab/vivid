#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include <cmath>
#include <algorithm>

struct MathThumbState {
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
/**
 * @brief Binary math operation on two control signals.
 *
 * Performs add, multiply, min, or max on inputs A and B. Chain multiple
 * Math operators for complex expressions.
 *
 * @see Logic, Macro, Quantizer
 */
struct Math : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "Math";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> operation{"operation", 0, {"add", "multiply", "min", "max"}};

    MathThumbState* thumb_state_ = nullptr;

    ~Math() override {
        if (thumb_state_) { thumb_state_->release_all(); delete thumb_state_; }
    }

    Math() {
        vivid::description(operation, "Binary operation applied to inputs A and B");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&operation);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"b",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"result", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    float compute(float a, float b, int op) const {
        switch (op) {
            case 0: return a + b;
            case 1: return a * b;
            case 2: return std::min(a, b);
            case 3: return std::max(a, b);
        }
        return 0.0f;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_state_) thumb_state_ = new MathThumbState();
        if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
            vivid_report_thumbnail_error(ctx, "math thumbnail pipeline init failed");
            return;
        }
        struct Uniforms { float op, pad0, pad1, pad2; } u{};
        u.op = ctx->param_values[0];
        wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group, "Math Thumb Pass");
    }

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
        thumb_state_->release_all();
        static const char* kShader = R"(
struct Uniforms { data: vec4f, };
struct VertexOutput { @builtin(position) position: vec4f, @location(0) uv: vec2f, }
@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

// SDF for + symbol
fn sdf_plus(p: vec2f, size: f32, thickness: f32) -> f32 {
    let q = abs(p);
    let d = min(max(q.x - thickness, q.y - size), max(q.x - size, q.y - thickness));
    return d;
}

// SDF for x (multiply) symbol
fn sdf_cross(p: vec2f, size: f32, thickness: f32) -> f32 {
    let r = mat2x2f(0.7071, -0.7071, 0.7071, 0.7071) * p;
    return sdf_plus(r, size, thickness);
}

// SDF for ∧ (min / down chevron)
fn sdf_min(p: vec2f, size: f32, thickness: f32) -> f32 {
    let q = vec2f(abs(p.x), p.y + size * 0.3);
    let d = abs(q.x + q.y * 0.8 - size * 0.5) - thickness;
    return d;
}

// SDF for ∨ (max / up chevron)
fn sdf_max(p: vec2f, size: f32, thickness: f32) -> f32 {
    return sdf_min(vec2f(p.x, -p.y), size, thickness);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let op = i32(uniforms.data.x);

    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let glyph_col = vec4f(160.0/255.0, 200.0/255.0, 240.0/255.0, 240.0/255.0);

    // Center coordinates, aspect-corrected
    let aspect = 140.0 / 88.0;
    let p = vec2f((uv.x - 0.5) * aspect, uv.y - 0.5);
    let size = 0.28;
    let thick = 0.06;

    var d = 1.0f;
    switch (op) {
        case 0: { d = sdf_plus(p, size, thick); }
        case 1: { d = sdf_cross(p, size * 0.85, thick); }
        case 2: { d = sdf_min(p, size, thick); }
        case 3: { d = sdf_max(p, size, thick); }
        default: { d = sdf_plus(p, size, thick); }
    }

    let aa = fwidth(d) * 1.5;
    let alpha = 1.0 - smoothstep(-aa, aa, d);
    if (alpha < 0.01) { return bg; }
    return vec4f(glyph_col.rgb, glyph_col.a * alpha);
}
)";
        thumb_state_->shader = vivid::thumbnail::create_shader(ctx->device, kShader, "Math Thumb Shader");
        thumb_state_->uniform_buf =
            vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Math Thumb Uniforms");
        thumb_state_->bind_layout =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Math Thumb BGL");
        thumb_state_->pipe_layout =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_state_->bind_layout, "Math Thumb Layout");
        thumb_state_->bind_group = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_state_->bind_layout, thumb_state_->uniform_buf, sizeof(float) * 4, "Math Thumb BG");
        thumb_state_->pipeline = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_state_->shader, thumb_state_->pipe_layout, ctx->thumbnail_format, "Math Thumb Pipeline");
        thumb_state_->pipeline_format = ctx->thumbnail_format;
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = compute(ctx->input_values[0], ctx->input_values[1],
                                        operation.int_value());
    }

};

VIVID_REGISTER(Math)
VIVID_THUMBNAIL(Math)
