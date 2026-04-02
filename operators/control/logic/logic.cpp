#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"

struct LogicThumbState {
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
 * @brief Boolean logic gate operating on two control signals.
 *
 * Applies AND, OR, XOR, NOT, NAND, or NOR to two inputs (threshold
 * > 0.5 = true). NOT only uses input A.
 *
 * @see Math, Gate
 */
struct Logic : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "Logic";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> operation{"operation", 0, {"AND", "OR", "XOR", "NOT", "NAND", "NOR"}};

    LogicThumbState* thumb_state_ = nullptr;

    ~Logic() override {
        if (thumb_state_) { thumb_state_->release_all(); delete thumb_state_; }
    }

    Logic() {
        vivid::description(operation, "Boolean operation to apply (NOT uses only input A)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&operation);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"b",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"result", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    float compute(float a_val, float b_val, int op) const {
        bool a = a_val > 0.5f;
        bool b = b_val > 0.5f;
        bool result = false;
        switch (op) {
            case 0: result = a && b;     break;  // AND
            case 1: result = a || b;     break;  // OR
            case 2: result = a != b;     break;  // XOR
            case 3: result = !a;         break;  // NOT
            case 4: result = !(a && b);  break;  // NAND
            case 5: result = !(a || b);  break;  // NOR
        }
        return result ? 1.0f : 0.0f;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_state_) thumb_state_ = new LogicThumbState();
        if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
            vivid_report_thumbnail_error(ctx, "logic thumbnail pipeline init failed");
            return;
        }
        struct Uniforms { float op, output_val, pad0, pad1; } u{};
        u.op = ctx->param_values[0];
        u.output_val = (ctx->output_count > 0) ? ctx->output_values[0] : 0.0f;
        wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group, "Logic Thumb Pass");
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

// SDF for AND gate shape (D-shape)
fn sdf_and(p: vec2f, size: f32) -> f32 {
    let q = p / size;
    // Left edge is flat, right edge is semicircle
    let d_circle = length(vec2f(max(q.x, 0.0), q.y)) - 0.8;
    let d_rect = max(abs(q.x + 0.2) - 0.6, abs(q.y) - 0.8);
    return min(d_circle, d_rect) * size;
}

// SDF for OR gate shape (pointed D)
fn sdf_or(p: vec2f, size: f32) -> f32 {
    let q = p / size;
    let d = length(vec2f(max(q.x - 0.1, 0.0), q.y)) - 0.8;
    let d_back = (q.x + 0.5) + 0.3 * (1.0 - q.y * q.y * 1.6);
    return max(d, -d_back) * size;
}

// Inversion bubble
fn sdf_bubble(p: vec2f, cx: f32, size: f32) -> f32 {
    return length(p - vec2f(cx, 0.0)) - size;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let op = i32(uniforms.data.x);
    let output_on = uniforms.data.y > 0.5;

    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let on_col = vec4f(80.0/255.0, 190.0/255.0, 120.0/255.0, 220.0/255.0);
    let off_col = vec4f(120.0/255.0, 140.0/255.0, 170.0/255.0, 220.0/255.0);
    let glyph_col = select(off_col, on_col, output_on);

    let aspect = 140.0 / 88.0;
    let p = vec2f((uv.x - 0.5) * aspect, uv.y - 0.5);
    let size = 0.32;

    var d = 1.0f;
    var has_bubble = false;

    // AND=0, OR=1, XOR=2, NOT=3, NAND=4, NOR=5
    switch (op) {
        case 0: { d = sdf_and(p, size); }                                    // AND
        case 1: { d = sdf_or(p, size); }                                     // OR
        case 2: { d = sdf_or(p, size); }                                     // XOR (OR shape, distinguished by context)
        case 3: { d = sdf_and(p, size); has_bubble = true; }                 // NOT
        case 4: { d = sdf_and(p, size); has_bubble = true; }                 // NAND
        case 5: { d = sdf_or(p, size); has_bubble = true; }                  // NOR
        default: { d = sdf_and(p, size); }
    }

    // Inversion bubble for NOT, NAND, NOR
    if (has_bubble) {
        let bd = abs(sdf_bubble(p, size * 1.0, size * 0.15)) - 0.01;
        d = min(d, bd);
    }

    let aa = fwidth(d) * 1.5;

    // Filled gate shape with outline
    let outline_d = abs(d) - 0.015;
    let fill_alpha = 1.0 - smoothstep(-aa, aa, d);
    let outline_alpha = 1.0 - smoothstep(-aa, aa, outline_d);

    if (outline_alpha > 0.01) {
        let interior_alpha = fill_alpha * 0.3;
        let edge_alpha = max(outline_alpha, interior_alpha);
        return vec4f(glyph_col.rgb, glyph_col.a * edge_alpha);
    }

    return bg;
}
)";
        thumb_state_->shader = vivid::thumbnail::create_shader(ctx->device, kShader, "Logic Thumb Shader");
        thumb_state_->uniform_buf =
            vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Logic Thumb Uniforms");
        thumb_state_->bind_layout =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Logic Thumb BGL");
        thumb_state_->pipe_layout =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_state_->bind_layout, "Logic Thumb Layout");
        thumb_state_->bind_group = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_state_->bind_layout, thumb_state_->uniform_buf, sizeof(float) * 4, "Logic Thumb BG");
        thumb_state_->pipeline = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_state_->shader, thumb_state_->pipe_layout, ctx->thumbnail_format, "Logic Thumb Pipeline");
        thumb_state_->pipeline_format = ctx->thumbnail_format;
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = compute(ctx->input_values[0], ctx->input_values[1],
                                        operation.int_value());
    }

};

VIVID_REGISTER(Logic)
VIVID_THUMBNAIL(Logic)
