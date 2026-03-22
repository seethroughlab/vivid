// Particles — GPU particle system with per-particle role-bound envelope.
//
// Each particle auto-spawns at a configurable rate. If an "envelope" role
// binding is provided, each particle gets its own BoundControlInstance
// controlling opacity and size over its lifetime.

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/bound_control_instance.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static constexpr int kMaxParticles = 64;

// Simple LCG hash for deterministic pseudo-random particle positions
static float hash_float(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / 16777216.0f; // [0, 1)
}

// ── WGSL fragment shader ────────────────────────────────────────────────

static const char* kParticleFragment = R"(

struct Uniforms {
    resolution: vec2f,
    time: f32,
    active_count: f32,
    glow: f32,
    color_r: f32,
    color_g: f32,
    color_b: f32,
    particles: array<vec4f, 64>,  // xy=pos, z=radius, w=opacity
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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let aspect = u.resolution.x / u.resolution.y;
    let uv = vec2f(input.uv.x * aspect, input.uv.y);
    let n = i32(u.active_count);
    let base_color = vec3f(u.color_r, u.color_g, u.color_b);
    let glow_strength = u.glow;

    var accum = vec3f(0.0);

    for (var i = 0; i < n; i++) {
        let p = u.particles[i];
        let ppos = vec2f(p.x * aspect, p.y);
        let radius = p.z;
        let opacity = p.w;

        if (opacity < 0.001 || radius < 0.001) {
            continue;
        }

        let d = length(uv - ppos);

        // Soft circle core
        let core = smoothstep(radius, radius * 0.3, d) * opacity;

        // Additive glow halo
        let glow = glow_strength * opacity * (radius * radius) / (d * d + 0.001);
        let glow_clamped = min(glow, 2.0);

        accum += base_color * (core + glow_clamped * 0.15);
    }

    let final_color = min(accum, vec3f(1.0));
    let alpha = max(final_color.r, max(final_color.g, final_color.b));
    return vec4f(final_color, alpha);
}
)";

// ── Uniform struct (must match WGSL layout exactly) ─────────────────────

struct ParticleUniforms {
    float resolution[2];
    float time;
    float active_count;
    float glow;
    float color_r, color_g, color_b;
    float particles[kMaxParticles * 4]; // array<vec4f, 64> flattened
};

// ── Particle state ──────────────────────────────────────────────────────

struct Particle {
    float x = 0.5f, y = 0.5f;
    float dx = 0.0f, dy = 0.0f;
    float age = 0.0f;
    float lifetime = 2.0f;  // seconds before gate-off
    bool  active = false;
    bool  released = false;  // gate-off sent, waiting for envelope to finish
};

// ── Operator ────────────────────────────────────────────────────────────

struct Particles : vivid::GpuOperatorBase {
    static constexpr const char* kName   = "Particles";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   count    {"count",   16,   1,     kMaxParticles};
    vivid::Param<float> rate     {"rate",    3.0f, 0.1f,  20.0f};
    vivid::Param<float> size     {"size",    0.12f, 0.01f, 0.5f};
    vivid::Param<float> spread   {"spread",  0.35f, 0.0f,  0.5f};
    vivid::Param<float> speed    {"speed",   0.5f,  0.0f,  3.0f};
    vivid::Param<float> color_r  {"color_r", 0.4f,  0.0f,  1.0f};
    vivid::Param<float> color_g  {"color_g", 0.6f,  0.0f,  1.0f};
    vivid::Param<float> color_b  {"color_b", 1.0f,  0.0f,  1.0f};
    vivid::Param<float> glow     {"glow",    0.8f,  0.0f,  2.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::layout_row(count, 2, 0);
        vivid::layout_row(rate,  2, 1);
        vivid::layout_row(size,    3, 0);
        vivid::layout_row(spread,  3, 1);
        vivid::layout_row(speed,   3, 2);
        vivid::display_hint(color_r, VIVID_DISPLAY_KNOB);
        vivid::display_hint(color_g, VIVID_DISPLAY_KNOB);
        vivid::display_hint(color_b, VIVID_DISPLAY_KNOB);
        vivid::layout_row(color_r, 4, 0);
        vivid::layout_row(color_g, 4, 1);
        vivid::layout_row(color_b, 4, 2);
        vivid::layout_row(glow,    4, 3);
        out.push_back(&count);
        out.push_back(&rate);
        out.push_back(&size);
        out.push_back(&spread);
        out.push_back(&speed);
        out.push_back(&color_r);
        out.push_back(&color_g);
        out.push_back(&color_b);
        out.push_back(&glow);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void collect_role_bindings(std::vector<VividRoleBindingDescriptor>& out) override {
        VividRoleBindingDescriptor role{};
        role.role_id                          = "envelope";
        role.label                            = "Particle Envelope";
        role.accepted_domain                  = VIVID_DOMAIN_CONTROL;
        role.runtime_scope                    = VIVID_ROLE_PER_VOICE;
        role.allowed_operator_types           = nullptr;
        role.allowed_operator_type_count      = 0;
        role.default_operator_type            = "Envelope";
        role.preferred_output_name            = "value";
        role.preferred_output_semantic_tags      = nullptr;
        role.preferred_output_semantic_tag_count = 0;
        out.push_back(role);
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_ && !lazy_init(ctx)) return;

        float dt = static_cast<float>(ctx->delta_time);
        int n = count.int_value();
        if (n < 1) n = 1;
        if (n > kMaxParticles) n = kMaxParticles;

        // ── Initialize envelope pool from role binding (lazy) ───────────
        maybe_init_envelopes(ctx, n);

        // ── Spawn new particles ─────────────────────────────────────────
        spawn_timer_ += dt;
        float spawn_interval = 1.0f / rate.value;
        while (spawn_timer_ >= spawn_interval) {
            spawn_timer_ -= spawn_interval;
            spawn_particle(n);
        }

        // ── Update particles + process envelopes ────────────────────────
        // Build a synthetic VividProcessContext for envelope processing
        VividProcessContext env_ctx{};
        env_ctx.time       = ctx->time;
        env_ctx.delta_time = ctx->delta_time;
        env_ctx.frame      = ctx->frame;

        int active_count = 0;
        ParticleUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.time          = static_cast<float>(ctx->time);
        u.glow          = glow.value;
        u.color_r       = color_r.value;
        u.color_g       = color_g.value;
        u.color_b       = color_b.value;

        for (int i = 0; i < n; ++i) {
            auto& p = particles_[i];
            if (!p.active) continue;

            p.age += dt;
            p.x += p.dx * dt;
            p.y += p.dy * dt;

            // Gate lifecycle
            if (!p.released && p.age >= p.lifetime) {
                p.released = true;
                if (i < static_cast<int>(envelope_pool_.size()) && envelope_pool_[i]) {
                    envelope_pool_[i]->set_input("gate", 0.0f);
                }
            }

            // Get envelope value (or 1.0 if no binding)
            float env_val = 1.0f;
            if (i < static_cast<int>(envelope_pool_.size()) && envelope_pool_[i]) {
                envelope_pool_[i]->process(&env_ctx);
                env_val = envelope_pool_[i]->output("value");
            }

            // Deactivate if envelope finished
            if (p.released && env_val < 0.001f && p.age > p.lifetime + 0.05f) {
                p.active = false;
                continue;
            }

            // Pack into uniforms
            u.particles[active_count * 4 + 0] = p.x;
            u.particles[active_count * 4 + 1] = p.y;
            u.particles[active_count * 4 + 2] = size.value * env_val;
            u.particles[active_count * 4 + 3] = env_val;
            active_count++;
        }

        u.active_count = static_cast<float>(active_count);

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));
        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "Particles Pass");
    }

    ~Particles() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
    }

private:
    // Particle state
    std::vector<Particle> particles_;
    float spawn_timer_ = 0.0f;
    uint32_t seed_ = 42;

    // Envelope pool (one BoundControlInstance per particle slot)
    std::vector<std::unique_ptr<vivid::BoundControlInstance>> envelope_pool_;
    bool envelopes_initialized_ = false;
    VividCreateBindableFn  cached_create_fn_  = nullptr;
    VividDestroyBindableFn cached_destroy_fn_ = nullptr;

    // GPU handles
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;

    bool lazy_init(const VividGpuContext* gpu) {
        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kParticleFragment;
        shader_ = vivid::gpu::create_shader(gpu->device, frag.c_str(), "Particles Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(
            gpu->device, sizeof(ParticleUniforms), "Particles Uniforms");

        WGPUBindGroupLayoutEntry bgl_entry{};
        bgl_entry.binding = 0;
        bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entry.buffer.minBindingSize = sizeof(ParticleUniforms);

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Particles BGL");
        bgl_desc.entryCount = 1;
        bgl_desc.entries = &bgl_entry;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Particles Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer  = uniform_buf_;
        bg_entry.offset  = 0;
        bg_entry.size    = sizeof(ParticleUniforms);

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Particles Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);

        pipeline_ = vivid::gpu::create_pipeline(
            gpu->device, shader_, pipe_layout_, gpu->output_format, "Particles Pipeline");
        if (!pipeline_) return false;

        particles_.resize(kMaxParticles);
        return true;
    }

    void maybe_init_envelopes(const VividGpuContext* ctx, int n) {
        if (!ctx->role_binding_configs || ctx->role_binding_count == 0) {
            // No role bindings — clear pool if previously initialized
            if (envelopes_initialized_) {
                envelope_pool_.clear();
                envelopes_initialized_ = false;
            }
            return;
        }

        // Find the "envelope" role config
        const VividRoleBindingRuntimeConfig* env_cfg = nullptr;
        for (uint32_t i = 0; i < ctx->role_binding_count; ++i) {
            if (std::strcmp(ctx->role_binding_configs[i].role_id, "envelope") == 0) {
                env_cfg = &ctx->role_binding_configs[i];
                break;
            }
        }

        if (!env_cfg || !env_cfg->create_fn) {
            if (envelopes_initialized_) {
                envelope_pool_.clear();
                envelopes_initialized_ = false;
            }
            return;
        }

        // Re-initialize if factory changed or pool size changed
        bool need_reinit = !envelopes_initialized_
            || cached_create_fn_ != env_cfg->create_fn
            || static_cast<int>(envelope_pool_.size()) != n;

        if (need_reinit) {
            envelope_pool_.clear();
            cached_create_fn_  = env_cfg->create_fn;
            cached_destroy_fn_ = env_cfg->destroy_fn;

            for (int i = 0; i < n; ++i) {
                auto* raw = static_cast<vivid::OperatorBase*>(env_cfg->create_fn());
                if (!raw) continue;

                auto destroy = [dfn = cached_destroy_fn_](vivid::OperatorBase* p) {
                    if (dfn) dfn(p); else delete p;
                };
                auto inst = std::make_unique<vivid::BoundControlInstance>(raw, std::move(destroy));

                // Apply template params from the bound node
                for (uint32_t pi = 0; pi < env_cfg->param_count; ++pi) {
                    if (inst->has_param(env_cfg->param_names[pi])) {
                        inst->set_param(env_cfg->param_names[pi], env_cfg->param_values[pi]);
                    }
                }

                envelope_pool_.push_back(std::move(inst));
            }
            envelopes_initialized_ = true;
        } else {
            // Sync params each frame (user may tweak envelope knobs live)
            for (auto& inst : envelope_pool_) {
                if (!inst) continue;
                for (uint32_t pi = 0; pi < env_cfg->param_count; ++pi) {
                    if (inst->has_param(env_cfg->param_names[pi])) {
                        inst->set_param(env_cfg->param_names[pi], env_cfg->param_values[pi]);
                    }
                }
            }
        }
    }

    void spawn_particle(int max_slots) {
        // Find an inactive slot
        int slot = -1;
        for (int i = 0; i < max_slots; ++i) {
            if (!particles_[i].active) { slot = i; break; }
        }
        if (slot < 0) return; // all slots full

        auto& p = particles_[slot];
        p.active   = true;
        p.released = false;
        p.age      = 0.0f;
        p.lifetime = 1.0f + hash_float(seed_) * 1.5f; // 1–2.5s before gate-off

        // Random position around center
        float angle = hash_float(seed_) * 6.2831853f;
        float dist  = hash_float(seed_) * spread.value;
        p.x = 0.5f + dist * std::cos(angle);
        p.y = 0.5f + dist * std::sin(angle);

        // Random drift velocity
        float vangle = hash_float(seed_) * 6.2831853f;
        float vspeed = (0.02f + hash_float(seed_) * 0.05f) * speed.value;
        p.dx = vspeed * std::cos(vangle);
        p.dy = vspeed * std::sin(vangle);

        // Reset and gate the envelope
        if (slot < static_cast<int>(envelope_pool_.size()) && envelope_pool_[slot]) {
            envelope_pool_[slot]->reset();
            envelope_pool_[slot]->set_input("gate", 1.0f);
        }
    }
};

VIVID_REGISTER(Particles)
