// Reaction-Diffusion — Gray-Scott system with SHARED role binding modulation.
//
// Two RGBA16Float state textures ping-pong for N iterations per frame.
// R=A concentration, G=B concentration. 5-point Laplacian stencil.
// Three SHARED role bindings (feed_mod, kill_mod, diffusion_mod) modulate
// the simulation parameters globally. A final visualization pass maps
// concentrations to color.

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/bound_control_instance.h"
#include <cmath>
#include <cstring>
#include <memory>
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

// ── Shared role binding ─────────────────────────────────────────────────

struct SharedBinding {
    std::unique_ptr<vivid::BoundControlInstance> instance;
    bool initialized = false;
    VividCreateBindableFn  cached_create_fn  = nullptr;
    VividDestroyBindableFn cached_destroy_fn = nullptr;
};

// ── Operator ────────────────────────────────────────────────────────────

struct ReactionDiffusion : vivid::GpuOperatorBase {
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
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void collect_role_bindings(std::vector<VividRoleBindingDescriptor>& out) override {
        {
            VividRoleBindingDescriptor role{};
            role.role_id                            = "feed_mod";
            role.label                              = "Feed Mod";
            role.accepted_domain                    = VIVID_DOMAIN_CONTROL;
            role.runtime_scope                      = VIVID_ROLE_SHARED;
            role.allowed_operator_types             = nullptr;
            role.allowed_operator_type_count        = 0;
            role.default_operator_type              = "LFO";
            role.preferred_output_name              = "value";
            role.preferred_output_semantic_tags      = nullptr;
            role.preferred_output_semantic_tag_count = 0;
            out.push_back(role);
        }
        {
            VividRoleBindingDescriptor role{};
            role.role_id                            = "kill_mod";
            role.label                              = "Kill Mod";
            role.accepted_domain                    = VIVID_DOMAIN_CONTROL;
            role.runtime_scope                      = VIVID_ROLE_SHARED;
            role.allowed_operator_types             = nullptr;
            role.allowed_operator_type_count        = 0;
            role.default_operator_type              = "LFO";
            role.preferred_output_name              = "value";
            role.preferred_output_semantic_tags      = nullptr;
            role.preferred_output_semantic_tag_count = 0;
            out.push_back(role);
        }
        {
            VividRoleBindingDescriptor role{};
            role.role_id                            = "diffusion_mod";
            role.label                              = "Diffusion Mod";
            role.accepted_domain                    = VIVID_DOMAIN_CONTROL;
            role.runtime_scope                      = VIVID_ROLE_SHARED;
            role.allowed_operator_types             = nullptr;
            role.allowed_operator_type_count        = 0;
            role.default_operator_type              = "LFO";
            role.preferred_output_name              = "value";
            role.preferred_output_semantic_tags      = nullptr;
            role.preferred_output_semantic_tag_count = 0;
            out.push_back(role);
        }
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!sim_pipeline_ && !lazy_init(ctx)) return;

        // ── Resolution change → recreate state textures ──────────────
        if (ctx->output_width != cached_width_ || ctx->output_height != cached_height_) {
            recreate_state_textures(ctx);
            cached_width_  = ctx->output_width;
            cached_height_ = ctx->output_height;
        }

        // ── Init shared bindings ─────────────────────────────────────
        maybe_init_shared(feed_binding_,      ctx, "feed_mod");
        maybe_init_shared(kill_binding_,       ctx, "kill_mod");
        maybe_init_shared(diffusion_binding_, ctx, "diffusion_mod");

        // ── Process shared bindings ──────────────────────────────────
        VividProcessContext ctrl_ctx{};
        ctrl_ctx.time       = ctx->time;
        ctrl_ctx.delta_time = ctx->delta_time;
        ctrl_ctx.frame      = ctx->frame;

        float feed_mod = process_shared(feed_binding_, &ctrl_ctx);
        float kill_mod = process_shared(kill_binding_, &ctrl_ctx);
        float diff_mod = process_shared(diffusion_binding_, &ctrl_ctx);

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
    // Shared role bindings
    SharedBinding feed_binding_;
    SharedBinding kill_binding_;
    SharedBinding diffusion_binding_;

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

    // ── Shared binding management ────────────────────────────────────

    void maybe_init_shared(SharedBinding& sb, const VividGpuContext* ctx,
                           const char* role_id) {
        if (!ctx->role_binding_configs || ctx->role_binding_count == 0) {
            if (sb.initialized) {
                sb.instance.reset();
                sb.initialized = false;
            }
            return;
        }

        const VividRoleBindingRuntimeConfig* cfg = nullptr;
        for (uint32_t i = 0; i < ctx->role_binding_count; ++i) {
            if (std::strcmp(ctx->role_binding_configs[i].role_id, role_id) == 0) {
                cfg = &ctx->role_binding_configs[i];
                break;
            }
        }

        if (!cfg || !cfg->create_fn) {
            if (sb.initialized) {
                sb.instance.reset();
                sb.initialized = false;
            }
            return;
        }

        bool need_reinit = !sb.initialized
            || sb.cached_create_fn != cfg->create_fn;

        if (need_reinit) {
            sb.instance.reset();
            sb.cached_create_fn  = cfg->create_fn;
            sb.cached_destroy_fn = cfg->destroy_fn;

            auto* raw = static_cast<vivid::OperatorBase*>(cfg->create_fn());
            if (!raw) return;

            auto destroy = [dfn = sb.cached_destroy_fn](vivid::OperatorBase* p) {
                if (dfn) dfn(p); else delete p;
            };
            sb.instance = std::make_unique<vivid::BoundControlInstance>(raw, std::move(destroy));

            for (uint32_t pi = 0; pi < cfg->param_count; ++pi) {
                if (sb.instance->has_param(cfg->param_names[pi])) {
                    sb.instance->set_param(cfg->param_names[pi], cfg->param_values[pi]);
                }
            }
            sb.initialized = true;
        } else {
            // Sync params each frame
            if (sb.instance) {
                for (uint32_t pi = 0; pi < cfg->param_count; ++pi) {
                    if (sb.instance->has_param(cfg->param_names[pi])) {
                        sb.instance->set_param(cfg->param_names[pi], cfg->param_values[pi]);
                    }
                }
            }
        }
    }

    static float process_shared(SharedBinding& sb, const VividProcessContext* ctrl_ctx) {
        if (sb.instance) {
            sb.instance->process(ctrl_ctx);
            return sb.instance->output("value");
        }
        return 0.0f;
    }
};

VIVID_REGISTER(ReactionDiffusion)
