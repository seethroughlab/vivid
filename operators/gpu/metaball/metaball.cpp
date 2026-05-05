#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cmath>
#include <string>

// =============================================================================
// Metaball WGSL Fragment Shader
// =============================================================================

static const char* kMetaballFragment = R"(

struct Ball {
    pos: vec2f,
    radius: f32,
    hue: f32,
}

struct Uniforms {
    resolution: vec2f,
    time: f32,
    count: f32,
    threshold: f32,
    softness: f32,
    glow: f32,
    color_mode: f32,
    color_r: f32,
    color_g: f32,
    color_b: f32,
    render_mode: f32,
    balls: array<vec4f, 16>,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

fn hsv2rgb(h: f32, s: f32, v: f32) -> vec3f {
    let c = v * s;
    let hp = h * 6.0;
    let x = c * (1.0 - abs(hp % 2.0 - 1.0));
    var rgb = vec3f(0.0);
    if (hp < 1.0) { rgb = vec3f(c, x, 0.0); }
    else if (hp < 2.0) { rgb = vec3f(x, c, 0.0); }
    else if (hp < 3.0) { rgb = vec3f(0.0, c, x); }
    else if (hp < 4.0) { rgb = vec3f(0.0, x, c); }
    else if (hp < 5.0) { rgb = vec3f(x, 0.0, c); }
    else { rgb = vec3f(c, 0.0, x); }
    return rgb + (v - c);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let aspect = u.resolution.x / u.resolution.y;
    let uv = vec2f(input.uv.x * aspect, input.uv.y);
    let n = i32(u.count);
    let render_mode = i32(u.render_mode);

    // --- Circles mode (BouncingBalls) ---
    if (render_mode == 1) {
        var best_dist = 999.0;
        var best_hue = 0.0;
        var best_r = 0.0;
        var best_delta = vec2f(0.0);
        var hit = false;
        for (var i = 0; i < n; i++) {
            let ball = u.balls[i];
            let bpos = vec2f(ball.x * aspect, ball.y);
            let r = ball.z;
            let delta = uv - bpos;
            let d = length(delta);
            if (d < r && d < best_dist) {
                best_dist = d;
                best_hue = ball.w;
                best_r = r;
                best_delta = delta;
                hit = true;
            }
        }
        if (!hit) {
            return vec4f(0.0, 0.0, 0.0, 0.0);
        }
        // Radial gradient: color at center → black at edge (matching Paper.js)
        let t = best_dist / best_r;
        let val = max(0.0, 1.0 - t * t);  // quadratic falloff to black

        // Specular highlight: bright spot offset from center (Paper.js inner circle)
        let highlight_offset = best_r * 0.125;  // radius/8
        let highlight_radius = best_r * 0.333;  // radius/3
        let highlight_center = vec2f(-highlight_offset, -highlight_offset);
        let highlight_dist = length(best_delta - highlight_center);
        let highlight = smoothstep(highlight_radius, 0.0, highlight_dist) * 0.4;

        let color_mode = i32(u.color_mode);
        var color = vec3f(u.color_r, u.color_g, u.color_b);
        if (color_mode == 1) {
            color = hsv2rgb(fract(best_hue), 1.0, 1.0);  // sat=1.0 like Paper.js
        }

        // Anti-aliased edge
        let edge = 1.0 - smoothstep(best_r - best_r * 0.02, best_r, best_dist);

        return vec4f(color * (val + highlight) * edge, edge);
    }

    // --- Metaball SDF mode (default) ---
    var field = 0.0;
    var weighted_hue = 0.0;
    var total_weight = 0.0;

    for (var i = 0; i < n; i++) {
        let ball = u.balls[i];
        let bpos = vec2f(ball.x * aspect, ball.y);
        let r = ball.z;
        let hue = ball.w;

        let d = length(uv - bpos);
        let contribution = (r * r) / (d * d + 0.0001);
        field += contribution;

        // Weight for per-ball coloring
        weighted_hue += hue * contribution;
        total_weight += contribution;
    }

    let color_mode = i32(u.color_mode);
    var color = vec3f(u.color_r, u.color_g, u.color_b);

    if (color_mode == 1 && total_weight > 0.0) {
        // Per-ball rainbow
        let avg_hue = weighted_hue / total_weight;
        color = hsv2rgb(fract(avg_hue), 0.8, 1.0);
    } else if (color_mode == 2) {
        // Field gradient
        let intensity = clamp(field / u.threshold, 0.0, 2.0);
        color = hsv2rgb(fract(intensity * 0.3 + u.time * 0.05), 0.7, 1.0);
    }

    // Surface with soft edge
    let surface = smoothstep(u.threshold - u.softness, u.threshold + u.softness, field);

    // Glow beyond surface
    let glow_falloff = u.glow * smoothstep(0.0, u.threshold * 0.8, field) * (1.0 - surface) * 0.5;

    let intensity = surface + glow_falloff;
    return vec4f(color * intensity, intensity);
}
)";

// =============================================================================
// Uniform struct matching the WGSL layout
// =============================================================================

struct MetaballUniforms {
    float resolution[2];
    float time;
    float count;
    float threshold;
    float softness;
    float glow;
    float color_mode;
    float color_r;
    float color_g;
    float color_b;
    float render_mode;
    float balls[16 * 4];  // array<vec4f, 16> — xy=position, z=radius, w=hue
};
/**
 * @brief 2D metaballs with implicit surface blending and multiple render modes.
 *
 * Simulates up to 16 bouncing blobs using inverse-distance field
 * accumulation. Supports SDF and circle render modes with per-ball
 * rainbow, field gradient, or single-color output.
 *
 * @param threshold Field strength cutoff for surface detection.
 * @see Fluid, Shape
 */
struct Metaball : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Metaball";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_KERNEL;

    vivid::Param<int>   count      {"count",      8,    1,    16};
    vivid::Param<float> threshold  {"threshold",  1.0f, 0.1f, 5.0f};
    vivid::Param<float> softness   {"softness",   0.01f, 0.001f, 0.05f};
    vivid::Param<float> glow       {"glow",       0.5f, 0.0f, 2.0f};
    vivid::Param<int>   color_mode {"color_mode", 1,    0,    2};
    vivid::Param<float> r          {"r",          0.2f, 0.0f, 1.0f};
    vivid::Param<float> g          {"g",          0.6f, 0.0f, 1.0f};
    vivid::Param<float> b          {"b",          1.0f, 0.0f, 1.0f};
    vivid::Param<int>   render_mode{"render_mode", 0,    0,    1};

    Metaball() {
        vivid::description(count, "Number of metaballs, 1-16");
        vivid::description(threshold, "Field strength cutoff for the metaball surface boundary");
        vivid::description(softness, "Width of the soft transition at the surface edge");
        vivid::description(glow, "Intensity of the glow halo beyond the surface");
        vivid::description(color_mode, "Coloring mode: Solid, per-ball Rainbow, or field Gradient");
        vivid::description(r, "Red component of the base color (Solid mode)");
        vivid::description(g, "Green component of the base color (Solid mode)");
        vivid::description(b, "Blue component of the base color (Solid mode)");
        vivid::description(render_mode, "Render style: Metaball SDF blending or discrete Circles");
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
        static const char* mode_labels[] = {"Solid", "Rainbow", "Gradient"};
        color_mode.choice_labels = mode_labels;
        color_mode.choice_count  = 3;

        static const char* render_labels[] = {"Metaball", "Circles"};
        render_mode.choice_labels = render_labels;
        render_mode.choice_count  = 2;

        display_hint(r, VIVID_DISPLAY_COLOR);
        display_hint(g, VIVID_DISPLAY_COLOR);
        display_hint(b, VIVID_DISPLAY_COLOR);

        out.push_back(&count);
        out.push_back(&threshold);
        out.push_back(&softness);
        out.push_back(&glow);
        out.push_back(&color_mode);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&render_mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"pos_x",   VIVID_PORT_LANE_ARRAY,  VIVID_PORT_INPUT});   // lane input 0
        out.push_back({"pos_y",   VIVID_PORT_LANE_ARRAY,  VIVID_PORT_INPUT});   // lane input 1
        out.push_back({"radii",   VIVID_PORT_LANE_ARRAY,  VIVID_PORT_INPUT});   // lane input 2
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[metaball] lazy_init FAILED\n");
                return;
            }
        }

        int n = count.int_value();
        if (n < 1) n = 1;
        if (n > 16) n = 16;

        MetaballUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.time       = static_cast<float>(ctx->time);
        u.count      = static_cast<float>(n);
        u.threshold  = threshold.value;
        u.softness   = softness.value;
        u.glow       = glow.value;
        u.color_mode = static_cast<float>(color_mode.int_value());
        u.color_r    = r.value;
        u.color_g    = g.value;
        u.color_b    = b.value;
        u.render_mode = static_cast<float>(render_mode.int_value());

        // Pack ball data from lane inputs (or generate defaults)
        for (int i = 0; i < n; ++i) {
            float px = 0.5f, py = 0.5f, rad = 0.08f;

            // Read lane inputs if connected
            if (ctx->input_lanes) {
                auto& sp_x = ctx->input_lanes[0];
                if (sp_x.data && static_cast<uint32_t>(i) < sp_x.length)
                    px = sp_x.data[i];

                auto& sp_y = ctx->input_lanes[1];
                if (sp_y.data && static_cast<uint32_t>(i) < sp_y.length)
                    py = sp_y.data[i];

                auto& sp_r = ctx->input_lanes[2];
                if (sp_r.data && static_cast<uint32_t>(i) < sp_r.length)
                    rad = sp_r.data[i];
            }

            // Fallback: distribute in a circle if no lane input
            if (!ctx->input_lanes || !ctx->input_lanes[0].data) {
                float angle = static_cast<float>(i) / static_cast<float>(n) * 6.2831853f;
                float phase = static_cast<float>(ctx->time) * 0.5f;
                px = 0.5f + 0.25f * std::cos(angle + phase);
                py = 0.5f + 0.25f * std::sin(angle + phase * 0.7f);
                rad = 0.06f + 0.03f * std::sin(phase * 1.3f + static_cast<float>(i));
            }

            u.balls[i * 4 + 0] = px;
            u.balls[i * 4 + 1] = py;
            u.balls[i * 4 + 2] = rad;
            u.balls[i * 4 + 3] = static_cast<float>(i) / static_cast<float>(n); // hue
        }

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "Metaball Pass");
    }

    ~Metaball() override {
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

    bool lazy_init(const VividGpuContext* gpu) {
        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kMetaballFragment;
        shader_ = vivid::gpu::create_shader(gpu->device, frag.c_str(), "Metaball Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(MetaballUniforms), "Metaball Uniforms");

        WGPUBindGroupLayoutEntry bgl_entry{};
        bgl_entry.binding = 0;
        bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entry.buffer.minBindingSize = sizeof(MetaballUniforms);

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Metaball BGL");
        bgl_desc.entryCount = 1;
        bgl_desc.entries = &bgl_entry;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Metaball Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer  = uniform_buf_;
        bg_entry.offset  = 0;
        bg_entry.size    = sizeof(MetaballUniforms);

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Metaball Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_, gpu->output_format, "Metaball Pipeline");
        if (!pipeline_) return false;

        return true;
    }
};

VIVID_DEFINE_OP(Metaball) {
}

