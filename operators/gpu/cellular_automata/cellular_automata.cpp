// Cellular Automata — Game of Life, HighLife, Seeds, and custom rules.
//
// Two RGBA16Float state textures at grid_size resolution, ping-pong.
// Moore neighborhood (8 neighbors), rule evaluation. Visualization pass
// with nearest-neighbor sampling to preserve cell edges. Step accumulator
// for fractional-frame timing. Two SHARED role bindings modulate
// birth and survival thresholds.

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
    birth_min: f32,
    birth_max: f32,
    survive_min: f32,
    survive_max: f32,
    randomize: f32,
    fill_density: f32,
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

// Simple hash for randomization (deterministic from UV + density)
fn pcg_hash(input: u32) -> u32 {
    var state = input * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;

    // Randomize pass
    if (u.randomize > 0.5) {
        let pixel = vec2u(uv * u.resolution);
        let seed = pixel.x + pixel.y * u32(u.resolution.x);
        let hash = pcg_hash(seed);
        let rand_val = f32(hash) / 4294967295.0;
        let alive = select(0.0, 1.0, rand_val < u.fill_density);
        return vec4f(alive, 0.0, 0.0, 1.0);
    }

    // Count Moore neighborhood (8 neighbors)
    let texel = vec2f(1.0) / u.resolution;
    var count = 0;
    for (var dy = -1; dy <= 1; dy++) {
        for (var dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) { continue; }
            let s = textureSample(stateTex, texSampler, uv + vec2f(f32(dx), f32(dy)) * texel);
            if (s.r > 0.5) { count++; }
        }
    }

    let alive = textureSample(stateTex, texSampler, uv).r > 0.5;
    let fc = f32(count);

    var new_alive = false;
    if (alive) {
        // Survive rule
        new_alive = fc >= u.survive_min && fc <= u.survive_max;
    } else {
        // Birth rule
        new_alive = fc >= u.birth_min && fc <= u.birth_max;
    }

    return vec4f(select(0.0, 1.0, new_alive), 0.0, 0.0, 1.0);
}
)";

// ── WGSL visualization shader ───────────────────────────────────────────

static const char* kVisFragment = R"(

struct Uniforms {
    resolution: vec2f,
    birth_min: f32,
    birth_max: f32,
    survive_min: f32,
    survive_max: f32,
    randomize: f32,
    fill_density: f32,
};

struct VisData {
    grid_resolution: vec2f,
    alive_r: f32,
    alive_g: f32,
    alive_b: f32,
    dead_r: f32,
    dead_g: f32,
    dead_b: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

// Vis uses the same uniform layout as sim for bind layout compatibility,
// but interprets the data differently via VisData alias.
// We pack vis data into the same 32-byte struct.
@group(0) @binding(0) var<uniform> v: VisData;
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
    let alive = c.r > 0.5;

    let alive_color = vec3f(v.alive_r, v.alive_g, v.alive_b);
    let dead_color = vec3f(v.dead_r, v.dead_g, v.dead_b);
    let color = select(dead_color, alive_color, alive);

    return vec4f(color, 1.0);
}
)";

// ── Uniform structs (must match WGSL layout) ────────────────────────────

struct SimUniforms {
    float resolution[2];   // vec2f
    float birth_min;       // f32
    float birth_max;       // f32
    float survive_min;     // f32
    float survive_max;     // f32
    float randomize;       // f32
    float fill_density;    // f32
};

struct VisUniforms {
    float grid_resolution[2]; // vec2f
    float alive_r, alive_g;   // f32, f32
    float alive_b, dead_r;    // f32, f32
    float dead_g, dead_b;     // f32, f32
};

// ── Shared role binding ─────────────────────────────────────────────────

struct SharedBinding {
    std::unique_ptr<vivid::BoundControlInstance> instance;
    bool initialized = false;
    VividCreateBindableFn  cached_create_fn  = nullptr;
    VividDestroyBindableFn cached_destroy_fn = nullptr;
};

// ── Operator ────────────────────────────────────────────────────────────

struct CellularAutomata : vivid::GpuOperatorBase {
    static constexpr const char* kName   = "Cellular Automata";
    static constexpr bool kTimeDependent = true;

    // Rules
    vivid::Param<int>   rule_mode    {"rule_mode",    0,     {"Life", "HighLife", "Seeds", "Custom"}};
    vivid::Param<int>   grid_size    {"grid_size",    256,   64,    1024};

    // Custom rules
    vivid::Param<int>   birth_min    {"birth_min",    3,     0,     8};
    vivid::Param<int>   birth_max    {"birth_max",    3,     0,     8};
    vivid::Param<int>   survive_min  {"survive_min",  2,     0,     8};
    vivid::Param<int>   survive_max  {"survive_max",  3,     0,     8};

    // Timing & density
    vivid::Param<float> speed        {"speed",        10.0f, 0.1f,  60.0f};
    vivid::Param<float> fill_density {"fill_density", 0.3f,  0.0f,  1.0f};

    // Colors
    vivid::Param<float> alive_r      {"alive_r",      1.0f,  0.0f,  1.0f};
    vivid::Param<float> alive_g      {"alive_g",      1.0f,  0.0f,  1.0f};
    vivid::Param<float> alive_b      {"alive_b",      1.0f,  0.0f,  1.0f};
    vivid::Param<float> dead_r       {"dead_r",       0.0f,  0.0f,  1.0f};
    vivid::Param<float> dead_g       {"dead_g",       0.0f,  0.0f,  1.0f};
    vivid::Param<float> dead_b       {"dead_b",       0.0f,  0.0f,  1.0f};

    // Trigger
    vivid::Param<int>   randomize    {"randomize",    0,     {"Off", "Randomize"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::layout_row(rule_mode,    2, 0);
        vivid::layout_row(grid_size,    2, 1);
        vivid::layout_row(birth_min,    4, 0);
        vivid::layout_row(birth_max,    4, 1);
        vivid::layout_row(survive_min,  4, 2);
        vivid::layout_row(survive_max,  4, 3);
        vivid::layout_row(speed,        2, 0);
        vivid::layout_row(fill_density, 2, 1);
        vivid::display_hint(alive_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(alive_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(alive_b, VIVID_DISPLAY_COLOR);
        vivid::layout_row(alive_r,      3, 0);
        vivid::layout_row(alive_g,      3, 1);
        vivid::layout_row(alive_b,      3, 2);
        vivid::display_hint(dead_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(dead_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(dead_b, VIVID_DISPLAY_COLOR);
        vivid::layout_row(dead_r,       3, 0);
        vivid::layout_row(dead_g,       3, 1);
        vivid::layout_row(dead_b,       3, 2);
        vivid::layout_row(randomize,    1, 0);
        out.push_back(&rule_mode);
        out.push_back(&grid_size);
        out.push_back(&birth_min);
        out.push_back(&birth_max);
        out.push_back(&survive_min);
        out.push_back(&survive_max);
        out.push_back(&speed);
        out.push_back(&fill_density);
        out.push_back(&alive_r);
        out.push_back(&alive_g);
        out.push_back(&alive_b);
        out.push_back(&dead_r);
        out.push_back(&dead_g);
        out.push_back(&dead_b);
        out.push_back(&randomize);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void collect_role_bindings(std::vector<VividRoleBindingDescriptor>& out) override {
        {
            VividRoleBindingDescriptor role{};
            role.role_id                            = "birth_threshold";
            role.label                              = "Birth Threshold";
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
            role.role_id                            = "survive_threshold";
            role.label                              = "Survive Threshold";
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

        // ── Check grid_size change → recreate state textures ─────────
        int gs = grid_size.int_value();
        if (gs < 64) gs = 64;
        if (gs > 1024) gs = 1024;
        if (gs != cached_grid_size_) {
            recreate_state_textures(ctx, static_cast<uint32_t>(gs));
            cached_grid_size_ = gs;
        }

        // ── Init shared bindings ─────────────────────────────────────
        maybe_init_shared(birth_binding_,   ctx, "birth_threshold");
        maybe_init_shared(survive_binding_, ctx, "survive_threshold");

        // ── Process shared bindings ──────────────────────────────────
        VividProcessContext ctrl_ctx{};
        ctrl_ctx.time       = ctx->time;
        ctrl_ctx.delta_time = ctx->delta_time;
        ctrl_ctx.frame      = ctx->frame;

        float birth_mod   = process_shared(birth_binding_, &ctrl_ctx);
        float survive_mod = process_shared(survive_binding_, &ctrl_ctx);

        // ── Handle randomize trigger ─────────────────────────────────
        int rnd = randomize.int_value();
        if (rnd != 0 && prev_randomize_ == 0) {
            needs_randomize_ = true;
        }
        prev_randomize_ = rnd;

        // ── Compute rule parameters ──────────────────────────────────
        float b_min, b_max, s_min, s_max;
        int mode = rule_mode.int_value();
        switch (mode) {
            case 0: // Life: B3/S23
                b_min = 3.0f; b_max = 3.0f; s_min = 2.0f; s_max = 3.0f;
                break;
            case 1: // HighLife: B36/S23
                b_min = 3.0f; b_max = 6.0f; s_min = 2.0f; s_max = 3.0f;
                break;
            case 2: // Seeds: B2/S(none)
                b_min = 2.0f; b_max = 2.0f; s_min = 9.0f; s_max = 9.0f; // impossible survive
                break;
            default: // Custom
                b_min = static_cast<float>(birth_min.int_value());
                b_max = static_cast<float>(birth_max.int_value());
                s_min = static_cast<float>(survive_min.int_value());
                s_max = static_cast<float>(survive_max.int_value());
                break;
        }

        // Apply modulation (offset by up to ±2)
        float b_off = std::round(birth_mod * 2.0f);
        float s_off = std::round(survive_mod * 2.0f);
        b_min = std::max(0.0f, std::min(8.0f, b_min + b_off));
        b_max = std::max(0.0f, std::min(8.0f, b_max + b_off));
        s_min = std::max(0.0f, std::min(8.0f, s_min + s_off));
        s_max = std::max(0.0f, std::min(8.0f, s_max + s_off));

        float gf = static_cast<float>(cached_grid_size_);

        // ── Randomize pass ───────────────────────────────────────────
        if (needs_randomize_) {
            SimUniforms su{};
            su.resolution[0] = gf;
            su.resolution[1] = gf;
            su.randomize     = 1.0f;
            su.fill_density  = fill_density.value;
            wgpuQueueWriteBuffer(ctx->queue, sim_uniform_buf_, 0, &su, sizeof(su));

            vivid::gpu::run_pass(ctx->command_encoder, sim_pipeline_, sim_bg_[0],
                                 state_view_[0], "CA Randomize");
            ping_ = 0;
            needs_randomize_ = false;
        }

        // ── Step accumulator ─────────────────────────────────────────
        float dt = static_cast<float>(ctx->delta_time);
        if (dt > 0.1f) dt = 0.1f;
        step_accum_ += speed.value * dt;
        int steps = static_cast<int>(step_accum_);
        if (steps > 4) steps = 4; // cap to prevent spiral on lag
        step_accum_ -= static_cast<float>(steps);

        // ── Simulation steps ─────────────────────────────────────────
        if (steps > 0) {
            SimUniforms su{};
            su.resolution[0] = gf;
            su.resolution[1] = gf;
            su.birth_min     = b_min;
            su.birth_max     = b_max;
            su.survive_min   = s_min;
            su.survive_max   = s_max;
            su.randomize     = 0.0f;
            su.fill_density  = 0.0f;
            wgpuQueueWriteBuffer(ctx->queue, sim_uniform_buf_, 0, &su, sizeof(su));

            for (int i = 0; i < steps; ++i) {
                int read  = ping_;
                int write = 1 - ping_;
                vivid::gpu::run_pass(ctx->command_encoder, sim_pipeline_, sim_bg_[read],
                                     state_view_[write], "CA Sim Step");
                ping_ = write;
            }
        }

        // ── Visualization pass ───────────────────────────────────────
        VisUniforms vu{};
        vu.grid_resolution[0] = gf;
        vu.grid_resolution[1] = gf;
        vu.alive_r = alive_r.value;
        vu.alive_g = alive_g.value;
        vu.alive_b = alive_b.value;
        vu.dead_r  = dead_r.value;
        vu.dead_g  = dead_g.value;
        vu.dead_b  = dead_b.value;
        wgpuQueueWriteBuffer(ctx->queue, vis_uniform_buf_, 0, &vu, sizeof(vu));

        vivid::gpu::run_pass(ctx->command_encoder, vis_pipeline_, vis_bg_[ping_],
                             ctx->output_texture_view, "CA Vis");
    }

    ~CellularAutomata() override {
        vivid::gpu::release(sim_pipeline_);
        vivid::gpu::release(vis_pipeline_);
        vivid::gpu::release(sim_shader_);
        vivid::gpu::release(vis_shader_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sim_uniform_buf_);
        vivid::gpu::release(vis_uniform_buf_);
        vivid::gpu::release(linear_sampler_);
        vivid::gpu::release(nearest_sampler_);
        for (int i = 0; i < 2; ++i) {
            vivid::gpu::release(state_tex_[i]);
            vivid::gpu::release(state_view_[i]);
            vivid::gpu::release(sim_bg_[i]);
            vivid::gpu::release(vis_bg_[i]);
        }
    }

private:
    // Shared role bindings
    SharedBinding birth_binding_;
    SharedBinding survive_binding_;

    // Ping-pong state
    int ping_ = 0;
    bool needs_randomize_ = true;
    int prev_randomize_ = 0;
    float step_accum_ = 0.0f;
    int cached_grid_size_ = 0;

    // GPU handles — pipelines
    WGPURenderPipeline  sim_pipeline_  = nullptr;
    WGPURenderPipeline  vis_pipeline_  = nullptr;
    WGPUShaderModule    sim_shader_    = nullptr;
    WGPUShaderModule    vis_shader_    = nullptr;
    WGPUBindGroupLayout bind_layout_   = nullptr;
    WGPUPipelineLayout  pipe_layout_   = nullptr;
    WGPUBuffer          sim_uniform_buf_ = nullptr;
    WGPUBuffer          vis_uniform_buf_ = nullptr;
    WGPUSampler         linear_sampler_  = nullptr;
    WGPUSampler         nearest_sampler_ = nullptr;

    // GPU handles — state textures
    WGPUTexture     state_tex_[2]  = { nullptr, nullptr };
    WGPUTextureView state_view_[2] = { nullptr, nullptr };
    WGPUBindGroup   sim_bg_[2]     = { nullptr, nullptr };
    WGPUBindGroup   vis_bg_[2]     = { nullptr, nullptr };

    // ── One-time GPU initialization ──────────────────────────────────

    bool lazy_init(const VividGpuContext* gpu) {
        std::string sim_src = std::string(vivid::gpu::WGSL_CONSTANTS) + kSimFragment;
        sim_shader_ = vivid::gpu::create_shader(gpu->device, sim_src.c_str(), "CA Sim Shader");
        if (!sim_shader_) return false;

        std::string vis_src = std::string(vivid::gpu::WGSL_CONSTANTS) + kVisFragment;
        vis_shader_ = vivid::gpu::create_shader(gpu->device, vis_src.c_str(), "CA Vis Shader");
        if (!vis_shader_) return false;

        sim_uniform_buf_ = vivid::gpu::create_uniform_buffer(
            gpu->device, sizeof(SimUniforms), "CA Sim Uniforms");
        vis_uniform_buf_ = vivid::gpu::create_uniform_buffer(
            gpu->device, sizeof(SimUniforms), "CA Vis Uniforms");

        linear_sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "CA Linear Sampler");

        // Nearest-neighbor sampler for visualization (crisp cell edges)
        {
            WGPUSamplerDescriptor desc{};
            desc.label = vivid_sv("CA Nearest Sampler");
            desc.addressModeU = WGPUAddressMode_ClampToEdge;
            desc.addressModeV = WGPUAddressMode_ClampToEdge;
            desc.addressModeW = WGPUAddressMode_ClampToEdge;
            desc.magFilter = WGPUFilterMode_Nearest;
            desc.minFilter = WGPUFilterMode_Nearest;
            desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
            desc.maxAnisotropy = 1;
            nearest_sampler_ = wgpuDeviceCreateSampler(gpu->device, &desc);
        }

        // Shared bind layout: uniform(0) + sampler(1) + texture(2)
        bind_layout_ = vivid::gpu::create_standard_bind_layout(
            gpu->device, 1, "CA BGL", sizeof(SimUniforms));

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("CA Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // Sim pipeline targets RGBA16Float state textures
        sim_pipeline_ = vivid::gpu::create_pipeline(
            gpu->device, sim_shader_, pipe_layout_,
            WGPUTextureFormat_RGBA16Float, "CA Sim Pipeline");
        if (!sim_pipeline_) return false;

        // Vis pipeline targets output format
        vis_pipeline_ = vivid::gpu::create_pipeline(
            gpu->device, vis_shader_, pipe_layout_,
            gpu->output_format, "CA Vis Pipeline");
        if (!vis_pipeline_) return false;

        int gs = grid_size.int_value();
        if (gs < 64) gs = 64;
        if (gs > 1024) gs = 1024;
        recreate_state_textures(gpu, static_cast<uint32_t>(gs));
        cached_grid_size_ = gs;

        return true;
    }

    // ── State texture management ─────────────────────────────────────

    void recreate_state_textures(const VividGpuContext* gpu, uint32_t gs) {
        for (int i = 0; i < 2; ++i) {
            vivid::gpu::release(state_tex_[i]);
            vivid::gpu::release(state_view_[i]);

            state_tex_[i] = vivid::gpu::create_state_texture(
                gpu->device, gs, gs,
                i == 0 ? "CA State A" : "CA State B");
            state_view_[i] = vivid::gpu::create_texture_view(
                state_tex_[i], WGPUTextureFormat_RGBA16Float,
                i == 0 ? "CA State A View" : "CA State B View");
        }

        rebuild_bind_groups(gpu);
        ping_ = 0;
        needs_randomize_ = true;
        step_accum_ = 0.0f;
    }

    void rebuild_bind_groups(const VividGpuContext* gpu) {
        for (int i = 0; i < 2; ++i) {
            vivid::gpu::release(sim_bg_[i]);
            vivid::gpu::release(vis_bg_[i]);

            // Sim bind groups use linear sampler
            sim_bg_[i] = vivid::gpu::create_standard_bind_group(
                gpu->device, bind_layout_, sim_uniform_buf_, sizeof(SimUniforms),
                linear_sampler_, &state_view_[i], 1,
                i == 0 ? "CA Sim BG 0" : "CA Sim BG 1");

            // Vis bind groups use nearest sampler (crisp cell edges)
            vis_bg_[i] = vivid::gpu::create_standard_bind_group(
                gpu->device, bind_layout_, vis_uniform_buf_, sizeof(SimUniforms),
                nearest_sampler_, &state_view_[i], 1,
                i == 0 ? "CA Vis BG 0" : "CA Vis BG 1");
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

VIVID_REGISTER(CellularAutomata)
