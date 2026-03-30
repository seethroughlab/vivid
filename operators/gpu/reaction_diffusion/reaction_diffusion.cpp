// Reaction-Diffusion — Gray-Scott system with owned ChildOp<LFO> modulation.
//
// Two RGBA16Float state textures ping-pong for N iterations per frame.
// R=A concentration, G=B concentration. 5-point Laplacian stencil.
// Three internal LFO instances (feed_mod, kill_mod, diffusion_mod) modulate
// the simulation parameters globally. A final visualization pass maps
// concentrations to color.

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/child_op.h"
#include "control/lfo/lfo.h"
#include <cmath>
#include <string>

// ── WGSL simulation shader ─────────────────────────────────────────────

static const char* kSimFragment = R"(

struct Uniforms {
    resolution: vec2f,
    feed: f32,
    kill: f32,
    da: f32,
    db: f32,
    seed: f32,
    seed_radius: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var stateTex: texture_2d<f32>;

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

    // Seed pass: A=1 everywhere, B=1 inside circle at center
    if (u.seed > 0.5) {
        let center = vec2f(0.5, 0.5);
        let dist = length(uv - center);
        let b_val = select(0.0, 1.0, dist < u.seed_radius);
        return vec4f(1.0, b_val, 0.0, 1.0);
    }

    // Sample current state
    let texel = vec2f(1.0) / u.resolution;
    let c = textureSample(stateTex, texSampler, uv);
    let a = c.r;
    let b = c.g;

    // 5-point Laplacian stencil
    let left = textureSample(stateTex, texSampler, uv + vec2f(-texel.x, 0.0));
    let right = textureSample(stateTex, texSampler, uv + vec2f( texel.x, 0.0));
    let top = textureSample(stateTex, texSampler, uv + vec2f(0.0, -texel.y));
    let bot = textureSample(stateTex, texSampler, uv + vec2f(0.0,  texel.y));

    let lap_a = left.r + right.r + top.r + bot.r - 4.0 * a;
    let lap_b = left.g + right.g + top.g + bot.g - 4.0 * b;

    // Gray-Scott equations
    let ab2 = a * b * b;
    let new_a = a + u.da * lap_a - ab2 + u.feed * (1.0 - a);
    let new_b = b + u.db * lap_b + ab2 - (u.feed + u.kill) * b;

    return vec4f(clamp(new_a, 0.0, 1.0), clamp(new_b, 0.0, 1.0), 0.0, 1.0);
}
)";

// ── WGSL visualization shader ───────────────────────────────────────────

static const char* kVisFragment = R"(

struct Uniforms {
    resolution: vec2f,
    feed: f32,
    kill: f32,
    da: f32,
    db: f32,
    seed: f32,
    seed_radius: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var stateTex: texture_2d<f32>;

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
    let c = textureSample(stateTex, texSampler, input.uv);
    let a = c.r;
    let b = c.g;

    // color_mode is packed into seed field (reusing uniform for vis pass)
    let mode = i32(u.seed);

    switch (mode) {
        case 1: {
            // Blue-White
            return vec4f(1.0 - b, 1.0 - b * 0.5, 1.0, 1.0);
        }
        case 2: {
            // Fire: black → red → yellow → white
            let r = clamp(b * 3.0, 0.0, 1.0);
            let g = clamp(b * 3.0 - 1.0, 0.0, 1.0);
            let bl = clamp(b * 3.0 - 2.0, 0.0, 1.0);
            return vec4f(r, g, bl, 1.0);
        }
        case 3: {
            // Chemical: green/magenta
            return vec4f(a * 0.2, a * 0.8 + b * 0.2, b * 0.9, 1.0);
        }
        default: {
            // Grayscale (mode 0)
            return vec4f(vec3f(b), 1.0);
        }
    }
}
)";

// ── Uniform struct (matches WGSL layout) ────────────────────────────────

struct SimUniforms {
    float resolution[2];   // vec2f
    float feed;            // f32
    float kill;            // f32
    float da;              // f32
    float db;              // f32
    float seed;            // f32 (sim: 1.0=seed/0.0=run; vis: color_mode)
    float seed_radius;     // f32
};
/**
 * @brief Gray-Scott reaction-diffusion with LFO-modulated parameters.
 *
 * Simulates the Gray-Scott model on a 2D grid with configurable feed
 * and kill rates. LFO modulation on feed, kill, and diffusion creates
 * evolving organic patterns. Multiple color palettes available.
 *
 * @param feed_rate Rate of chemical A replenishment.
 * @param kill_rate Rate of chemical B removal.
 * @see CellularAutomata, Fluid
 */
struct ReactionDiffusion : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Reaction Diffusion";
    static constexpr bool kTimeDependent = true;

    // Reaction parameters
    vivid::Param<float> feed_rate    {"feed_rate",    0.055f, 0.0f,  0.1f};
    vivid::Param<float> kill_rate    {"kill_rate",    0.062f, 0.0f,  0.1f};
    vivid::Param<float> diffusion_a  {"diffusion_a",  1.0f,   0.0f,  2.0f};
    vivid::Param<float> diffusion_b  {"diffusion_b",  0.5f,   0.0f,  2.0f};

    // Simulation control
    vivid::Param<int>   iterations   {"iterations",   8,      1,     32};
    vivid::Param<float> seed_radius  {"seed_radius",  0.05f,  0.01f, 0.3f};

    // Visualization
    vivid::Param<int>   color_mode   {"color_mode",   0,      {"Grayscale", "Blue-White", "Fire", "Chemical"}};
    vivid::Param<int>   reset        {"reset",        0,      {"Off", "Reset"}};

    // Feed modulation LFO
    vivid::Param<int>   feed_mod_enabled  {"feed_mod_enabled",  0, {"Off", "On"}};
    vivid::Param<float> feed_mod_amount   {"feed_mod_amount",   1.0f, 0.0f, 2.0f};
    vivid::Param<float> feed_mod_rate     {"feed_mod_rate",     1.0f, 0.01f, 20.0f};
    vivid::Param<int>   feed_mod_waveform {"feed_mod_waveform", 0, {"Sine", "Triangle", "Saw", "Square"}};
    vivid::Param<float> feed_mod_offset   {"feed_mod_offset",   0.0f, -1.0f, 1.0f};

    // Kill modulation LFO
    vivid::Param<int>   kill_mod_enabled  {"kill_mod_enabled",  0, {"Off", "On"}};
    vivid::Param<float> kill_mod_amount   {"kill_mod_amount",   1.0f, 0.0f, 2.0f};
    vivid::Param<float> kill_mod_rate     {"kill_mod_rate",     1.0f, 0.01f, 20.0f};
    vivid::Param<int>   kill_mod_waveform {"kill_mod_waveform", 0, {"Sine", "Triangle", "Saw", "Square"}};
    vivid::Param<float> kill_mod_offset   {"kill_mod_offset",   0.0f, -1.0f, 1.0f};

    // Diffusion modulation LFO
    vivid::Param<int>   diffusion_mod_enabled  {"diffusion_mod_enabled",  0, {"Off", "On"}};
    vivid::Param<float> diffusion_mod_amount   {"diffusion_mod_amount",   1.0f, 0.0f, 2.0f};
    vivid::Param<float> diffusion_mod_rate     {"diffusion_mod_rate",     1.0f, 0.01f, 20.0f};
    vivid::Param<int>   diffusion_mod_waveform {"diffusion_mod_waveform", 0, {"Sine", "Triangle", "Saw", "Square"}};
    vivid::Param<float> diffusion_mod_offset   {"diffusion_mod_offset",   0.0f, -1.0f, 1.0f};

    ReactionDiffusion() {
        vivid::description(feed_rate, "Rate at which chemical A is replenished");
        vivid::description(kill_rate, "Rate at which chemical B is removed");
        vivid::description(diffusion_a, "Diffusion speed of chemical A");
        vivid::description(diffusion_b, "Diffusion speed of chemical B");
        vivid::description(iterations, "Simulation steps per frame, more means faster evolution");
        vivid::description(seed_radius, "Radius of the initial seed circle at center");
        vivid::description(color_mode, "Visualization palette: Grayscale, Blue-White, Fire, or Chemical");
        vivid::description(reset, "Trigger a reset to re-seed the simulation");
        vivid::description(feed_mod_enabled, "Enable LFO modulation of the feed rate");
        vivid::description(feed_mod_amount, "Strength of LFO modulation on feed rate");
        vivid::description(feed_mod_rate, "LFO frequency for feed modulation in Hz");
        vivid::description(feed_mod_waveform, "LFO waveform shape for feed modulation");
        vivid::description(feed_mod_offset, "DC offset added to the feed LFO output");
        vivid::description(kill_mod_enabled, "Enable LFO modulation of the kill rate");
        vivid::description(kill_mod_amount, "Strength of LFO modulation on kill rate");
        vivid::description(kill_mod_rate, "LFO frequency for kill modulation in Hz");
        vivid::description(kill_mod_waveform, "LFO waveform shape for kill modulation");
        vivid::description(kill_mod_offset, "DC offset added to the kill LFO output");
        vivid::description(diffusion_mod_enabled, "Enable LFO modulation of both diffusion rates");
        vivid::description(diffusion_mod_amount, "Strength of LFO modulation on diffusion");
        vivid::description(diffusion_mod_rate, "LFO frequency for diffusion modulation in Hz");
        vivid::description(diffusion_mod_waveform, "LFO waveform shape for diffusion modulation");
        vivid::description(diffusion_mod_offset, "DC offset added to the diffusion LFO output");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::layout_row(feed_rate,    2, 0);
        vivid::layout_row(kill_rate,    2, 1);
        vivid::layout_row(diffusion_a,  2, 0);
        vivid::layout_row(diffusion_b,  2, 1);
        vivid::layout_row(iterations,   2, 0);
        vivid::layout_row(seed_radius,  2, 1);
        vivid::layout_row(color_mode,   2, 0);
        vivid::layout_row(reset,        2, 1);
        out.push_back(&feed_rate);
        out.push_back(&kill_rate);
        out.push_back(&diffusion_a);
        out.push_back(&diffusion_b);
        out.push_back(&iterations);
        out.push_back(&seed_radius);
        out.push_back(&color_mode);
        out.push_back(&reset);

        // Feed mod LFO params
        out.push_back(&feed_mod_enabled);
        out.push_back(&feed_mod_amount);
        out.push_back(&feed_mod_rate);
        out.push_back(&feed_mod_waveform);
        out.push_back(&feed_mod_offset);

        // Kill mod LFO params
        out.push_back(&kill_mod_enabled);
        out.push_back(&kill_mod_amount);
        out.push_back(&kill_mod_rate);
        out.push_back(&kill_mod_waveform);
        out.push_back(&kill_mod_offset);

        // Diffusion mod LFO params
        out.push_back(&diffusion_mod_enabled);
        out.push_back(&diffusion_mod_amount);
        out.push_back(&diffusion_mod_rate);
        out.push_back(&diffusion_mod_waveform);
        out.push_back(&diffusion_mod_offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }



    void process_gpu(const VividGpuContext* ctx) override {
        if (!sim_pipeline_ && !lazy_init(ctx)) return;

        // ── Resolution change → recreate state textures ──────────────
        if (ctx->output_width != cached_width_ || ctx->output_height != cached_height_) {
            recreate_state_textures(ctx);
            cached_width_  = ctx->output_width;
            cached_height_ = ctx->output_height;
        }

        // ── Process owned LFO modulators ────────────────────────────
        // Waveform index remap: our choices {Sine, Triangle, Saw, Square}
        // map to LFO waveform indices       {0,    3,        1,   2}.
        static constexpr int kWaveformMap[] = {0, 3, 1, 2};

        VividFrameContext ctrl_ctx{};
        ctrl_ctx.time       = ctx->time;
        ctrl_ctx.delta_time = ctx->delta_time;
        ctrl_ctx.frame      = ctx->frame;

        float feed_mod = 0.0f;
        if (feed_mod_enabled.int_value()) {
            feed_lfo_.set_param("frequency", feed_mod_rate.value);
            feed_lfo_.set_param("amplitude", 1.0f);
            feed_lfo_.set_param("offset", feed_mod_offset.value);
            feed_lfo_.set_param("waveform", static_cast<float>(kWaveformMap[feed_mod_waveform.int_value()]));
            feed_lfo_.process(&ctrl_ctx);
            feed_mod = feed_lfo_.output("value") * feed_mod_amount.value;
        }

        float kill_mod = 0.0f;
        if (kill_mod_enabled.int_value()) {
            kill_lfo_.set_param("frequency", kill_mod_rate.value);
            kill_lfo_.set_param("amplitude", 1.0f);
            kill_lfo_.set_param("offset", kill_mod_offset.value);
            kill_lfo_.set_param("waveform", static_cast<float>(kWaveformMap[kill_mod_waveform.int_value()]));
            kill_lfo_.process(&ctrl_ctx);
            kill_mod = kill_lfo_.output("value") * kill_mod_amount.value;
        }

        float diff_mod = 0.0f;
        if (diffusion_mod_enabled.int_value()) {
            diffusion_lfo_.set_param("frequency", diffusion_mod_rate.value);
            diffusion_lfo_.set_param("amplitude", 1.0f);
            diffusion_lfo_.set_param("offset", diffusion_mod_offset.value);
            diffusion_lfo_.set_param("waveform", static_cast<float>(kWaveformMap[diffusion_mod_waveform.int_value()]));
            diffusion_lfo_.process(&ctrl_ctx);
            diff_mod = diffusion_lfo_.output("value") * diffusion_mod_amount.value;
        }

        // ── Handle reset trigger ─────────────────────────────────────
        int rst = reset.int_value();
        if (rst != 0 && prev_reset_ == 0) {
            needs_seed_ = true;
        }
        prev_reset_ = rst;

        float w = static_cast<float>(ctx->output_width);
        float h = static_cast<float>(ctx->output_height);

        // ── Seed pass ────────────────────────────────────────────────
        if (needs_seed_) {
            SimUniforms su{};
            su.resolution[0] = w;
            su.resolution[1] = h;
            su.seed          = 1.0f;
            su.seed_radius   = seed_radius.value;
            wgpuQueueWriteBuffer(ctx->queue, sim_uniform_buf_, 0, &su, sizeof(su));

            vivid::gpu::run_pass(ctx->command_encoder, sim_pipeline_, sim_bg_[0],
                                 state_view_[0], "RD Seed");
            ping_ = 0;
            needs_seed_ = false;
        }

        // ── Simulation iterations ────────────────────────────────────
        int n = iterations.int_value();
        if (n < 1) n = 1;
        if (n > 32) n = 32;

        SimUniforms su{};
        su.resolution[0] = w;
        su.resolution[1] = h;
        su.feed          = std::max(0.0f, feed_rate.value + feed_mod * 0.05f);
        su.kill          = std::max(0.0f, kill_rate.value + kill_mod * 0.05f);
        su.da            = std::max(0.0f, diffusion_a.value * std::max(0.0f, 1.0f + diff_mod));
        su.db            = std::max(0.0f, diffusion_b.value * std::max(0.0f, 1.0f + diff_mod));
        su.seed          = 0.0f;
        su.seed_radius   = 0.0f;
        wgpuQueueWriteBuffer(ctx->queue, sim_uniform_buf_, 0, &su, sizeof(su));

        for (int i = 0; i < n; ++i) {
            int read  = ping_;
            int write = 1 - ping_;
            vivid::gpu::run_pass(ctx->command_encoder, sim_pipeline_, sim_bg_[read],
                                 state_view_[write], "RD Sim Iter");
            ping_ = write;
        }

        // ── Visualization pass ───────────────────────────────────────
        SimUniforms vu{};
        vu.resolution[0] = w;
        vu.resolution[1] = h;
        vu.seed          = static_cast<float>(color_mode.int_value()); // reuse seed field for color_mode
        wgpuQueueWriteBuffer(ctx->queue, vis_uniform_buf_, 0, &vu, sizeof(vu));

        vivid::gpu::run_pass(ctx->command_encoder, vis_pipeline_, vis_bg_[ping_],
                             ctx->output_texture_view, "RD Vis");
    }

    ~ReactionDiffusion() override {
        vivid::gpu::release(sim_pipeline_);
        vivid::gpu::release(vis_pipeline_);
        vivid::gpu::release(sim_shader_);
        vivid::gpu::release(vis_shader_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sim_uniform_buf_);
        vivid::gpu::release(vis_uniform_buf_);
        vivid::gpu::release(sampler_);
        for (int i = 0; i < 2; ++i) {
            vivid::gpu::release(state_tex_[i]);
            vivid::gpu::release(state_view_[i]);
            vivid::gpu::release(sim_bg_[i]);
            vivid::gpu::release(vis_bg_[i]);
        }
    }

private:
    // Owned LFO modulators
    vivid::ChildOp<LFO> feed_lfo_;
    vivid::ChildOp<LFO> kill_lfo_;
    vivid::ChildOp<LFO> diffusion_lfo_;

    // Ping-pong state
    int ping_ = 0;
    bool needs_seed_ = true;
    int prev_reset_ = 0;

    // GPU handles — pipelines
    WGPURenderPipeline  sim_pipeline_  = nullptr;
    WGPURenderPipeline  vis_pipeline_  = nullptr;
    WGPUShaderModule    sim_shader_    = nullptr;
    WGPUShaderModule    vis_shader_    = nullptr;
    WGPUBindGroupLayout bind_layout_   = nullptr;
    WGPUPipelineLayout  pipe_layout_   = nullptr;
    WGPUBuffer          sim_uniform_buf_ = nullptr;
    WGPUBuffer          vis_uniform_buf_ = nullptr;
    WGPUSampler         sampler_       = nullptr;

    // GPU handles — state textures
    WGPUTexture     state_tex_[2]  = { nullptr, nullptr };
    WGPUTextureView state_view_[2] = { nullptr, nullptr };
    WGPUBindGroup   sim_bg_[2]     = { nullptr, nullptr };
    WGPUBindGroup   vis_bg_[2]     = { nullptr, nullptr };

    // Cache
    uint32_t cached_width_  = 0;
    uint32_t cached_height_ = 0;

    // ── One-time GPU initialization ──────────────────────────────────

    bool lazy_init(const VividGpuContext* gpu) {
        std::string sim_src = std::string(vivid::gpu::WGSL_CONSTANTS) + kSimFragment;
        sim_shader_ = vivid::gpu::create_shader(gpu->device, sim_src.c_str(), "RD Sim Shader");
        if (!sim_shader_) return false;

        std::string vis_src = std::string(vivid::gpu::WGSL_CONSTANTS) + kVisFragment;
        vis_shader_ = vivid::gpu::create_shader(gpu->device, vis_src.c_str(), "RD Vis Shader");
        if (!vis_shader_) return false;

        sim_uniform_buf_ = vivid::gpu::create_uniform_buffer(
            gpu->device, sizeof(SimUniforms), "RD Sim Uniforms");
        vis_uniform_buf_ = vivid::gpu::create_uniform_buffer(
            gpu->device, sizeof(SimUniforms), "RD Vis Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "RD Sampler");

        // Shared bind layout: uniform(0) + sampler(1) + texture(2)
        bind_layout_ = vivid::gpu::create_standard_bind_layout(
            gpu->device, 1, "RD BGL", sizeof(SimUniforms));

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("RD Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // Sim pipeline targets RGBA16Float state textures
        sim_pipeline_ = vivid::gpu::create_pipeline(
            gpu->device, sim_shader_, pipe_layout_,
            WGPUTextureFormat_RGBA16Float, "RD Sim Pipeline");
        if (!sim_pipeline_) return false;

        // Vis pipeline targets output format
        vis_pipeline_ = vivid::gpu::create_pipeline(
            gpu->device, vis_shader_, pipe_layout_,
            gpu->output_format, "RD Vis Pipeline");
        if (!vis_pipeline_) return false;

        recreate_state_textures(gpu);
        cached_width_  = gpu->output_width;
        cached_height_ = gpu->output_height;

        return true;
    }

    // ── State texture management ─────────────────────────────────────

    void recreate_state_textures(const VividGpuContext* gpu) {
        for (int i = 0; i < 2; ++i) {
            vivid::gpu::release(state_tex_[i]);
            vivid::gpu::release(state_view_[i]);

            state_tex_[i] = vivid::gpu::create_state_texture(
                gpu->device, gpu->output_width, gpu->output_height,
                i == 0 ? "RD State A" : "RD State B");
            state_view_[i] = vivid::gpu::create_texture_view(
                state_tex_[i], WGPUTextureFormat_RGBA16Float,
                i == 0 ? "RD State A View" : "RD State B View");
        }

        rebuild_bind_groups(gpu);
        ping_ = 0;
        needs_seed_ = true;
    }

    void rebuild_bind_groups(const VividGpuContext* gpu) {
        for (int i = 0; i < 2; ++i) {
            vivid::gpu::release(sim_bg_[i]);
            vivid::gpu::release(vis_bg_[i]);

            sim_bg_[i] = vivid::gpu::create_standard_bind_group(
                gpu->device, bind_layout_, sim_uniform_buf_, sizeof(SimUniforms),
                sampler_, &state_view_[i], 1,
                i == 0 ? "RD Sim BG 0" : "RD Sim BG 1");

            vis_bg_[i] = vivid::gpu::create_standard_bind_group(
                gpu->device, bind_layout_, vis_uniform_buf_, sizeof(SimUniforms),
                sampler_, &state_view_[i], 1,
                i == 0 ? "RD Vis BG 0" : "RD Vis BG 1");
        }
    }

};

VIVID_REGISTER(ReactionDiffusion)
