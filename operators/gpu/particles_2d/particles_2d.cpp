#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/gpu_2d.h"
#include <cstdio>
#include <cstring>
#include <vector>

// =============================================================================
// Particles2D — GPU compute-shader particle simulator for the 2D drawable pipeline
// =============================================================================
//
// Architecture (mirrors Particles3D):
//   - Per-particle state (position, velocity, age, lifetime) stored in two
//     ping-pong storage buffers. One frame reads A/writes B, the next swaps.
//   - Compute shader runs one thread per particle: integrate physics, spawn
//     new particles into dead slots via an atomic counter, write per-particle
//     InstanceData2D records into an internal instance buffer.
//   - Emits a single VividDrawable2D of type SHAPE with the instance buffer
//     attached. Render2D's shape-instanced pipeline renders all N particles
//     via one DrawIndexed(6, N) call.
//
// Output: VividDrawable2D (SHAPE, blend=ADDITIVE). No upstream drawable input
// — Particles2D is a pure source. Future variants may thread a texture for
// sprite particles.
// =============================================================================

// ---------------------------------------------------------------------------
// Compute shader: particle simulation + instance data generation (2D)
// ---------------------------------------------------------------------------

static const char* kParticles2DCompute = R"(
struct Particle {
    position: vec2f,
    velocity: vec2f,
    age: f32,
    lifetime: f32,
    _pad0: f32,
    _pad1: f32,
}

struct Params {
    max_count: u32,
    new_spawns: u32,
    dt: f32,
    gravity: f32,
    speed: f32,
    spread_rad: f32,
    lifetime: f32,
    size: f32,
    color: vec4f,
    seed: u32,
    noise_octaves: u32,
    noise_scale: f32,
    noise_speed: f32,
    curl_strength: f32,
    drag: f32,
    time: f32,
    bounds: f32,
    learning_mode: u32,
    _pad0: f32,
    _pad1: f32,
}

// Matches CPU-side InstanceData2D (48 bytes):
//   mat3x2f is 24 bytes + 8 bytes WGSL align pad = 32; color 16 → 48 total.
struct InstanceData {
    transform: mat3x2f,
    color:     vec4f,
}

@group(0) @binding(0) var<storage, read>       particles_in:  array<Particle>;
@group(0) @binding(1) var<storage, read_write> particles_out: array<Particle>;
@group(0) @binding(2) var<storage, read_write> instances_out: array<InstanceData>;
@group(0) @binding(3) var<uniform>             params: Params;
@group(0) @binding(4) var<storage, read_write> counter: atomic<u32>;

// ---------------------------------------------------------------------------
// 2D simplex noise (adapted from common 2D simplex implementations)
// ---------------------------------------------------------------------------

fn permute2(x: vec3f) -> vec3f {
    return (((x * 34.0) + 1.0) * x) % 289.0;
}

fn simplex2D(v: vec2f) -> f32 {
    let C = vec4f(0.211324865405187, 0.366025403784439,
                 -0.577350269189626, 0.024390243902439);
    var i  = floor(v + dot(v, C.yy));
    let x0 = v - i + dot(i, C.xx);
    var i1 = vec2f(0.0, 1.0);
    if (x0.x > x0.y) { i1 = vec2f(1.0, 0.0); }
    let x1 = x0 - i1 + C.xx;
    let x2 = x0 - vec2f(1.0, 1.0) + 2.0 * C.xx;
    i = i % 289.0;
    let p = permute2(permute2(vec3f(0.0, i1.y, 1.0) + i.y)
                     + vec3f(0.0, i1.x, 1.0) + i.x);
    var m = max(vec3f(0.5) - vec3f(dot(x0, x0), dot(x1, x1), dot(x2, x2)), vec3f(0.0));
    m = m * m;
    m = m * m;
    let x_ = 2.0 * fract(p * C.www) - 1.0;
    let h  = abs(x_) - 0.5;
    let ox = floor(x_ + 0.5);
    let a0 = x_ - ox;
    m = m * (1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h));
    var g: vec3f;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.y = a0.y * x1.x + h.y * x1.y;
    g.z = a0.z * x2.x + h.z * x2.y;
    return 130.0 * dot(m, g);
}

fn fbm_simplex2D(p_in: vec2f, octaves: u32, lacunarity: f32, persistence: f32) -> f32 {
    var value = 0.0;
    var amplitude = 1.0;
    var frequency = 1.0;
    var max_value = 0.0;
    var p = p_in;
    for (var i = 0u; i < octaves; i++) {
        value += amplitude * simplex2D(p * frequency);
        max_value += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return value / max_value;
}

fn curl_noise_2d(p: vec2f, octaves: u32) -> vec2f {
    // 2D curl of a scalar potential field F: curl = (dF/dy, -dF/dx).
    // Produces divergence-free advection, which looks organic for particles.
    let e = 0.01;
    let f_py = fbm_simplex2D(p + vec2f(0.0, e), octaves, 2.0, 0.5);
    let f_ny = fbm_simplex2D(p - vec2f(0.0, e), octaves, 2.0, 0.5);
    let f_px = fbm_simplex2D(p + vec2f(e, 0.0), octaves, 2.0, 0.5);
    let f_nx = fbm_simplex2D(p - vec2f(e, 0.0), octaves, 2.0, 0.5);
    let inv2e = 1.0 / (2.0 * e);
    return vec2f((f_py - f_ny) * inv2e, -(f_px - f_nx) * inv2e);
}

// ---------------------------------------------------------------------------
// PCG hash — deterministic PRNG
// ---------------------------------------------------------------------------

fn pcg_hash(input: u32) -> u32 {
    let state = input * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

fn rand_float(seed: u32) -> f32 {
    return f32(pcg_hash(seed)) / 4294967295.0;
}

// ---------------------------------------------------------------------------
// Compute entry point
// ---------------------------------------------------------------------------

@compute @workgroup_size(256)
fn cs_main(@builtin(global_invocation_id) gid: vec3u) {
    let idx = gid.x;
    if (idx >= params.max_count) { return; }

    var p = particles_in[idx];
    let is_dead = p.lifetime <= 0.0;

    if (is_dead) {
        // Try to claim a spawn slot.
        let slot = atomicAdd(&counter, 1u);
        if (slot < params.new_spawns) {
            let s0 = pcg_hash(params.seed + idx * 3u);
            let s1 = pcg_hash(s0);
            let s2 = pcg_hash(s1);

            // Emission cone in 2D: angle within ±spread_rad/2 of "up"
            // (or any chosen axis). We use ±spread_rad/2 centred on +Y.
            let angle = (rand_float(s0) - 0.5) * params.spread_rad;
            let vx = sin(angle) * params.speed;
            let vy = cos(angle) * params.speed;

            p.position = vec2f(0.0, 0.0);
            p.velocity = vec2f(vx, vy);
            p.age = 0.0;
            p.lifetime = params.lifetime * (0.8 + 0.4 * rand_float(s2));
        } else {
            p.lifetime = 0.0;  // stay dead
        }
    } else {
        // Integrate live particle.
        p.velocity.y += params.gravity * params.dt;

        // Curl-noise force.
        if (params.curl_strength > 0.0) {
            let noise_pos = p.position * params.noise_scale
                          + vec2f(0.0, params.time * params.noise_speed);
            let curl = curl_noise_2d(noise_pos, params.noise_octaves);
            p.velocity += curl * params.curl_strength * params.dt;
        }

        // Drag.
        if (params.drag > 0.0) {
            p.velocity *= 1.0 - params.drag * params.dt;
        }

        p.position += p.velocity * params.dt;

        // Bounds: kill particles that escape the NDC-like region so runaway
        // noise doesn't silently eat FPS. Off by default (bounds = 0).
        if (params.bounds > 0.0) {
            if (abs(p.position.x) > params.bounds ||
                abs(p.position.y) > params.bounds) {
                p.lifetime = 0.0;
            }
        }
        p.age += params.dt;
        if (p.age >= p.lifetime) {
            p.lifetime = 0.0;
        }
    }

    particles_out[idx] = p;

    // Write InstanceData2D for this slot.
    var inst: InstanceData;
    if (p.lifetime > 0.0) {
        let age_ratio   = p.age / p.lifetime;
        let size_factor = 1.0 - age_ratio * age_ratio;   // shrink over lifetime
        let alpha       = 1.0 - age_ratio;               // fade over lifetime
        let sz = params.size * size_factor;

        // Column-major mat3x2 = (scale, 0) / (0, scale) / (tx, ty)
        inst.transform = mat3x2f(
            vec2f(sz, 0.0),
            vec2f(0.0, sz),
            vec2f(p.position.x, p.position.y)
        );
        inst.color = vec4f(params.color.rgb, params.color.a * alpha);
    } else {
        // Dead particle — degenerate transform (zero scale, far off-screen).
        inst.transform = mat3x2f(
            vec2f(0.0, 0.0),
            vec2f(0.0, 0.0),
            vec2f(99999.0, 99999.0)
        );
        inst.color = vec4f(0.0);
    }
    instances_out[idx] = inst;
}
)";

// ---------------------------------------------------------------------------
// Params uniform (CPU-side mirror of the WGSL Params struct).
// ---------------------------------------------------------------------------

struct ParamsData {
    uint32_t max_count;       //   0
    uint32_t new_spawns;      //   4
    float    dt;              //   8
    float    gravity;         //  12
    float    speed;           //  16
    float    spread_rad;      //  20
    float    lifetime;        //  24
    float    size;            //  28
    float    color[4];        //  32
    uint32_t seed;            //  48
    uint32_t noise_octaves;   //  52
    float    noise_scale;     //  56
    float    noise_speed;     //  60
    float    curl_strength;   //  64
    float    drag;            //  68
    float    time;            //  72
    float    bounds;          //  76
    uint32_t learning_mode;   //  80
    float    _pad0;           //  84
    float    _pad1;           //  88
    float    _pad2;           //  92
};
static_assert(sizeof(ParamsData) == 96, "ParamsData must be 96 bytes");

// ---------------------------------------------------------------------------
// Particles2D operator
// ---------------------------------------------------------------------------

/**
 * @brief Compute-shader particle simulator emitting a 2D drawable.
 *
 * Self-contained GPU particle system for the 2D drawable pipeline. Per-particle
 * state lives in ping-pong storage buffers; one compute pass per frame advances
 * every particle and writes InstanceData2D records. The output drawable carries
 * the instance buffer; Render2D draws all particles in one instanced call.
 *
 * @param count          Maximum live particle count (1–100000).
 * @param emission_rate  New particles spawned per second.
 * @param lifetime       Lifetime of each particle (seconds).
 * @param speed          Base particle speed.
 * @param gravity        Y-axis acceleration.
 * @param spread         Emission cone width (degrees).
 * @param drag           Velocity damping.
 * @param curl_strength  Curl-noise force amplitude.
 * @param noise_scale    Spatial frequency of the noise field.
 * @param noise_speed    Rate at which the noise field evolves.
 * @param noise_octaves  Number of FBM octaves (1..4).
 * @param size           Base particle radius (NDC units).
 * @param softness       Edge softness of the SDF circle.
 * @param bounds         NDC-box clamp; 0 = disabled.
 * @param learning_mode  Advanced vs Beginner preset.
 * @param r / g / b / a  Base color (multiplied with per-particle alpha fade).
 *
 * @tip Feed directly into Render2D — the compute shader owns the instance buffer.
 * @tip Curl noise looks best at `curl_strength` 1–3 with small `curl_scale` (~2).
 * @recipe Particles2D -> Render2D -> Bloom -> video_out
 * @pitfall `count` > 100K will tax lower-end GPUs; start at 5000 and scale up.
 * @common_companions Render2D, Bloom, Feedback
 * @best_used_with Render2D
 * @family 2D drawable pipeline
 * @see Render2D, Flocking2D, Bloom
 */
struct Particles2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Particles2D";
    static constexpr bool kTimeDependent = true;

    // Emission
    vivid::Param<int>   count         {"count",         2000, 1, 100000};
    vivid::Param<float> emission_rate {"emission_rate", 500.0f, 0.0f, 10000.0f};
    vivid::Param<float> lifetime      {"lifetime",      2.0f, 0.1f, 30.0f};

    // Physics
    vivid::Param<float> speed   {"speed",   0.4f, 0.0f, 5.0f};
    vivid::Param<float> gravity {"gravity", -0.2f, -5.0f, 5.0f};
    vivid::Param<float> spread  {"spread",  45.0f, 0.0f, 360.0f};
    vivid::Param<float> drag    {"drag",    0.0f, 0.0f, 10.0f};

    // Curl noise
    vivid::Param<float> curl_strength {"curl_strength", 0.0f, 0.0f, 5.0f};
    vivid::Param<float> noise_scale   {"noise_scale",   1.0f, 0.01f, 10.0f};
    vivid::Param<float> noise_speed   {"noise_speed",   0.5f, 0.0f, 5.0f};
    vivid::Param<int>   noise_octaves {"noise_octaves", 2, 1, 4};

    // Appearance
    vivid::Param<float> size     {"size",     0.015f, 0.001f, 0.2f};
    vivid::Param<float> softness {"softness", 0.5f,   0.0f,   1.0f};
    vivid::Param<float> bounds   {"bounds",   2.0f,   0.0f,  10.0f};

    // Color (default warm ember)
    vivid::Param<float> r {"r", 1.0f,  0.0f, 1.0f};
    vivid::Param<float> g {"g", 0.6f,  0.0f, 1.0f};
    vivid::Param<float> b {"b", 0.25f, 0.0f, 1.0f};
    vivid::Param<float> a {"a", 0.8f,  0.0f, 1.0f};

    // Learning mode (Advanced/Beginner)
    vivid::Param<int> learning_mode {"learning_mode", 0, {"Advanced", "Beginner"}};

    Particles2D() {
        vivid::display_hint(r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(b, VIVID_DISPLAY_COLOR);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(learning_mode, "Learning");

        vivid::param_group(count,         "Emission");
        vivid::param_group(emission_rate, "Emission");
        vivid::param_group(lifetime,      "Emission");

        vivid::param_group(speed,   "Physics");
        vivid::param_group(gravity, "Physics");
        vivid::param_group(spread,  "Physics");
        vivid::param_group(drag,    "Physics");

        vivid::param_group(curl_strength, "Curl Noise");
        vivid::param_group(noise_scale,   "Curl Noise");
        vivid::param_group(noise_speed,   "Curl Noise");
        vivid::param_group(noise_octaves, "Curl Noise");

        vivid::param_group(size,     "Appearance");
        vivid::param_group(softness, "Appearance");
        vivid::param_group(bounds,   "Appearance");

        vivid::param_group(r, "Color");
        vivid::param_group(g, "Color");
        vivid::param_group(b, "Color");
        vivid::param_group(a, "Color");

        out.push_back(&count);
        out.push_back(&emission_rate);
        out.push_back(&lifetime);
        out.push_back(&speed);
        out.push_back(&gravity);
        out.push_back(&spread);
        out.push_back(&drag);
        out.push_back(&curl_strength);
        out.push_back(&noise_scale);
        out.push_back(&noise_speed);
        out.push_back(&noise_octaves);
        out.push_back(&size);
        out.push_back(&softness);
        out.push_back(&bounds);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&a);
        out.push_back(&learning_mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::drawable_port("drawable", VIVID_PORT_OUTPUT));
    }

    ~Particles2D() override {
        vivid::gpu::release(compute_pipeline_);
        vivid::gpu::release(compute_shader_);
        vivid::gpu::release(compute_pipe_layout_);
        vivid::gpu::release(compute_bgl_);
        vivid::gpu::release(bind_group_a_);
        vivid::gpu::release(bind_group_b_);
        vivid::gpu::release(particle_buf_a_);
        vivid::gpu::release(particle_buf_b_);
        vivid::gpu::release(instance_buf_);
        vivid::gpu::release(params_ubo_);
        vivid::gpu::release(counter_buf_);
    }

    void process_gpu(const VividGpuContext* ctx) override {
        uint32_t max_count = static_cast<uint32_t>(count.int_value());
        if (max_count == 0) max_count = 1;

        if (max_count != current_count_ || !compute_pipeline_) {
            rebuild_gpu_resources(ctx, max_count);
        }
        if (!compute_pipeline_) return;

        float dt = static_cast<float>(ctx->delta_time);

        // Fractional spawn accumulator.
        spawn_accumulator_ += emission_rate.value * dt;
        uint32_t new_spawns = static_cast<uint32_t>(spawn_accumulator_);
        spawn_accumulator_ -= static_cast<float>(new_spawns);
        if (new_spawns > max_count) new_spawns = max_count;

        // Upload params uniform.
        const bool beginner = (learning_mode.int_value() == 1);
        ParamsData params{};
        params.max_count     = max_count;
        params.new_spawns    = new_spawns;
        params.dt            = dt;
        params.gravity       = gravity.value;
        params.speed         = speed.value;
        params.spread_rad    = spread.value * (3.14159265358979f / 180.0f);
        params.lifetime      = lifetime.value;
        params.size          = size.value;
        params.color[0]      = r.value;
        params.color[1]      = g.value;
        params.color[2]      = b.value;
        params.color[3]      = a.value;
        params.seed          = frame_counter_++;
        params.noise_octaves = beginner ? 1u : static_cast<uint32_t>(noise_octaves.int_value());
        params.noise_scale   = beginner ? 1.0f : noise_scale.value;
        params.noise_speed   = beginner ? 0.0f : noise_speed.value;
        params.curl_strength = beginner ? 0.0f : curl_strength.value;
        params.drag          = beginner ? 0.05f : drag.value;
        elapsed_time_ += dt;
        params.time          = elapsed_time_;
        params.bounds        = bounds.value;
        params.learning_mode = beginner ? 1u : 0u;
        wgpuQueueWriteBuffer(ctx->queue, params_ubo_, 0, &params, sizeof(params));

        // Reset atomic counter.
        uint32_t zero = 0;
        wgpuQueueWriteBuffer(ctx->queue, counter_buf_, 0, &zero, sizeof(zero));

        // Compute pass.
        WGPUComputePassDescriptor cp{};
        cp.label = vivid_sv("Particles2D Compute");
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(
            ctx->command_encoder, &cp);
        wgpuComputePassEncoderSetPipeline(pass, compute_pipeline_);
        wgpuComputePassEncoderSetBindGroup(pass, 0,
            ping_ ? bind_group_a_ : bind_group_b_, 0, nullptr);
        uint32_t workgroups = (max_count + 255) / 256;
        wgpuComputePassEncoderDispatchWorkgroups(pass, workgroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        ping_ = !ping_;

        // Emit the drawable.
        vivid::gpu::drawable_identity(output_);
        output_.type            = vivid::gpu::VIVID_DRAWABLE2D_SHAPE;
        output_.blend_mode      = vivid::gpu::VIVID_BLEND_ADDITIVE;
        output_.shape_sides     = 0;            // circle
        output_.shape_softness  = softness.value;
        output_.shape_star_factor = 0.0f;
        output_.color[0] = 1.0f;
        output_.color[1] = 1.0f;
        output_.color[2] = 1.0f;
        output_.color[3] = 1.0f;
        output_.instance_buffer = instance_buf_;
        output_.instance_count  = max_count;
        ctx->custom_outputs[0]  = &output_;
    }

private:
    vivid::gpu::VividDrawable2D output_{};

    // Compute pipeline
    WGPUComputePipeline  compute_pipeline_    = nullptr;
    WGPUShaderModule     compute_shader_      = nullptr;
    WGPUPipelineLayout   compute_pipe_layout_ = nullptr;
    WGPUBindGroupLayout  compute_bgl_         = nullptr;

    // Ping-pong bind groups
    WGPUBindGroup bind_group_a_ = nullptr;
    WGPUBindGroup bind_group_b_ = nullptr;

    // GPU buffers
    WGPUBuffer particle_buf_a_ = nullptr;
    WGPUBuffer particle_buf_b_ = nullptr;
    WGPUBuffer instance_buf_   = nullptr;
    WGPUBuffer params_ubo_     = nullptr;
    WGPUBuffer counter_buf_    = nullptr;

    uint32_t current_count_     = 0;
    bool     ping_              = true;
    float    spawn_accumulator_ = 0.0f;
    uint32_t frame_counter_     = 0;
    float    elapsed_time_      = 0.0f;

    static constexpr uint64_t kParticleRecordBytes = 32;  // vec2 pos, vec2 vel, age, lifetime, pad (WGSL alignment)
    static constexpr uint64_t kInstanceRecordBytes = sizeof(vivid::gpu::InstanceData2D);

    void rebuild_gpu_resources(const VividGpuContext* gpu, uint32_t max_count) {
        vivid::gpu::release(compute_pipeline_);
        vivid::gpu::release(compute_shader_);
        vivid::gpu::release(compute_pipe_layout_);
        vivid::gpu::release(compute_bgl_);
        vivid::gpu::release(bind_group_a_);
        vivid::gpu::release(bind_group_b_);
        vivid::gpu::release(particle_buf_a_);
        vivid::gpu::release(particle_buf_b_);
        vivid::gpu::release(instance_buf_);
        vivid::gpu::release(params_ubo_);
        vivid::gpu::release(counter_buf_);

        current_count_ = max_count;
        ping_          = true;

        auto make_storage = [&](const char* label, uint64_t bytes) -> WGPUBuffer {
            WGPUBufferDescriptor d{};
            d.label = vivid_sv(label);
            d.size  = bytes;
            d.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            return wgpuDeviceCreateBuffer(gpu->device, &d);
        };

        uint64_t particle_bytes = static_cast<uint64_t>(max_count) * kParticleRecordBytes;
        if (particle_bytes < kParticleRecordBytes) particle_bytes = kParticleRecordBytes;
        particle_buf_a_ = make_storage("Particles2D Buf A", particle_bytes);
        particle_buf_b_ = make_storage("Particles2D Buf B", particle_bytes);

        uint64_t instance_bytes = static_cast<uint64_t>(max_count) * kInstanceRecordBytes;
        if (instance_bytes < kInstanceRecordBytes) instance_bytes = kInstanceRecordBytes;
        instance_buf_ = make_storage("Particles2D Instances", instance_bytes);

        // Zero-fill particle buffers — all particles start dead.
        std::vector<uint8_t> zeros(static_cast<size_t>(particle_bytes), 0);
        wgpuQueueWriteBuffer(gpu->queue, particle_buf_a_, 0, zeros.data(), particle_bytes);
        wgpuQueueWriteBuffer(gpu->queue, particle_buf_b_, 0, zeros.data(), particle_bytes);

        params_ubo_  = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(ParamsData),
                                                        "Particles2D Params");
        counter_buf_ = make_storage("Particles2D Counter", 4);

        // Compile compute shader.
        std::string wgsl = std::string(vivid::gpu::WGSL_CONSTANTS) + kParticles2DCompute;
        WGPUShaderSourceWGSL src{};
        src.chain.sType = WGPUSType_ShaderSourceWGSL;
        src.code        = vivid_sv(wgsl.c_str());
        WGPUShaderModuleDescriptor sm{};
        sm.nextInChain = &src.chain;
        sm.label       = vivid_sv("Particles2D Compute Shader");
        compute_shader_ = wgpuDeviceCreateShaderModule(gpu->device, &sm);
        if (!compute_shader_) {
            std::fprintf(stderr, "[particles_2d] shader compile failed\n");
            return;
        }

        // Bind group layout: 5 entries.
        WGPUBindGroupLayoutEntry entries[5]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Compute;
        entries[1].buffer.type = WGPUBufferBindingType_Storage;
        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Compute;
        entries[2].buffer.type = WGPUBufferBindingType_Storage;
        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Compute;
        entries[3].buffer.type = WGPUBufferBindingType_Uniform;
        entries[3].buffer.minBindingSize = sizeof(ParamsData);
        entries[4].binding = 4;
        entries[4].visibility = WGPUShaderStage_Compute;
        entries[4].buffer.type = WGPUBufferBindingType_Storage;
        entries[4].buffer.minBindingSize = 4;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label      = vivid_sv("Particles2D BGL");
        bgl_desc.entryCount = 5;
        bgl_desc.entries    = entries;
        compute_bgl_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label                = vivid_sv("Particles2D Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts     = &compute_bgl_;
        compute_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        WGPUComputePipelineDescriptor cp_desc{};
        cp_desc.label              = vivid_sv("Particles2D Compute Pipeline");
        cp_desc.layout             = compute_pipe_layout_;
        cp_desc.compute.module     = compute_shader_;
        cp_desc.compute.entryPoint = vivid_sv("cs_main");
        compute_pipeline_ = wgpuDeviceCreateComputePipeline(gpu->device, &cp_desc);
        if (!compute_pipeline_) {
            std::fprintf(stderr, "[particles_2d] compute pipeline creation failed\n");
            return;
        }

        create_bind_group(gpu, particle_buf_a_, particle_buf_b_,
                          particle_bytes, &bind_group_a_, "Particles2D BG A");
        create_bind_group(gpu, particle_buf_b_, particle_buf_a_,
                          particle_bytes, &bind_group_b_, "Particles2D BG B");
    }

    void create_bind_group(const VividGpuContext* gpu,
                            WGPUBuffer read_buf, WGPUBuffer write_buf,
                            uint64_t particle_bytes,
                            WGPUBindGroup* out_bg, const char* label) {
        uint64_t instance_bytes = static_cast<uint64_t>(current_count_) * kInstanceRecordBytes;
        if (instance_bytes < kInstanceRecordBytes) instance_bytes = kInstanceRecordBytes;

        WGPUBindGroupEntry entries[5]{};
        entries[0].binding = 0;
        entries[0].buffer  = read_buf;
        entries[0].size    = particle_bytes;
        entries[1].binding = 1;
        entries[1].buffer  = write_buf;
        entries[1].size    = particle_bytes;
        entries[2].binding = 2;
        entries[2].buffer  = instance_buf_;
        entries[2].size    = instance_bytes;
        entries[3].binding = 3;
        entries[3].buffer  = params_ubo_;
        entries[3].size    = sizeof(ParamsData);
        entries[4].binding = 4;
        entries[4].buffer  = counter_buf_;
        entries[4].size    = 4;

        WGPUBindGroupDescriptor desc{};
        desc.label      = vivid_sv(label);
        desc.layout     = compute_bgl_;
        desc.entryCount = 5;
        desc.entries    = entries;
        *out_bg = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    }
};

VIVID_REGISTER(Particles2D)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividDrawable2D)
