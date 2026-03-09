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

    let d = sdShape(uv);
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
};

// =============================================================================
// Shape Operator
// =============================================================================

struct Shape : vivid::OperatorBase {
    static constexpr const char* kName   = "Shape";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> radius   {"radius",   0.3f,  0.01f, 1.0f};
    vivid::Param<int>   sides    {"sides",    4,     3,     64};
    vivid::Param<float> star     {"star",     0.0f,  0.0f,  0.9f};
    vivid::Param<float> rotation {"rotation", 0.0f,  0.0f,  6.28f};
    vivid::Param<float> softness {"softness", 0.005f, 0.0f, 0.1f};
    vivid::Param<float> r        {"r",        1.0f,  0.0f,  1.0f};
    vivid::Param<float> g        {"g",        1.0f,  0.0f,  1.0f};
    vivid::Param<float> b        {"b",        1.0f,  0.0f,  1.0f};

    Shape() {
        vivid::semantic_tag(rotation, "rotation_radians");
        vivid::semantic_shape(rotation, "scalar");
        vivid::semantic_unit(rotation, "rad");

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
        display_hint(r, VIVID_DISPLAY_COLOR);
        display_hint(g, VIVID_DISPLAY_COLOR);
        display_hint(b, VIVID_DISPLAY_COLOR);

        out.push_back(&radius);
        out.push_back(&sides);
        out.push_back(&star);
        out.push_back(&rotation);
        out.push_back(&softness);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process(VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) {
            if (ctx->frame % 60 == 0) std::fprintf(stderr, "[shape] gpu is NULL\n");
            return;
        }

        if (!pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[shape] lazy_init FAILED\n");
                return;
            }
        }

        // Update uniforms
        ShapeUniforms u{};
        u.resolution[0] = static_cast<float>(gpu->output_width);
        u.resolution[1] = static_cast<float>(gpu->output_height);
        u.radius      = radius.value;
        u.sides       = static_cast<float>(sides.int_value());
        u.star_factor = star.value;
        u.rotation    = rotation.value;
        u.softness    = softness.value;
        u.color_r     = r.value;
        u.color_g     = g.value;
        u.color_b     = b.value;

        wgpuQueueWriteBuffer(gpu->queue, uniform_buf_, 0, &u, sizeof(u));

        vivid::gpu::run_pass(gpu->command_encoder, pipeline_, bind_group_,
                             gpu->output_texture_view, "Shape Pass");
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

    bool lazy_init(VividGpuState* gpu) {
        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kShapeFragment;
        shader_ = vivid::gpu::create_shader(gpu->device, frag.c_str(), "Shape Shader");
        if (!shader_) return false;

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
