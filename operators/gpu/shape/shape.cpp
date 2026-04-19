#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <string>

// =============================================================================
// Shape WGSL Fragment Shader (vertex code + constants come from gpu_common.h)
// =============================================================================

static const char* kShapeFragment = R"(

struct Uniforms {
    resolution: vec2f,
    radius: f32,
    sides: f32,
    star_factor: f32,
    rotation: f32,
    softness: f32,
    color_r: f32,
    color_g: f32,
    color_b: f32,
    position_x: f32,
    position_y: f32,
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

fn sdShape(p_in: vec2f) -> f32 {
    let n = uniforms.sides;
    let r = uniforms.radius;
    let sf = uniforms.star_factor;
    let rot = uniforms.rotation;

    // Apply rotation
    let c = cos(rot);
    let s = sin(rot);
    let p = vec2f(c * p_in.x + s * p_in.y, -s * p_in.x + c * p_in.y);

    let an = PI / n;  // half-sector angle
    let angle = atan2(p.y, p.x);

    if (sf < 0.001) {
        // Regular polygon SDF — fold into one sector, measure distance to edge
        let sector = round(angle / (2.0 * an));
        let folded = angle - sector * 2.0 * an;
        let q = length(p) * vec2f(cos(folded), abs(sin(folded)));
        return q.x - r * cos(an);
    } else {
        // Star SDF — 2N alternating vertices with outer/inner radii
        let inner_r = r * (1.0 - sf);
        let sector = round(angle / (2.0 * an));
        let folded = abs(angle - sector * 2.0 * an);

        let lp = length(p);
        let q = vec2f(lp * cos(folded), lp * sin(folded));

        // Edge from outer vertex (r, 0) to inner vertex at angle an
        let v0 = vec2f(r, 0.0);
        let v1 = vec2f(inner_r * cos(an), inner_r * sin(an));
        let edge = v1 - v0;
        // Outward-facing normal (right-hand normal of edge direction)
        let normal = normalize(vec2f(edge.y, -edge.x));
        return dot(q - v0, normal);
    }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let aspect = uniforms.resolution.x / uniforms.resolution.y;
    // Center UV so (0,0) is at screen center, aspect-corrected
    let uv = vec2f((input.uv.x - 0.5) * aspect, input.uv.y - 0.5);

    // Recenter sample point on the requested position. +x = right, +y = down;
    // one full position unit spans the full frame, so ±0.5 places the shape
    // center at the left/right or top/bottom edge regardless of aspect ratio.
    let pos = vec2f(uniforms.position_x * aspect, uniforms.position_y);
    let d = sdShape(uv - pos);
    let alpha = 1.0 - smoothstep(-uniforms.softness, uniforms.softness, d);

    let color = vec3f(uniforms.color_r, uniforms.color_g, uniforms.color_b);
    return vec4f(color * alpha, alpha);
}
)";

// =============================================================================
// Uniform struct matching the WGSL Uniforms
// =============================================================================

struct ShapeUniforms {
    float resolution[2];
    float radius;
    float sides;
    float star_factor;
    float rotation;
    float softness;
    float color_r;
    float color_g;
    float color_b;
    float position_x;
    float position_y;
};

/**
 * @brief SDF polygon/star shape generator with soft edges.
 *
 * Renders a regular polygon or star using a signed-distance-field evaluated
 * in a fragment shader. The shape is aspect-corrected; use `position_x` and
 * `position_y` to place it off-center (±0.5 places the center at an edge)
 * without needing a separate Transform node. Increase `sides` for rounder
 * shapes (64 = near-circle). Enable `star` to pull alternating vertices
 * inward, creating star patterns.
 *
 * Output is premultiplied-alpha — composite it with Composite or layer
 * it over other generators.
 *
 * @tip Set sides=64 and star=0 for a soft circle. Pair with Trails for motion blur.
 * @tip Modulate rotation with an LFO for spinning shapes.
 * @tip Drive position_x/y from control signals to move the shape musically.
 * @see Noise, Composite, Trails
 * @param star How far inner vertices pull inward. 0 = regular polygon, 0.9 = extreme spikes.
 * @param softness Width of the antialiased edge. Higher values give a glow-like falloff.
 * @output texture The rendered shape on a transparent background.
 */
struct Shape : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Shape";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> radius     {"radius",     0.3f,  0.01f, 1.0f};
    vivid::Param<int>   sides      {"sides",      4,     3,     64};
    vivid::Param<float> star       {"star",       0.0f,  0.0f,  0.9f};
    vivid::Param<float> rotation   {"rotation",   0.0f,  0.0f,  6.28f};
    vivid::Param<float> softness   {"softness",   0.0f,  0.0f,  0.1f};
    vivid::Param<float> position_x {"position_x", 0.0f, -1.0f,  1.0f};
    vivid::Param<float> position_y {"position_y", 0.0f, -1.0f,  1.0f};
    vivid::Param<float> r          {"r",          1.0f,  0.0f,  1.0f};
    vivid::Param<float> g          {"g",          1.0f,  0.0f,  1.0f};
    vivid::Param<float> b          {"b",          1.0f,  0.0f,  1.0f};

    Shape() {
        vivid::description(radius, "Size of the shape from center to edge");
        vivid::description(sides, "Number of polygon sides, 64 is nearly a circle");
        vivid::description(star, "How far inner vertices pull inward, 0 = regular polygon");
        vivid::description(rotation, "Shape rotation in radians");
        vivid::description(softness, "Width of the antialiased edge, higher values add glow");
        vivid::description(position_x, "Horizontal position. 0 = center, -0.5 = left edge, +0.5 = right edge");
        vivid::description(position_y, "Vertical position. 0 = center, -0.5 = top edge, +0.5 = bottom edge");
        vivid::description(r, "Red component of the shape color");
        vivid::description(g, "Green component of the shape color");
        vivid::description(b, "Blue component of the shape color");

        vivid::semantic_tag(rotation, "rotation_radians");
        vivid::semantic_shape(rotation, "scalar");
        vivid::semantic_unit(rotation, "rad");

        vivid::semantic_tag(position_x, "position_xy");
        vivid::semantic_shape(position_x, "scalar");
        vivid::semantic_intent(position_x, "x_component");
        vivid::semantic_tag(position_y, "position_xy");
        vivid::semantic_shape(position_y, "scalar");
        vivid::semantic_intent(position_y, "y_component");

        vivid::semantic_tag(r, "color_rgba");
        vivid::semantic_shape(r, "scalar");
        vivid::semantic_intent(r, "color_red");
        vivid::semantic_tag(g, "color_rgba");
        vivid::semantic_shape(g, "scalar");
        vivid::semantic_intent(g, "color_green");
        vivid::semantic_tag(b, "color_rgba");
        vivid::semantic_shape(b, "scalar");
        vivid::semantic_intent(b, "color_blue");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(position_x, VIVID_DISPLAY_XY_PAD);
        display_hint(position_y, VIVID_DISPLAY_XY_PAD);
        display_hint(r, VIVID_DISPLAY_COLOR);
        display_hint(g, VIVID_DISPLAY_COLOR);
        display_hint(b, VIVID_DISPLAY_COLOR);

        out.push_back(&radius);
        out.push_back(&sides);
        out.push_back(&star);
        out.push_back(&rotation);
        out.push_back(&softness);
        out.push_back(&position_x);
        out.push_back(&position_y);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (init_failed_) {
            vivid_report_gpu_error(ctx, shader_error_msg_.c_str());
            return;
        }
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                init_failed_ = true;
                return;
            }
        }

        // Update uniforms
        ShapeUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.radius      = radius.value;
        u.sides       = static_cast<float>(sides.int_value());
        u.star_factor = star.value;
        u.rotation    = rotation.value;
        u.softness    = softness.value;
        u.color_r     = r.value;
        u.color_g     = g.value;
        u.color_b     = b.value;
        u.position_x  = position_x.value;
        u.position_y  = position_y.value;

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "Shape Pass");
    }

    ~Shape() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
    }

private:
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    bool                init_failed_      = false;  // set on shader error; cleared on reload
    std::string         shader_error_msg_;

    bool lazy_init(const VividGpuContext* gpu) {
        // Error scope guards shader creation — see noise.cpp for full explanation.
        wgpuDevicePushErrorScope(gpu->device, WGPUErrorFilter_Validation);
        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kShapeFragment;
        shader_ = vivid::gpu::create_shader(gpu->device, frag.c_str(), "Shape Shader");
        {
            WGPUPopErrorScopeCallbackInfo cb{};
            cb.mode = WGPUCallbackMode_AllowSpontaneous;
            cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type,
                              WGPUStringView msg, void* ud, void*) {
                if (type != WGPUErrorType_NoError) {
                    auto* self = static_cast<Shape*>(ud);
                    self->shader_error_msg_ = msg.data
                        ? std::string(msg.data, msg.length) : "unknown WGSL error";
                    std::fprintf(stderr, "[shape] WGSL error — keeping black output. %s\n",
                                 self->shader_error_msg_.c_str());
                }
            };
            cb.userdata1 = this;
            wgpuDevicePopErrorScope(gpu->device, cb);
        }
        // wgpu-native fires error-scope callbacks synchronously during popErrorScope.
        if (!shader_error_msg_.empty() || !shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(ShapeUniforms), "Shape Uniforms");

        // Bind group layout: uniform at binding 0
        WGPUBindGroupLayoutEntry bgl_entry{};
        bgl_entry.binding = 0;
        bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entry.buffer.minBindingSize = sizeof(ShapeUniforms);

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Shape BGL");
        bgl_desc.entryCount = 1;
        bgl_desc.entries = &bgl_entry;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        // Pipeline layout
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Shape Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // Bind group
        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer  = uniform_buf_;
        bg_entry.offset  = 0;
        bg_entry.size    = sizeof(ShapeUniforms);

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Shape Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_, gpu->output_format, "Shape Pipeline");
        if (!pipeline_) return false;

        return true;
    }
};

VIVID_REGISTER(Shape)
