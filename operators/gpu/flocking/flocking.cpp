// Flocking / Boids — GPU operator with classical Reynolds flocking rules.
//
// CPU simulation (N<=64, O(N²)) with separation, alignment, and cohesion.
// Three PER_VOICE role bindings (speed_mod, separation_mod, alignment_mod)
// allow independent modulation per boid. GPU renders oriented triangle SDFs
// with optional directional trails.

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

static constexpr int kMaxBoids = 64;

// Simple LCG hash for deterministic pseudo-random initialization
static float hash_float(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / 16777216.0f; // [0, 1)
}

// ── WGSL fragment shader ────────────────────────────────────────────────

static const char* kFlockingFragment = R"(

struct Uniforms {
    resolution: vec2f,
    time: f32,
    active_count: f32,
    trail_length: f32,
    softness: f32,
    _pad: vec2f,
    boids_geo:   array<vec4f, 64>,  // xy=position, z=size, w=heading
    boids_color: array<vec4f, 64>,  // rgb=color, a=alpha
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

// Oriented isoceles triangle SDF — tip points in heading direction.
fn sdBoidTriangle(p_in: vec2f, size: f32, heading: f32) -> f32 {
    // Rotate so heading faces +x
    let c = cos(-heading);
    let s = sin(-heading);
    let p = vec2f(c * p_in.x + s * p_in.y, -s * p_in.x + c * p_in.y);

    // Isoceles triangle: tip at (size, 0), base corners at (-size*0.6, ±size*0.5)
    let tip = size;
    let base_x = -size * 0.6;
    let base_y = size * 0.5;

    // Fold by symmetry (abs y)
    let q = vec2f(p.x, abs(p.y));

    // Edge from tip to base corner
    let e = vec2f(base_x - tip, base_y);
    let w = q - vec2f(tip, 0.0);
    let t = clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
    let closest = vec2f(tip, 0.0) + t * e;
    let d_edge = length(q - closest);

    // Base edge (vertical at base_x)
    let d_base = length(q - vec2f(base_x, clamp(q.y, 0.0, base_y)));

    var d = min(d_edge, d_base);

    // Sign: inside if left of edge and within base
    let cross_val = e.x * w.y - e.y * w.x;
    if (cross_val < 0.0 && q.x > base_x) {
        d = -d;
    }
    return d;
}

// Elongated ellipse SDF for directional trail behind boid.
fn sdTrail(p_in: vec2f, size: f32, heading: f32, trail_len: f32) -> f32 {
    let c = cos(-heading);
    let s = sin(-heading);
    let p = vec2f(c * p_in.x + s * p_in.y, -s * p_in.x + c * p_in.y);

    // Trail extends behind boid (negative x direction)
    let ex = size * (0.5 + trail_len);
    let ey = size * 0.3;
    let offset_p = vec2f(p.x + ex * 0.5, p.y);

    // Approximate ellipse SDF
    let q = offset_p / vec2f(ex, ey);
    return (length(q) - 1.0) * min(ex, ey);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let aspect = u.resolution.x / u.resolution.y;
    let uv = vec2f(input.uv.x * aspect, input.uv.y);
    let n = i32(u.active_count);

    var accum = vec4f(0.0);

    for (var i = 0; i < n; i++) {
        let geo = u.boids_geo[i];
        let col = u.boids_color[i];

        let ppos = vec2f(geo.x * aspect, geo.y);
        let delta = uv - ppos;
        let size = geo.z;
        let heading = geo.w;

        // Body: oriented triangle
        let d_body = sdBoidTriangle(delta, size, heading);
        let alpha_body = 1.0 - smoothstep(-u.softness, u.softness, d_body);

        // Trail: elongated ellipse behind boid
        var alpha_trail = 0.0;
        if (u.trail_length > 0.01) {
            let d_trail = sdTrail(delta, size, heading, u.trail_length);
            alpha_trail = (1.0 - smoothstep(-u.softness * 2.0, u.softness * 2.0, d_trail)) * 0.35;
        }

        let alpha = max(alpha_body, alpha_trail) * col.a;
        accum += vec4f(col.rgb * alpha, alpha);
    }

    return vec4f(min(accum.rgb, vec3f(1.0)), min(accum.a, 1.0));
}
)";

// ── Uniform struct (must match WGSL layout exactly) ─────────────────────

struct FlockingUniforms {
    float resolution[2];
    float time;
    float active_count;
    float trail_length;
    float softness;
    float _pad[2];
    float boids_geo[kMaxBoids * 4];    // array<vec4f, 64>
    float boids_color[kMaxBoids * 4];  // array<vec4f, 64>
};

// ── Boid state ──────────────────────────────────────────────────────────

struct Boid {
    float x  = 0.5f, y  = 0.5f;   // position in [0,1] UV space
    float vx = 0.0f, vy = 0.0f;   // velocity
    float heading = 0.0f;          // derived from velocity direction
};

// ── Role binding pool ───────────────────────────────────────────────────

struct RolePool {
    std::vector<std::unique_ptr<vivid::BoundControlInstance>> pool;
    bool initialized = false;
    VividCreateBindableFn  cached_create_fn  = nullptr;
    VividDestroyBindableFn cached_destroy_fn = nullptr;
};

// ── Operator ────────────────────────────────────────────────────────────

struct Flocking : vivid::GpuOperatorBase {
    static constexpr const char* kName   = "Flocking";
    static constexpr bool kTimeDependent = true;

    // Count & boundary
    vivid::Param<int>   count         {"count",             16,    1,     kMaxBoids};
    vivid::Param<int>   boundary_mode {"boundary_mode",     0,     {"Wrap", "Bounce"}};

    // Flocking weights
    vivid::Param<float> separation_wt {"separation",        1.5f,  0.0f,  5.0f};
    vivid::Param<float> alignment_wt  {"alignment",         1.0f,  0.0f,  5.0f};
    vivid::Param<float> cohesion_wt   {"cohesion",          1.0f,  0.0f,  5.0f};

    // Radii & speed
    vivid::Param<float> sep_radius    {"separation_radius", 0.1f,  0.01f, 0.5f};
    vivid::Param<float> max_speed     {"max_speed",         0.3f,  0.01f, 2.0f};

    // Appearance
    vivid::Param<float> boid_size     {"size",              0.02f, 0.005f, 0.1f};
    vivid::Param<float> trail_length  {"trail_length",      0.0f,  0.0f,  3.0f};
    vivid::Param<float> softness      {"softness",          0.003f, 0.0f, 0.02f};

    // Color
    vivid::Param<float> color_r       {"color_r",           0.4f,  0.0f,  1.0f};
    vivid::Param<float> color_g       {"color_g",           0.8f,  0.0f,  1.0f};
    vivid::Param<float> color_b       {"color_b",           1.0f,  0.0f,  1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::layout_row(count,         2, 0);
        vivid::layout_row(boundary_mode, 2, 1);
        // separation_wt, alignment_wt, cohesion_wt: full-width sliders
        vivid::layout_row(sep_radius,    2, 0);
        vivid::layout_row(max_speed,     2, 1);
        // boid_size, trail_length, softness: full-width sliders
        vivid::display_hint(color_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(color_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(color_b, VIVID_DISPLAY_COLOR);
        // color: compound widget handles COLOR triplet automatically
        out.push_back(&count);
        out.push_back(&boundary_mode);
        out.push_back(&separation_wt);
        out.push_back(&alignment_wt);
        out.push_back(&cohesion_wt);
        out.push_back(&sep_radius);
        out.push_back(&max_speed);
        out.push_back(&boid_size);
        out.push_back(&trail_length);
        out.push_back(&softness);
        out.push_back(&color_r);
        out.push_back(&color_g);
        out.push_back(&color_b);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void collect_role_bindings(std::vector<VividRoleBindingDescriptor>& out) override {
        {
            VividRoleBindingDescriptor role{};
            role.role_id                            = "speed_mod";
            role.label                              = "Speed Mod";
            role.accepted_domain                    = VIVID_DOMAIN_CONTROL;
            role.runtime_scope                      = VIVID_ROLE_PER_VOICE;
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
            role.role_id                            = "separation_mod";
            role.label                              = "Separation Mod";
            role.accepted_domain                    = VIVID_DOMAIN_CONTROL;
            role.runtime_scope                      = VIVID_ROLE_PER_VOICE;
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
            role.role_id                            = "alignment_mod";
            role.label                              = "Alignment Mod";
            role.accepted_domain                    = VIVID_DOMAIN_CONTROL;
            role.runtime_scope                      = VIVID_ROLE_PER_VOICE;
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
        if (!pipeline_ && !lazy_init(ctx)) return;

        int n = count.int_value();
        if (n < 1) n = 1;
        if (n > kMaxBoids) n = kMaxBoids;

        // ── Initialize role binding pools ────────────────────────────
        maybe_init_pool(speed_pool_,      ctx, "speed_mod",      n);
        maybe_init_pool(separation_pool_, ctx, "separation_mod", n);
        maybe_init_pool(alignment_pool_,  ctx, "alignment_mod",  n);

        // ── Re-randomize on count change ─────────────────────────────
        if (n != prev_count_) {
            randomize_boids(n);
            prev_count_ = n;
        }

        float dt = static_cast<float>(ctx->delta_time);
        if (dt > 0.05f) dt = 0.05f; // clamp to prevent teleporting

        // ── Process role bindings ────────────────────────────────────
        VividProcessContext ctrl_ctx{};
        ctrl_ctx.time       = ctx->time;
        ctrl_ctx.delta_time = ctx->delta_time;
        ctrl_ctx.frame      = ctx->frame;

        float speed_mod_vals[kMaxBoids];
        float sep_mod_vals[kMaxBoids];
        float align_mod_vals[kMaxBoids];

        for (int i = 0; i < n; ++i) {
            speed_mod_vals[i] = 0.0f;
            if (i < static_cast<int>(speed_pool_.pool.size()) && speed_pool_.pool[i]) {
                speed_pool_.pool[i]->process(&ctrl_ctx);
                speed_mod_vals[i] = speed_pool_.pool[i]->output("value");
            }

            sep_mod_vals[i] = 0.0f;
            if (i < static_cast<int>(separation_pool_.pool.size()) && separation_pool_.pool[i]) {
                separation_pool_.pool[i]->process(&ctrl_ctx);
                sep_mod_vals[i] = separation_pool_.pool[i]->output("value");
            }

            align_mod_vals[i] = 0.0f;
            if (i < static_cast<int>(alignment_pool_.pool.size()) && alignment_pool_.pool[i]) {
                alignment_pool_.pool[i]->process(&ctrl_ctx);
                align_mod_vals[i] = alignment_pool_.pool[i]->output("value");
            }
        }

        // ── Flocking simulation ──────────────────────────────────────
        float sr     = sep_radius.value;
        float percep = sr * 2.0f; // perception radius for alignment/cohesion
        float sw     = separation_wt.value;
        float aw     = alignment_wt.value;
        float cw     = cohesion_wt.value;
        float ms     = max_speed.value;
        int   bmode  = boundary_mode.int_value();

        for (int i = 0; i < n; ++i) {
            auto& b = boids_[i];

            float sep_fx = 0.0f, sep_fy = 0.0f;
            float align_vx = 0.0f, align_vy = 0.0f;
            float coh_cx = 0.0f, coh_cy = 0.0f;
            int neighbor_count = 0;

            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                auto& other = boids_[j];

                float dx = other.x - b.x;
                float dy = other.y - b.y;

                // Wrap: shortest toroidal distance
                if (bmode == 0) {
                    dx -= std::round(dx);
                    dy -= std::round(dy);
                }

                float dist2 = dx * dx + dy * dy;
                float dist  = std::sqrt(dist2);

                // Separation
                if (dist < sr && dist > 0.0001f) {
                    float weight = 1.0f - dist / sr;
                    sep_fx -= (dx / dist) * weight;
                    sep_fy -= (dy / dist) * weight;
                }

                // Alignment & cohesion (within perception radius)
                if (dist < percep) {
                    align_vx += other.vx;
                    align_vy += other.vy;
                    coh_cx += b.x + dx; // use adjusted position for wrap
                    coh_cy += b.y + dy;
                    neighbor_count++;
                }
            }

            // Per-boid modulated weights
            float eff_sep_wt = sw * std::max(0.0f, 1.0f + sep_mod_vals[i]);
            float eff_align_wt = aw * std::max(0.0f, 1.0f + align_mod_vals[i]);
            float eff_max_speed = ms * std::max(0.0f, 1.0f + speed_mod_vals[i]);

            float ax = sep_fx * eff_sep_wt;
            float ay = sep_fy * eff_sep_wt;

            if (neighbor_count > 0) {
                // Alignment steering: desired = avg neighbor velocity direction * max_speed
                align_vx /= static_cast<float>(neighbor_count);
                align_vy /= static_cast<float>(neighbor_count);
                float align_len = std::sqrt(align_vx * align_vx + align_vy * align_vy);
                if (align_len > 0.0001f) {
                    float steer_x = (align_vx / align_len) * eff_max_speed - b.vx;
                    float steer_y = (align_vy / align_len) * eff_max_speed - b.vy;
                    ax += steer_x * eff_align_wt;
                    ay += steer_y * eff_align_wt;
                }

                // Cohesion steering: desired = toward avg neighbor position
                coh_cx /= static_cast<float>(neighbor_count);
                coh_cy /= static_cast<float>(neighbor_count);
                float coh_dx = coh_cx - b.x;
                float coh_dy = coh_cy - b.y;
                float coh_len = std::sqrt(coh_dx * coh_dx + coh_dy * coh_dy);
                if (coh_len > 0.0001f) {
                    float steer_x = (coh_dx / coh_len) * eff_max_speed - b.vx;
                    float steer_y = (coh_dy / coh_len) * eff_max_speed - b.vy;
                    ax += steer_x * cw;
                    ay += steer_y * cw;
                }
            }

            // Integrate velocity
            b.vx += ax * dt;
            b.vy += ay * dt;

            // Clamp to effective max speed
            float spd = std::sqrt(b.vx * b.vx + b.vy * b.vy);
            if (spd > eff_max_speed) {
                b.vx = (b.vx / spd) * eff_max_speed;
                b.vy = (b.vy / spd) * eff_max_speed;
            }

            // Integrate position
            b.x += b.vx * dt;
            b.y += b.vy * dt;

            // Boundary handling
            if (bmode == 0) {
                // Wrap
                b.x = b.x - std::floor(b.x);
                b.y = b.y - std::floor(b.y);
            } else {
                // Bounce
                if (b.x < 0.0f) { b.x = -b.x; b.vx = std::abs(b.vx); }
                if (b.x > 1.0f) { b.x = 2.0f - b.x; b.vx = -std::abs(b.vx); }
                if (b.y < 0.0f) { b.y = -b.y; b.vy = std::abs(b.vy); }
                if (b.y > 1.0f) { b.y = 2.0f - b.y; b.vy = -std::abs(b.vy); }
            }

            // Update heading from velocity
            if (spd > 0.001f) {
                b.heading = std::atan2(b.vy, b.vx);
            }
        }

        // ── Pack uniforms ────────────────────────────────────────────
        FlockingUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.time          = static_cast<float>(ctx->time);
        u.active_count  = static_cast<float>(n);
        u.trail_length  = trail_length.value;
        u.softness      = softness.value;

        float cr = color_r.value;
        float cg = color_g.value;
        float cb = color_b.value;
        float sz = boid_size.value;

        for (int i = 0; i < n; ++i) {
            auto& b = boids_[i];

            // Geometry: xy=position, z=size, w=heading
            u.boids_geo[i * 4 + 0] = b.x;
            u.boids_geo[i * 4 + 1] = b.y;
            u.boids_geo[i * 4 + 2] = sz;
            u.boids_geo[i * 4 + 3] = b.heading;

            // Color
            u.boids_color[i * 4 + 0] = cr;
            u.boids_color[i * 4 + 1] = cg;
            u.boids_color[i * 4 + 2] = cb;
            u.boids_color[i * 4 + 3] = 1.0f;
        }

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));
        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "Flocking Pass");
    }

    ~Flocking() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
    }

private:
    // Boid state
    std::vector<Boid> boids_;
    int prev_count_ = -1;

    // Role binding pools
    RolePool speed_pool_;
    RolePool separation_pool_;
    RolePool alignment_pool_;

    // GPU handles
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;

    bool lazy_init(const VividGpuContext* gpu) {
        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kFlockingFragment;
        shader_ = vivid::gpu::create_shader(gpu->device, frag.c_str(), "Flocking Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(
            gpu->device, sizeof(FlockingUniforms), "Flocking Uniforms");

        WGPUBindGroupLayoutEntry bgl_entry{};
        bgl_entry.binding = 0;
        bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entry.buffer.minBindingSize = sizeof(FlockingUniforms);

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Flocking BGL");
        bgl_desc.entryCount = 1;
        bgl_desc.entries = &bgl_entry;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Flocking Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer  = uniform_buf_;
        bg_entry.offset  = 0;
        bg_entry.size    = sizeof(FlockingUniforms);

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Flocking Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);

        pipeline_ = vivid::gpu::create_pipeline(
            gpu->device, shader_, pipe_layout_, gpu->output_format, "Flocking Pipeline");
        if (!pipeline_) return false;

        boids_.resize(kMaxBoids);
        return true;
    }

    // ── Boid initialization ──────────────────────────────────────────

    void randomize_boids(int n) {
        constexpr float margin = 0.1f;
        uint32_t seed = 42;

        for (int i = 0; i < n; ++i) {
            auto& b = boids_[i];
            b.x  = margin + hash_float(seed) * (1.0f - 2.0f * margin);
            b.y  = margin + hash_float(seed) * (1.0f - 2.0f * margin);
            b.vx = (hash_float(seed) - 0.5f) * max_speed.value;
            b.vy = (hash_float(seed) - 0.5f) * max_speed.value;
            float spd = std::sqrt(b.vx * b.vx + b.vy * b.vy);
            b.heading = (spd > 0.001f) ? std::atan2(b.vy, b.vx) : 0.0f;
        }
    }

    // ── Role binding pool management ────────────────────────────────

    void maybe_init_pool(RolePool& rp, const VividGpuContext* ctx,
                         const char* role_id, int n) {
        if (!ctx->role_binding_configs || ctx->role_binding_count == 0) {
            if (rp.initialized) {
                rp.pool.clear();
                rp.initialized = false;
            }
            return;
        }

        // Find matching role config
        const VividRoleBindingRuntimeConfig* cfg = nullptr;
        for (uint32_t i = 0; i < ctx->role_binding_count; ++i) {
            if (std::strcmp(ctx->role_binding_configs[i].role_id, role_id) == 0) {
                cfg = &ctx->role_binding_configs[i];
                break;
            }
        }

        if (!cfg || !cfg->create_fn) {
            if (rp.initialized) {
                rp.pool.clear();
                rp.initialized = false;
            }
            return;
        }

        bool need_reinit = !rp.initialized
            || rp.cached_create_fn != cfg->create_fn
            || static_cast<int>(rp.pool.size()) != n;

        if (need_reinit) {
            rp.pool.clear();
            rp.cached_create_fn  = cfg->create_fn;
            rp.cached_destroy_fn = cfg->destroy_fn;

            for (int i = 0; i < n; ++i) {
                auto* raw = static_cast<vivid::OperatorBase*>(cfg->create_fn());
                if (!raw) continue;

                auto destroy = [dfn = rp.cached_destroy_fn](vivid::OperatorBase* p) {
                    if (dfn) dfn(p); else delete p;
                };
                auto inst = std::make_unique<vivid::BoundControlInstance>(raw, std::move(destroy));

                for (uint32_t pi = 0; pi < cfg->param_count; ++pi) {
                    if (inst->has_param(cfg->param_names[pi])) {
                        inst->set_param(cfg->param_names[pi], cfg->param_values[pi]);
                    }
                }

                // Spread phase_offset across boids so each runs at
                // a different point in the LFO/envelope cycle.
                if (inst->has_param("phase_offset")) {
                    inst->set_param("phase_offset",
                        static_cast<float>(i) / static_cast<float>(n));
                }

                rp.pool.push_back(std::move(inst));
            }
            rp.initialized = true;
        } else {
            // Sync params each frame (user may tweak knobs live)
            for (int i = 0; i < static_cast<int>(rp.pool.size()); ++i) {
                auto& inst = rp.pool[i];
                if (!inst) continue;
                for (uint32_t pi = 0; pi < cfg->param_count; ++pi) {
                    if (inst->has_param(cfg->param_names[pi])) {
                        inst->set_param(cfg->param_names[pi], cfg->param_values[pi]);
                    }
                }
                // Re-apply per-instance phase offset (template sync may overwrite it)
                if (inst->has_param("phase_offset")) {
                    inst->set_param("phase_offset",
                        static_cast<float>(i) / static_cast<float>(rp.pool.size()));
                }
            }
        }
    }
};

VIVID_REGISTER(Flocking)
