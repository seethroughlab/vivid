// Instanced Shapes — GPU SDF geometry with per-instance role bindings.
//
// Renders N instances of a chosen SDF shape (circle, triangle, square, pentagon,
// hexagon, star) in configurable spatial layouts. Three PER_VOICE role bindings
// (scale, rotation, color_mod) allow independent modulation per instance.

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

static constexpr int kMaxInstances = 64;

// Simple LCG hash for deterministic pseudo-random layout positions
static float hash_float(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / 16777216.0f; // [0, 1)
}

// ── WGSL fragment shader ────────────────────────────────────────────────

static const char* kInstancedShapesFragment = R"(

struct Uniforms {
    resolution: vec2f,
    time: f32,
    active_count: f32,
    shape_sides: f32,
    star_factor: f32,
    softness: f32,
    _pad: f32,
    instances_geo:   array<vec4f, 64>,  // xy=position, z=size, w=rotation
    instances_color: array<vec4f, 64>,  // rgb=color, a=alpha
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

fn sdShape(p_in: vec2f, size: f32, rot: f32) -> f32 {
    // Apply per-instance rotation
    let c = cos(rot);
    let s = sin(rot);
    let p = vec2f(c * p_in.x + s * p_in.y, -s * p_in.x + c * p_in.y);

    let sides = u.shape_sides;
    let sf = u.star_factor;

    // Circle: simple distance field
    if (sides < 1.0) {
        return length(p) - size;
    }

    // Polygon / Star SDF via sector folding
    let an = PI / sides;
    let angle = atan2(p.y, p.x);

    if (sf < 0.001) {
        // Regular polygon
        let sector = round(angle / (2.0 * an));
        let folded = angle - sector * 2.0 * an;
        let q = length(p) * vec2f(cos(folded), abs(sin(folded)));
        return q.x - size * cos(an);
    } else {
        // Star — alternating outer/inner vertices
        let inner_r = size * (1.0 - sf);
        let sector = round(angle / (2.0 * an));
        let folded = abs(angle - sector * 2.0 * an);

        let lp = length(p);
        let q = vec2f(lp * cos(folded), lp * sin(folded));

        let v0 = vec2f(size, 0.0);
        let v1 = vec2f(inner_r * cos(an), inner_r * sin(an));
        let edge = v1 - v0;
        let normal = normalize(vec2f(edge.y, -edge.x));
        return dot(q - v0, normal);
    }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let aspect = u.resolution.x / u.resolution.y;
    let uv = vec2f(input.uv.x * aspect, input.uv.y);
    let n = i32(u.active_count);

    var accum = vec4f(0.0);

    for (var i = 0; i < n; i++) {
        let geo = u.instances_geo[i];
        let col = u.instances_color[i];

        let ppos = vec2f(geo.x * aspect, geo.y);
        let d = sdShape(uv - ppos, geo.z, geo.w);
        let alpha = (1.0 - smoothstep(-u.softness, u.softness, d)) * col.a;

        accum += vec4f(col.rgb * alpha, alpha);
    }

    return vec4f(min(accum.rgb, vec3f(1.0)), min(accum.a, 1.0));
}
)";

// ── Uniform struct (must match WGSL layout exactly) ─────────────────────

struct InstancedShapesUniforms {
    float resolution[2];
    float time;
    float active_count;
    float shape_sides;
    float star_factor;
    float softness;
    float _pad;
    float instances_geo[kMaxInstances * 4];    // array<vec4f, 64>
    float instances_color[kMaxInstances * 4];  // array<vec4f, 64>
};

// ── Instance state ──────────────────────────────────────────────────────

struct Instance {
    float x = 0.5f, y = 0.5f;   // base position in [0,1] UV space
    float phase = 0.0f;          // per-instance animation phase offset
};

// ── Role binding pool ───────────────────────────────────────────────────

struct RolePool {
    std::vector<std::unique_ptr<vivid::BoundControlInstance>> pool;
    bool initialized = false;
    VividCreateBindableFn  cached_create_fn  = nullptr;
    VividDestroyBindableFn cached_destroy_fn = nullptr;
};

// ── Operator ────────────────────────────────────────────────────────────

struct InstancedShapes : vivid::GpuOperatorBase {
    static constexpr const char* kName   = "Instanced Shapes";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   count     {"count",     16,    1,     kMaxInstances};
    vivid::Param<int>   shape     {"shape",     0,     {"Circle", "Triangle", "Square", "Pentagon", "Hexagon", "Star"}};
    vivid::Param<float> base_size {"base_size", 0.08f, 0.01f, 0.5f};
    vivid::Param<float> softness  {"softness",  0.005f, 0.0f, 0.05f};
    vivid::Param<float> color_r   {"color_r",   1.0f,  0.0f,  1.0f};
    vivid::Param<float> color_g   {"color_g",   1.0f,  0.0f,  1.0f};
    vivid::Param<float> color_b   {"color_b",   1.0f,  0.0f,  1.0f};
    vivid::Param<int>   layout    {"layout",    0,     {"Random", "Grid", "Circle", "Line"}};
    vivid::Param<int>   animate   {"animate",   0,     {"Off", "On"}};
    vivid::Param<float> speed     {"speed",     1.0f,  0.0f,  5.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::layout_row(count,     2, 0);
        vivid::layout_row(shape,     2, 1);
        // base_size, softness, layout: full-width sliders
        vivid::display_hint(color_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(color_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(color_b, VIVID_DISPLAY_COLOR);
        // color: compound widget handles COLOR triplet automatically
        vivid::layout_row(animate,   2, 0);
        vivid::layout_row(speed,     2, 1);
        out.push_back(&count);
        out.push_back(&shape);
        out.push_back(&base_size);
        out.push_back(&softness);
        out.push_back(&layout);
        out.push_back(&color_r);
        out.push_back(&color_g);
        out.push_back(&color_b);
        out.push_back(&animate);
        out.push_back(&speed);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void collect_role_bindings(std::vector<VividRoleBindingDescriptor>& out) override {
        {
            VividRoleBindingDescriptor role{};
            role.role_id                            = "scale";
            role.label                              = "Scale";
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
            role.role_id                            = "rotation";
            role.label                              = "Rotation";
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
            role.role_id                            = "color_mod";
            role.label                              = "Color Mod";
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
        if (n > kMaxInstances) n = kMaxInstances;

        // ── Initialize role binding pools ────────────────────────────
        maybe_init_pool(scale_pool_,     ctx, "scale",     n);
        maybe_init_pool(rotation_pool_,  ctx, "rotation",  n);
        maybe_init_pool(color_mod_pool_, ctx, "color_mod", n);

        // ── Compute layout positions ─────────────────────────────────
        int layout_mode = layout.int_value();
        bool animating = animate.int_value() != 0;
        float t = static_cast<float>(ctx->time);
        float spd = speed.value;

        if (n != prev_count_ || layout_mode != prev_layout_) {
            compute_base_layout(n, layout_mode);
            prev_count_  = n;
            prev_layout_ = layout_mode;
        }

        // ── Shape → SDF params ───────────────────────────────────────
        int shape_idx = shape.int_value();
        float shape_sides = 0.0f;
        float star_factor = 0.0f;
        switch (shape_idx) {
            case 0: shape_sides = 0.0f; break;  // Circle
            case 1: shape_sides = 3.0f; break;  // Triangle
            case 2: shape_sides = 4.0f; break;  // Square
            case 3: shape_sides = 5.0f; break;  // Pentagon
            case 4: shape_sides = 6.0f; break;  // Hexagon
            case 5: shape_sides = 5.0f; star_factor = 0.5f; break; // Star
        }

        // ── Pack uniforms ────────────────────────────────────────────
        InstancedShapesUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.time          = t;
        u.active_count  = static_cast<float>(n);
        u.shape_sides   = shape_sides;
        u.star_factor   = star_factor;
        u.softness      = softness.value;

        // Shared process context for all bound control instances
        VividProcessContext ctrl_ctx{};
        ctrl_ctx.time       = ctx->time;
        ctrl_ctx.delta_time = ctx->delta_time;
        ctrl_ctx.frame      = ctx->frame;

        float cr = color_r.value;
        float cg = color_g.value;
        float cb = color_b.value;
        float bs = base_size.value;

        for (int i = 0; i < n; ++i) {
            auto& inst = instances_[i];

            // Animated position
            float px = inst.x;
            float py = inst.y;
            if (animating) {
                switch (layout_mode) {
                    case 0: // Random: slow drift
                        px += std::cos(inst.phase + t * spd) * 0.02f;
                        py += std::sin(inst.phase + t * spd) * 0.02f;
                        break;
                    case 1: // Grid: oscillation
                        px += std::sin(inst.phase + t * spd) * 0.015f;
                        py += std::cos(inst.phase + t * spd * 0.7f) * 0.015f;
                        break;
                    case 2: { // Circle: rotate ring
                        float angle = inst.phase + t * spd * 0.5f;
                        float radius = 0.3f;
                        px = 0.5f + radius * std::cos(angle);
                        py = 0.5f + radius * std::sin(angle);
                        break;
                    }
                    case 3: // Line: sine wave
                        py += std::sin(inst.phase + t * spd) * 0.05f;
                        break;
                }
            }

            // Query role bindings
            float scale_val = 1.0f;
            if (i < static_cast<int>(scale_pool_.pool.size()) && scale_pool_.pool[i]) {
                scale_pool_.pool[i]->process(&ctrl_ctx);
                scale_val = scale_pool_.pool[i]->output("value");
            }

            float rot_val = 0.0f;
            if (i < static_cast<int>(rotation_pool_.pool.size()) && rotation_pool_.pool[i]) {
                rotation_pool_.pool[i]->process(&ctrl_ctx);
                rot_val = rotation_pool_.pool[i]->output("value");
            }

            float color_mod_val = 0.0f;
            if (i < static_cast<int>(color_mod_pool_.pool.size()) && color_mod_pool_.pool[i]) {
                color_mod_pool_.pool[i]->process(&ctrl_ctx);
                color_mod_val = color_mod_pool_.pool[i]->output("value");
            }

            // Pack geometry: xy=position, z=size, w=rotation
            // scale_val: bipolar [-1,1] → remap to [0,2] so 0 = default size
            float sz = bs * std::max(0.0f, 1.0f + scale_val);
            u.instances_geo[i * 4 + 0] = px;
            u.instances_geo[i * 4 + 1] = py;
            u.instances_geo[i * 4 + 2] = sz;
            u.instances_geo[i * 4 + 3] = rot_val * 6.2831853f;

            // Pack color: rgb=modulated color, a=alpha
            // color_mod_val: bipolar [-1,1] → brightness range [0.5, 1.5]
            float mod = std::max(0.0f, 1.0f + color_mod_val * 0.5f);
            u.instances_color[i * 4 + 0] = cr * mod;
            u.instances_color[i * 4 + 1] = cg * mod;
            u.instances_color[i * 4 + 2] = cb * mod;
            u.instances_color[i * 4 + 3] = 1.0f;
        }

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));
        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "Instanced Shapes Pass");
    }

    ~InstancedShapes() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
    }

private:
    // Instance state
    std::vector<Instance> instances_;
    int prev_count_  = -1;
    int prev_layout_ = -1;

    // Role binding pools
    RolePool scale_pool_;
    RolePool rotation_pool_;
    RolePool color_mod_pool_;

    // GPU handles
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    bool lazy_init(const VividGpuContext* gpu) {
        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kInstancedShapesFragment;
        shader_ = vivid::gpu::create_shader(gpu->device, frag.c_str(), "Instanced Shapes Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(
            gpu->device, sizeof(InstancedShapesUniforms), "Instanced Shapes Uniforms");

        WGPUBindGroupLayoutEntry bgl_entry{};
        bgl_entry.binding = 0;
        bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entry.buffer.minBindingSize = sizeof(InstancedShapesUniforms);

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Instanced Shapes BGL");
        bgl_desc.entryCount = 1;
        bgl_desc.entries = &bgl_entry;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Instanced Shapes Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer  = uniform_buf_;
        bg_entry.offset  = 0;
        bg_entry.size    = sizeof(InstancedShapesUniforms);

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Instanced Shapes Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);

        pipeline_ = vivid::gpu::create_pipeline(
            gpu->device, shader_, pipe_layout_, gpu->output_format, "Instanced Shapes Pipeline");
        if (!pipeline_) return false;

        instances_.resize(kMaxInstances);
        return true;
    }

    // ── Layout computation ──────────────────────────────────────────

    void compute_base_layout(int n, int layout_mode) {
        constexpr float TAU = 6.2831853f;
        constexpr float margin = 0.1f;

        for (int i = 0; i < n; ++i) {
            instances_[i].phase = TAU * static_cast<float>(i) / static_cast<float>(n);
        }

        switch (layout_mode) {
            case 0: { // Random — deterministic from fixed seed
                uint32_t seed = 42;
                for (int i = 0; i < n; ++i) {
                    instances_[i].x = margin + hash_float(seed) * (1.0f - 2.0f * margin);
                    instances_[i].y = margin + hash_float(seed) * (1.0f - 2.0f * margin);
                }
                break;
            }
            case 1: { // Grid
                int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(n))));
                int rows = (n + cols - 1) / cols;
                for (int i = 0; i < n; ++i) {
                    int col = i % cols;
                    int row = i / cols;
                    instances_[i].x = (cols > 1)
                        ? margin + static_cast<float>(col) * (1.0f - 2.0f * margin) / static_cast<float>(cols - 1)
                        : 0.5f;
                    instances_[i].y = (rows > 1)
                        ? margin + static_cast<float>(row) * (1.0f - 2.0f * margin) / static_cast<float>(rows - 1)
                        : 0.5f;
                }
                break;
            }
            case 2: { // Circle
                float radius = 0.3f;
                for (int i = 0; i < n; ++i) {
                    float angle = TAU * static_cast<float>(i) / static_cast<float>(n);
                    instances_[i].x = 0.5f + radius * std::cos(angle);
                    instances_[i].y = 0.5f + radius * std::sin(angle);
                }
                break;
            }
            case 3: { // Line
                for (int i = 0; i < n; ++i) {
                    instances_[i].x = (n > 1)
                        ? margin + static_cast<float>(i) * (1.0f - 2.0f * margin) / static_cast<float>(n - 1)
                        : 0.5f;
                    instances_[i].y = 0.5f;
                }
                break;
            }
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

                // Spread phase_offset across instances so each runs at
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

VIVID_REGISTER(InstancedShapes)
