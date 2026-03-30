// Fluid Simulation — 2D Navier-Stokes (Stable Fluids) with owned LFO
// modulation.
//
// Seven RGBA16Float state textures: velocity[2], pressure[2], dye[2], divergence.
// Seven passes per frame: advect velocity, apply forces, compute divergence,
// pressure solve (N Jacobi iterations), subtract pressure gradient, advect dye,
// visualize. Three owned ChildOp<LFO> instances (viscosity, buoyancy, force)
// modulate simulation parameters.

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/child_op.h"
#include "control/lfo/lfo.h"
#include <algorithm>
#include <cmath>
#include <string>

// ── WGSL shared preamble (uniform struct + bindings) ────────────────────

static const char* kShaderPreamble = R"(

struct Uniforms {
    sim_res: vec2f,
    dt: f32,
    viscosity: f32,
    force_x: f32,
    force_y: f32,
    force_strength: f32,
    emitter_x: f32,
    emitter_y: f32,
    emitter_radius: f32,
    dissipation: f32,
    buoyancy: f32,
    dye_r: f32,
    dye_g: f32,
    dye_b: f32,
    padding: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var tex_a: texture_2d<f32>;
@group(0) @binding(3) var tex_b: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}
)";

// ── Shader 1: Advect Velocity (semi-Lagrangian backtrace) ───────────────

static const char* kAdvectVelFragment = R"(
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let vel = textureSample(tex_a, samp, uv).rg;
    let back = uv - vel * u.dt / u.sim_res;
    return vec4f(textureSample(tex_a, samp, back).rg * 0.999, 0.0, 1.0);
}
)";

// ── Shader 2: Apply Forces (Gaussian splat + buoyancy) ──────────────────

static const char* kForcesFragment = R"(
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    var vel = textureSample(tex_a, samp, uv).rg;

    // Gaussian force splat at emitter
    let emitter = vec2f(u.emitter_x, u.emitter_y);
    let dist = length(uv - emitter);
    let gauss = exp(-dist * dist / (2.0 * u.emitter_radius * u.emitter_radius));
    vel += vec2f(u.force_x, u.force_y) * u.force_strength * gauss * u.dt;

    // Buoyancy: dye density drives upward force
    let dye_density = textureSample(tex_b, samp, uv).r;
    vel.y -= u.buoyancy * dye_density * u.dt;

    return vec4f(vel, 0.0, 1.0);
}
)";

// ── Shader 3: Compute Divergence (central differences) ──────────────────

static const char* kDivergenceFragment = R"(
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let t = 1.0 / u.sim_res;
    let vR = textureSample(tex_a, samp, uv + vec2f(t.x, 0.0)).r;
    let vL = textureSample(tex_a, samp, uv - vec2f(t.x, 0.0)).r;
    let vT = textureSample(tex_a, samp, uv + vec2f(0.0, t.y)).g;
    let vB = textureSample(tex_a, samp, uv - vec2f(0.0, t.y)).g;
    let div = 0.5 * (vR - vL + vT - vB);
    return vec4f(div, 0.0, 0.0, 1.0);
}
)";

// ── Shader 4: Pressure Solve (Jacobi iteration) ─────────────────────────

static const char* kPressureFragment = R"(
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let t = 1.0 / u.sim_res;
    let pL = textureSample(tex_a, samp, uv - vec2f(t.x, 0.0)).r;
    let pR = textureSample(tex_a, samp, uv + vec2f(t.x, 0.0)).r;
    let pB = textureSample(tex_a, samp, uv - vec2f(0.0, t.y)).r;
    let pT = textureSample(tex_a, samp, uv + vec2f(0.0, t.y)).r;
    let div = textureSample(tex_b, samp, uv).r;
    let p = (pL + pR + pB + pT - div) * 0.25;
    return vec4f(p, 0.0, 0.0, 1.0);
}
)";

// ── Shader 5: Subtract Pressure Gradient ────────────────────────────────

static const char* kGradientSubFragment = R"(
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let t = 1.0 / u.sim_res;
    let vel = textureSample(tex_a, samp, uv).rg;
    let pL = textureSample(tex_b, samp, uv - vec2f(t.x, 0.0)).r;
    let pR = textureSample(tex_b, samp, uv + vec2f(t.x, 0.0)).r;
    let pB = textureSample(tex_b, samp, uv - vec2f(0.0, t.y)).r;
    let pT = textureSample(tex_b, samp, uv + vec2f(0.0, t.y)).r;
    let new_vel = vel - 0.5 * vec2f(pR - pL, pT - pB);
    return vec4f(new_vel, 0.0, 1.0);
}
)";

// ── Shader 6: Advect Dye (semi-Lagrangian + emitter inject) ─────────────

static const char* kAdvectDyeFragment = R"(
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;

    // Backtrace through velocity field
    let vel = textureSample(tex_a, samp, uv).rg;
    let back = uv - vel * u.dt / u.sim_res;
    var dye = textureSample(tex_b, samp, back).rgb * u.dissipation;

    // Inject dye at emitter position
    let emitter = vec2f(u.emitter_x, u.emitter_y);
    let dist = length(uv - emitter);
    let gauss = exp(-dist * dist / (2.0 * u.emitter_radius * u.emitter_radius));
    dye += vec3f(u.dye_r, u.dye_g, u.dye_b) * gauss;

    return vec4f(dye, 1.0);
}
)";

// ── Shader 7: Visualize (dye → output) ──────────────────────────────────

static const char* kVisFragment = R"(
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let dye = textureSample(tex_a, samp, input.uv).rgb;
    return vec4f(dye, 1.0);
}
)";

// ── Uniform struct (matches WGSL layout) ────────────────────────────────

struct FluidUniforms {
    float sim_res[2];       // vec2f — simulation grid dimensions
    float dt;               // f32 — delta time (capped)
    float viscosity;        // f32
    float force_x;          // f32 — emitter force direction x
    float force_y;          // f32 — emitter force direction y
    float force_strength;   // f32
    float emitter_x;        // f32
    float emitter_y;        // f32
    float emitter_radius;   // f32
    float dissipation;      // f32
    float buoyancy;         // f32
    float dye_r;            // f32
    float dye_g;            // f32
    float dye_b;            // f32
    float padding;          // f32 — 64 bytes total
};
/**
 * @brief 2D Navier-Stokes fluid simulation with dye advection.
 *
 * Implements Stable Fluids on the GPU: semi-Lagrangian advection, Jacobi
 * pressure solver, and Gaussian force emitter. Dye injection visualizes
 * the flow field. Buoyancy drives dye upward.
 *
 * @param grid_size Simulation resolution. Higher = more detail, more cost.
 * @param iterations Jacobi pressure solver steps. More = more accurate, slower.
 * @see ReactionDiffusion, CellularAutomata
 */
struct Fluid : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Fluid";
    static constexpr bool kTimeDependent = true;

    // ── Simulation parameters ───────────────────────────────────────
    vivid::Param<float> viscosity       {"viscosity",       0.0001f, 0.0f,   0.01f};
    vivid::Param<int>   pressure_iters  {"pressure_iters",  20,      4,      40};
    vivid::Param<float> buoyancy        {"buoyancy",        0.5f,    0.0f,   2.0f};
    vivid::Param<float> dissipation     {"dissipation",     0.98f,   0.8f,   1.0f};

    // ── Emitter ─────────────────────────────────────────────────────
    vivid::Param<float> emitter_x       {"emitter_x",       0.5f,    0.0f,   1.0f};
    vivid::Param<float> emitter_y       {"emitter_y",       0.8f,    0.0f,   1.0f};
    vivid::Param<float> emitter_radius  {"emitter_radius",  0.08f,   0.01f,  0.2f};
    vivid::Param<float> force_strength  {"force_strength",  150.0f,  0.0f,   1000.0f};

    // ── Color ───────────────────────────────────────────────────────
    vivid::Param<float> color_r         {"color_r",         1.0f,    0.0f,   1.0f};
    vivid::Param<float> color_g         {"color_g",         0.3f,    0.0f,   1.0f};
    vivid::Param<float> color_b         {"color_b",         0.1f,    0.0f,   1.0f};

    // ── Control ─────────────────────────────────────────────────────
    vivid::Param<int>   sim_resolution  {"sim_resolution",  2,       {"64", "128", "256", "512"}};
    vivid::Param<int>   reset           {"reset",           0,       {"Off", "Reset"}};

    // ── Modulation ──────────────────────────────────────────────
    vivid::Param<int>   viscosity_mod_enabled  {"viscosity_mod_enabled",  0, {"Off", "On"}};
    vivid::Param<float> viscosity_mod_amount   {"viscosity_mod_amount",   1.0f, 0.0f, 2.0f};
    vivid::Param<float> viscosity_mod_rate     {"viscosity_mod_rate",     1.0f, 0.01f, 20.0f};
    vivid::Param<int>   viscosity_mod_waveform {"viscosity_mod_waveform", 0, {"Sine", "Triangle", "Saw", "Square"}};
    vivid::Param<float> viscosity_mod_offset   {"viscosity_mod_offset",   0.0f, -1.0f, 1.0f};

    vivid::Param<int>   buoyancy_mod_enabled   {"buoyancy_mod_enabled",  0, {"Off", "On"}};
    vivid::Param<float> buoyancy_mod_amount    {"buoyancy_mod_amount",   1.0f, 0.0f, 2.0f};
    vivid::Param<float> buoyancy_mod_rate      {"buoyancy_mod_rate",     1.0f, 0.01f, 20.0f};
    vivid::Param<int>   buoyancy_mod_waveform  {"buoyancy_mod_waveform", 0, {"Sine", "Triangle", "Saw", "Square"}};
    vivid::Param<float> buoyancy_mod_offset    {"buoyancy_mod_offset",   0.0f, -1.0f, 1.0f};

    vivid::Param<int>   force_mod_enabled      {"force_mod_enabled",     0, {"Off", "On"}};
    vivid::Param<float> force_mod_amount       {"force_mod_amount",      1.0f, 0.0f, 2.0f};
    vivid::Param<float> force_mod_rate         {"force_mod_rate",        1.0f, 0.01f, 20.0f};
    vivid::Param<int>   force_mod_waveform     {"force_mod_waveform",    0, {"Sine", "Triangle", "Saw", "Square"}};
    vivid::Param<float> force_mod_offset       {"force_mod_offset",      0.0f, -1.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::layout_row(viscosity,       2, 0);
        vivid::layout_row(pressure_iters,  2, 1);
        vivid::layout_row(buoyancy,        2, 1);
        vivid::layout_row(dissipation,     2, 0);
        vivid::layout_row(force_strength,  2, 1);
        vivid::layout_row(emitter_x,       2, 0);
        vivid::layout_row(emitter_y,       2, 1);
        vivid::layout_row(emitter_radius,  2, 0);
        vivid::layout_row(color_r,         2, 1);
        vivid::layout_row(color_g,         2, 0);
        vivid::layout_row(color_b,         2, 1);
        vivid::layout_row(sim_resolution,  2, 0);
        vivid::layout_row(reset,           2, 1);

        out.push_back(&viscosity);
        out.push_back(&pressure_iters);
        out.push_back(&buoyancy);
        out.push_back(&dissipation);
        out.push_back(&force_strength);
        out.push_back(&emitter_x);
        out.push_back(&emitter_y);
        out.push_back(&emitter_radius);
        out.push_back(&color_r);
        out.push_back(&color_g);
        out.push_back(&color_b);
        out.push_back(&sim_resolution);
        out.push_back(&reset);
        out.push_back(&viscosity_mod_enabled);
        out.push_back(&viscosity_mod_amount);
        out.push_back(&viscosity_mod_rate);
        out.push_back(&viscosity_mod_waveform);
        out.push_back(&viscosity_mod_offset);
        out.push_back(&buoyancy_mod_enabled);
        out.push_back(&buoyancy_mod_amount);
        out.push_back(&buoyancy_mod_rate);
        out.push_back(&buoyancy_mod_waveform);
        out.push_back(&buoyancy_mod_offset);
        out.push_back(&force_mod_enabled);
        out.push_back(&force_mod_amount);
        out.push_back(&force_mod_rate);
        out.push_back(&force_mod_waveform);
        out.push_back(&force_mod_offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }



    // ── Per-frame GPU processing ────────────────────────────────────

    void process_gpu(const VividGpuContext* ctx) override {
        if (!advect_vel_pipeline_ && !lazy_init(ctx)) return;

        // ── Resolution change → recreate state textures ─────────
        int cur_sim_res = sim_resolution.int_value();
        if (cur_sim_res != cached_sim_res_) {
            destroy_state_textures();
            create_state_textures(ctx);
        }

        // ── Reset trigger ───────────────────────────────────────
        int rst = reset.int_value();
        if (rst != 0 && prev_reset_ == 0) {
            destroy_state_textures();
            create_state_textures(ctx);
        }
        prev_reset_ = rst;

        // ── Owned modulation ───────────────────────────────────
        VividFrameContext ctrl_ctx{};
        ctrl_ctx.time       = ctx->time;
        ctrl_ctx.delta_time = ctx->delta_time;
        ctrl_ctx.frame      = ctx->frame;

        float visc_mod = 0.0f, buoy_mod = 0.0f, force_mod = 0.0f;

        if (viscosity_mod_enabled.int_value()) {
            viscosity_lfo_.set_param("rate", viscosity_mod_rate.value);
            viscosity_lfo_.set_param("waveform", static_cast<float>(viscosity_mod_waveform.int_value()));
            viscosity_lfo_.set_param("offset", viscosity_mod_offset.value);
            viscosity_lfo_.process(&ctrl_ctx);
            visc_mod = viscosity_lfo_.output("value") * viscosity_mod_amount.value;
        }
        if (buoyancy_mod_enabled.int_value()) {
            buoyancy_lfo_.set_param("rate", buoyancy_mod_rate.value);
            buoyancy_lfo_.set_param("waveform", static_cast<float>(buoyancy_mod_waveform.int_value()));
            buoyancy_lfo_.set_param("offset", buoyancy_mod_offset.value);
            buoyancy_lfo_.process(&ctrl_ctx);
            buoy_mod = buoyancy_lfo_.output("value") * buoyancy_mod_amount.value;
        }
        if (force_mod_enabled.int_value()) {
            force_lfo_.set_param("rate", force_mod_rate.value);
            force_lfo_.set_param("waveform", static_cast<float>(force_mod_waveform.int_value()));
            force_lfo_.set_param("offset", force_mod_offset.value);
            force_lfo_.process(&ctrl_ctx);
            force_mod = force_lfo_.output("value") * force_mod_amount.value;
        }

        // ── Fill uniforms ───────────────────────────────────────
        float s = static_cast<float>(sim_res_pixels());
        float dt = static_cast<float>(std::min(ctx->delta_time, 1.0 / 30.0));

        FluidUniforms fu{};
        fu.sim_res[0]     = s;
        fu.sim_res[1]     = s;
        fu.dt             = dt;
        fu.viscosity      = std::max(0.0f, viscosity.value * (1.0f + visc_mod));
        fu.force_x        = 0.0f;
        fu.force_y        = -1.0f;  // negative Y = upward in Y-down UV coords
        fu.force_strength = std::max(0.0f, force_strength.value * (1.0f + force_mod));
        fu.emitter_x      = emitter_x.value;
        fu.emitter_y      = emitter_y.value;
        fu.emitter_radius  = emitter_radius.value;
        fu.dissipation     = dissipation.value;
        fu.buoyancy        = buoyancy.value * (1.0f + buoy_mod);
        fu.dye_r           = color_r.value;
        fu.dye_g           = color_g.value;
        fu.dye_b           = color_b.value;
        fu.padding         = 0.0f;
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &fu, sizeof(fu));

        // ── Pass 1: Advect velocity ─────────────────────────────
        {
            int rd = vel_ping_, wr = 1 - vel_ping_;
            auto bg = make_bg(ctx, velocity_view_[rd], velocity_view_[rd], "Fluid Advect Vel BG");
            vivid::gpu::run_pass(ctx->command_encoder, advect_vel_pipeline_,
                                 bg, velocity_view_[wr], "Fluid Advect Vel");
            vivid::gpu::release(bg);
            vel_ping_ = wr;
        }

        // ── Pass 2: Apply forces ────────────────────────────────
        {
            int rd = vel_ping_, wr = 1 - vel_ping_;
            auto bg = make_bg(ctx, velocity_view_[rd], dye_view_[dye_ping_], "Fluid Forces BG");
            vivid::gpu::run_pass(ctx->command_encoder, forces_pipeline_,
                                 bg, velocity_view_[wr], "Fluid Forces");
            vivid::gpu::release(bg);
            vel_ping_ = wr;
        }

        // ── Pass 3: Compute divergence ──────────────────────────
        {
            auto bg = make_bg(ctx, velocity_view_[vel_ping_], velocity_view_[vel_ping_], "Fluid Div BG");
            vivid::gpu::run_pass(ctx->command_encoder, divergence_pipeline_,
                                 bg, divergence_view_, "Fluid Divergence");
            vivid::gpu::release(bg);
        }

        // ── Pass 4: Pressure solve (N Jacobi iterations) ────────
        int n_iters = std::clamp(pressure_iters.int_value(), 4, 40);
        for (int i = 0; i < n_iters; ++i) {
            int rd = pres_ping_, wr = 1 - pres_ping_;
            auto bg = make_bg(ctx, pressure_view_[rd], divergence_view_, "Fluid Pressure BG");
            vivid::gpu::run_pass(ctx->command_encoder, pressure_pipeline_,
                                 bg, pressure_view_[wr], "Fluid Pressure");
            vivid::gpu::release(bg);
            pres_ping_ = wr;
        }

        // ── Pass 5: Subtract pressure gradient ──────────────────
        {
            int rd = vel_ping_, wr = 1 - vel_ping_;
            auto bg = make_bg(ctx, velocity_view_[rd], pressure_view_[pres_ping_], "Fluid Grad BG");
            vivid::gpu::run_pass(ctx->command_encoder, gradient_sub_pipeline_,
                                 bg, velocity_view_[wr], "Fluid Grad Sub");
            vivid::gpu::release(bg);
            vel_ping_ = wr;
        }

        // ── Pass 6: Advect dye ──────────────────────────────────
        {
            int rd = dye_ping_, wr = 1 - dye_ping_;
            auto bg = make_bg(ctx, velocity_view_[vel_ping_], dye_view_[rd], "Fluid Advect Dye BG");
            vivid::gpu::run_pass(ctx->command_encoder, advect_dye_pipeline_,
                                 bg, dye_view_[wr], "Fluid Advect Dye");
            vivid::gpu::release(bg);
            dye_ping_ = wr;
        }

        // ── Pass 7: Visualize ───────────────────────────────────
        {
            auto bg = make_bg(ctx, dye_view_[dye_ping_], dye_view_[dye_ping_], "Fluid Vis BG");
            vivid::gpu::run_pass(ctx->command_encoder, vis_pipeline_,
                                 bg, ctx->output_texture_view, "Fluid Vis");
            vivid::gpu::release(bg);
        }
    }

    ~Fluid() override {
        // Shaders
        vivid::gpu::release(advect_vel_shader_);
        vivid::gpu::release(forces_shader_);
        vivid::gpu::release(divergence_shader_);
        vivid::gpu::release(pressure_shader_);
        vivid::gpu::release(gradient_sub_shader_);
        vivid::gpu::release(advect_dye_shader_);
        vivid::gpu::release(vis_shader_);

        // Pipelines
        vivid::gpu::release(advect_vel_pipeline_);
        vivid::gpu::release(forces_pipeline_);
        vivid::gpu::release(divergence_pipeline_);
        vivid::gpu::release(pressure_pipeline_);
        vivid::gpu::release(gradient_sub_pipeline_);
        vivid::gpu::release(advect_dye_pipeline_);
        vivid::gpu::release(vis_pipeline_);

        // Layouts, buffers, sampler
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(sampler_);

        // State textures
        destroy_state_textures();
    }

private:
    // ── Owned LFO modulators ────────────────────────────────────────
    vivid::ChildOp<LFO> viscosity_lfo_;
    vivid::ChildOp<LFO> buoyancy_lfo_;
    vivid::ChildOp<LFO> force_lfo_;

    // ── Ping-pong state ─────────────────────────────────────────────
    int vel_ping_  = 0;
    int pres_ping_ = 0;
    int dye_ping_  = 0;
    int prev_reset_ = 0;

    // ── GPU handles — shaders ───────────────────────────────────────
    WGPUShaderModule advect_vel_shader_    = nullptr;
    WGPUShaderModule forces_shader_        = nullptr;
    WGPUShaderModule divergence_shader_    = nullptr;
    WGPUShaderModule pressure_shader_      = nullptr;
    WGPUShaderModule gradient_sub_shader_  = nullptr;
    WGPUShaderModule advect_dye_shader_    = nullptr;
    WGPUShaderModule vis_shader_           = nullptr;

    // ── GPU handles — pipelines ─────────────────────────────────────
    WGPURenderPipeline advect_vel_pipeline_    = nullptr;
    WGPURenderPipeline forces_pipeline_        = nullptr;
    WGPURenderPipeline divergence_pipeline_    = nullptr;
    WGPURenderPipeline pressure_pipeline_      = nullptr;
    WGPURenderPipeline gradient_sub_pipeline_  = nullptr;
    WGPURenderPipeline advect_dye_pipeline_    = nullptr;
    WGPURenderPipeline vis_pipeline_           = nullptr;

    // ── GPU handles — layouts, buffers, sampler ─────────────────────
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUSampler         sampler_     = nullptr;

    // ── GPU handles — state textures ────────────────────────────────
    WGPUTexture     velocity_tex_[2]   = { nullptr, nullptr };
    WGPUTextureView velocity_view_[2]  = { nullptr, nullptr };
    WGPUTexture     pressure_tex_[2]   = { nullptr, nullptr };
    WGPUTextureView pressure_view_[2]  = { nullptr, nullptr };
    WGPUTexture     dye_tex_[2]        = { nullptr, nullptr };
    WGPUTextureView dye_view_[2]       = { nullptr, nullptr };
    WGPUTexture     divergence_tex_    = nullptr;
    WGPUTextureView divergence_view_   = nullptr;

    // ── Resolution cache ────────────────────────────────────────────
    int cached_sim_res_ = 0;

    // ── Helpers ─────────────────────────────────────────────────────

    uint32_t sim_res_pixels() const {
        static const uint32_t sizes[] = { 64, 128, 256, 512 };
        int idx = sim_resolution.int_value();
        return sizes[std::clamp(idx, 0, 3)];
    }

    WGPUBindGroup make_bg(const VividGpuContext* ctx, WGPUTextureView a,
                           WGPUTextureView b, const char* label) {
        WGPUTextureView views[2] = { a, b };
        return vivid::gpu::create_standard_bind_group(
            ctx->device, bind_layout_, uniform_buf_, sizeof(FluidUniforms),
            sampler_, views, 2, label);
    }

    WGPUShaderModule compile_shader(WGPUDevice device, const char* frag,
                                     const char* label) {
        std::string src = std::string(vivid::gpu::WGSL_CONSTANTS)
                        + kShaderPreamble + frag;
        return vivid::gpu::create_shader(device, src.c_str(), label);
    }

    // ── One-time GPU initialization ─────────────────────────────────

    bool lazy_init(const VividGpuContext* gpu) {
        // Compile shaders
        advect_vel_shader_   = compile_shader(gpu->device, kAdvectVelFragment,   "Fluid Advect Vel Shader");
        forces_shader_       = compile_shader(gpu->device, kForcesFragment,      "Fluid Forces Shader");
        divergence_shader_   = compile_shader(gpu->device, kDivergenceFragment,  "Fluid Divergence Shader");
        pressure_shader_     = compile_shader(gpu->device, kPressureFragment,    "Fluid Pressure Shader");
        gradient_sub_shader_ = compile_shader(gpu->device, kGradientSubFragment, "Fluid Grad Sub Shader");
        advect_dye_shader_   = compile_shader(gpu->device, kAdvectDyeFragment,   "Fluid Advect Dye Shader");
        vis_shader_          = compile_shader(gpu->device, kVisFragment,         "Fluid Vis Shader");

        if (!advect_vel_shader_ || !forces_shader_ || !divergence_shader_ ||
            !pressure_shader_ || !gradient_sub_shader_ || !advect_dye_shader_ ||
            !vis_shader_) return false;

        // Uniform buffer, sampler
        uniform_buf_ = vivid::gpu::create_uniform_buffer(
            gpu->device, sizeof(FluidUniforms), "Fluid Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Fluid Sampler");

        // Bind layout: uniform(0) + sampler(1) + tex_a(2) + tex_b(3)
        bind_layout_ = vivid::gpu::create_standard_bind_layout(
            gpu->device, 2, "Fluid BGL", sizeof(FluidUniforms));

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Fluid Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // Sim pipelines (target RGBA16Float state textures)
        advect_vel_pipeline_   = vivid::gpu::create_pipeline(gpu->device, advect_vel_shader_,   pipe_layout_, WGPUTextureFormat_RGBA16Float, "Fluid Advect Vel Pipeline");
        forces_pipeline_       = vivid::gpu::create_pipeline(gpu->device, forces_shader_,       pipe_layout_, WGPUTextureFormat_RGBA16Float, "Fluid Forces Pipeline");
        divergence_pipeline_   = vivid::gpu::create_pipeline(gpu->device, divergence_shader_,   pipe_layout_, WGPUTextureFormat_RGBA16Float, "Fluid Divergence Pipeline");
        pressure_pipeline_     = vivid::gpu::create_pipeline(gpu->device, pressure_shader_,     pipe_layout_, WGPUTextureFormat_RGBA16Float, "Fluid Pressure Pipeline");
        gradient_sub_pipeline_ = vivid::gpu::create_pipeline(gpu->device, gradient_sub_shader_, pipe_layout_, WGPUTextureFormat_RGBA16Float, "Fluid Grad Sub Pipeline");
        advect_dye_pipeline_   = vivid::gpu::create_pipeline(gpu->device, advect_dye_shader_,   pipe_layout_, WGPUTextureFormat_RGBA16Float, "Fluid Advect Dye Pipeline");

        // Vis pipeline (targets output format)
        vis_pipeline_ = vivid::gpu::create_pipeline(
            gpu->device, vis_shader_, pipe_layout_, gpu->output_format, "Fluid Vis Pipeline");

        if (!advect_vel_pipeline_ || !forces_pipeline_ || !divergence_pipeline_ ||
            !pressure_pipeline_ || !gradient_sub_pipeline_ || !advect_dye_pipeline_ ||
            !vis_pipeline_) return false;

        // Create initial state textures
        create_state_textures(gpu);
        return true;
    }

    // ── State texture management ────────────────────────────────────

    void create_state_textures(const VividGpuContext* gpu) {
        uint32_t s = sim_res_pixels();

        for (int i = 0; i < 2; ++i) {
            const char* suffix = i == 0 ? "A" : "B";

            std::string vl = std::string("Fluid Vel ") + suffix;
            velocity_tex_[i]  = vivid::gpu::create_state_texture(gpu->device, s, s, vl.c_str());
            velocity_view_[i] = vivid::gpu::create_texture_view(velocity_tex_[i], WGPUTextureFormat_RGBA16Float, vl.c_str());

            std::string pl = std::string("Fluid Pres ") + suffix;
            pressure_tex_[i]  = vivid::gpu::create_state_texture(gpu->device, s, s, pl.c_str());
            pressure_view_[i] = vivid::gpu::create_texture_view(pressure_tex_[i], WGPUTextureFormat_RGBA16Float, pl.c_str());

            std::string dl = std::string("Fluid Dye ") + suffix;
            dye_tex_[i]  = vivid::gpu::create_state_texture(gpu->device, s, s, dl.c_str());
            dye_view_[i] = vivid::gpu::create_texture_view(dye_tex_[i], WGPUTextureFormat_RGBA16Float, dl.c_str());
        }

        divergence_tex_  = vivid::gpu::create_state_texture(gpu->device, s, s, "Fluid Div");
        divergence_view_ = vivid::gpu::create_texture_view(divergence_tex_, WGPUTextureFormat_RGBA16Float, "Fluid Div View");

        vel_ping_ = pres_ping_ = dye_ping_ = 0;
        cached_sim_res_ = sim_resolution.int_value();
    }

    void destroy_state_textures() {
        for (int i = 0; i < 2; ++i) {
            vivid::gpu::release(velocity_tex_[i]);   velocity_tex_[i]  = nullptr;
            vivid::gpu::release(velocity_view_[i]);  velocity_view_[i] = nullptr;
            vivid::gpu::release(pressure_tex_[i]);   pressure_tex_[i]  = nullptr;
            vivid::gpu::release(pressure_view_[i]);  pressure_view_[i] = nullptr;
            vivid::gpu::release(dye_tex_[i]);        dye_tex_[i]       = nullptr;
            vivid::gpu::release(dye_view_[i]);       dye_view_[i]      = nullptr;
        }
        vivid::gpu::release(divergence_tex_);   divergence_tex_  = nullptr;
        vivid::gpu::release(divergence_view_);  divergence_view_ = nullptr;
    }

};

VIVID_REGISTER(Fluid)
