#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/gpu_2d.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>

// =============================================================================
// Flocking2D — GPU compute-shader Reynolds boids for the 2D drawable pipeline
// =============================================================================
//
// Architecture (same pattern as Particles2D, different force model):
//   - Per-boid state (position, velocity) in ping-pong storage buffers.
//   - Compute shader runs one thread per boid; O(N²) neighbour loop computes
//     separation / alignment / cohesion forces, integrates velocity, wraps
//     position across NDC edges.
//   - Writes InstanceData2D records to an internal instance buffer each frame.
//   - Emits one VividDrawable2D (SHAPE, ADDITIVE) carrying that buffer.
//
// Boids are persistent — no spawning, no aging. Initial positions and
// velocities are generated CPU-side on every count-change rebuild and
// uploaded to both ping-pong buffers. After that the compute shader runs
// the integration loop forever.
//
// O(N²) fits on the GPU comfortably up to a few thousand boids. Beyond that
// spatial hashing / grid bins would be needed — deferred.
// =============================================================================

// ---------------------------------------------------------------------------
// Compute shader
// ---------------------------------------------------------------------------

static const char* kFlocking2DCompute = R"(
struct Boid {
    position: vec2f,
    velocity: vec2f,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
}

struct Params {
    count: u32,
    _pad_count: u32,
    dt: f32,
    time: f32,
    sep_radius: f32,
    view_radius: f32,
    sep_weight: f32,
    align_weight: f32,
    cohesion_weight: f32,
    max_speed: f32,
    min_speed: f32,
    size: f32,
    color: vec4f,
    wrap: u32,
    _pad0: u32,
    _pad1: u32,
    _pad2: u32,
}

struct InstanceData {
    transform: mat3x2f,
    color:     vec4f,
}

@group(0) @binding(0) var<storage, read>       boids_in:      array<Boid>;
@group(0) @binding(1) var<storage, read_write> boids_out:     array<Boid>;
@group(0) @binding(2) var<storage, read_write> instances_out: array<InstanceData>;
@group(0) @binding(3) var<uniform>             params: Params;

@compute @workgroup_size(256)
fn cs_main(@builtin(global_invocation_id) gid: vec3u) {
    let i = gid.x;
    if (i >= params.count) { return; }

    var b = boids_in[i];

    // --- Accumulate Reynolds forces by looping all other boids ---
    var sep_accum  = vec2f(0.0);
    var align_sum  = vec2f(0.0);
    var coh_sum    = vec2f(0.0);
    var align_n    = 0.0;
    var coh_n      = 0.0;

    let sep_r2  = params.sep_radius  * params.sep_radius;
    let view_r2 = params.view_radius * params.view_radius;

    for (var j = 0u; j < params.count; j = j + 1u) {
        if (j == i) { continue; }
        let other = boids_in[j];
        let diff = b.position - other.position;
        let d2 = dot(diff, diff);
        if (d2 > view_r2 || d2 <= 0.00001) { continue; }

        // Alignment + cohesion: neighbours within view_radius.
        align_sum += other.velocity;
        align_n   += 1.0;
        coh_sum   += other.position;
        coh_n     += 1.0;

        // Separation: only neighbours within sep_radius, weighted inverse.
        if (d2 < sep_r2) {
            let d = sqrt(d2);
            sep_accum += diff / max(d, 0.001);
        }
    }

    // Build steering forces.
    var steer = vec2f(0.0);

    // Separation — already the "away from neighbours" direction.
    steer += sep_accum * params.sep_weight;

    // Alignment — toward neighbour average velocity.
    if (align_n > 0.0) {
        let avg_vel = align_sum / align_n;
        steer += (avg_vel - b.velocity) * params.align_weight;
    }

    // Cohesion — toward neighbour centre of mass.
    if (coh_n > 0.0) {
        let centre = coh_sum / coh_n;
        steer += (centre - b.position) * params.cohesion_weight;
    }

    // Integrate velocity.
    b.velocity += steer * params.dt;

    // Clamp speed into [min_speed, max_speed].
    let sp = length(b.velocity);
    if (sp > params.max_speed) {
        b.velocity = b.velocity * (params.max_speed / sp);
    } else if (sp < params.min_speed && sp > 0.00001) {
        b.velocity = b.velocity * (params.min_speed / sp);
    }

    // Integrate position.
    b.position += b.velocity * params.dt;

    // Wrap across NDC [-1, 1] edges (optional).
    if (params.wrap != 0u) {
        if (b.position.x >  1.2) { b.position.x -= 2.4; }
        if (b.position.x < -1.2) { b.position.x += 2.4; }
        if (b.position.y >  1.2) { b.position.y -= 2.4; }
        if (b.position.y < -1.2) { b.position.y += 2.4; }
    }

    boids_out[i] = b;

    // Write InstanceData2D record: rotation (from velocity heading) × non-uniform
    // scale (elongated → arrow-like) × translation. The Render2D SDF for
    // shape_sides=3 puts the triangle's sharp vertex at local -X, so we map
    // local -X (not +X) to the velocity direction — i.e. rotate by heading+PI.
    let heading = atan2(b.velocity.y, b.velocity.x);
    let c = cos(heading);
    let s = sin(heading);
    let sx = params.size * 1.8;   // length (along motion)
    let sy = params.size * 0.7;   // width  (perpendicular)

    var inst: InstanceData;
    inst.transform = mat3x2f(
        vec2f(-c * sx, -s * sx),           // col 0: local +X → -velocity (so -X → +velocity = arrow tip forward)
        vec2f( s * sy, -c * sy),           // col 1: local +Y → flipped perpendicular
        vec2f(b.position.x, b.position.y)  // col 2: translation
    );
    inst.color = params.color;
    instances_out[i] = inst;
}
)";

// ---------------------------------------------------------------------------
// Params uniform (CPU mirror)
// ---------------------------------------------------------------------------

struct FlockParams {
    uint32_t count;
    uint32_t _pad_count;
    float    dt;
    float    time;
    float    sep_radius;
    float    view_radius;
    float    sep_weight;
    float    align_weight;
    float    cohesion_weight;
    float    max_speed;
    float    min_speed;
    float    size;
    float    color[4];
    uint32_t wrap;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};
static_assert(sizeof(FlockParams) == 80, "FlockParams must be 80 bytes");

// Boid CPU record — 32 bytes to match the WGSL Boid struct (8 floats).
struct BoidCpu {
    float position[2];
    float velocity[2];
    float _pad[4];
};
static_assert(sizeof(BoidCpu) == 32, "BoidCpu must be 32 bytes");

// ---------------------------------------------------------------------------
// Flocking2D operator
// ---------------------------------------------------------------------------

/**
 * @brief GPU Reynolds boids simulator emitting a 2D drawable.
 *
 * Simulates N persistent boids with classic separation / alignment / cohesion
 * steering on the GPU. Each frame runs an O(N²) neighbour loop per boid in a
 * compute shader and writes per-boid InstanceData2D records. Render2D draws
 * all boids in one `DrawIndexed(6, N)` call.
 *
 * Practical ceiling is around 2000–4000 boids on current hardware before the
 * O(N²) compute becomes a bottleneck; larger flocks would need spatial hashing.
 *
 * @param count           Number of boids (1–4096).
 * @param view_radius     Perception radius for alignment + cohesion (NDC units).
 * @param sep_radius      Perception radius for separation (NDC units).
 * @param separation      Separation force weight.
 * @param alignment       Alignment force weight.
 * @param cohesion        Cohesion force weight.
 * @param max_speed       Maximum boid speed (NDC units per second).
 * @param min_speed       Minimum boid speed — keeps boids from stalling.
 * @param wrap            Wrap positions across NDC edges (on/off).
 * @param size            Boid render radius (NDC).
 * @param softness        SDF edge softness.
 * @param r / g / b / a   Boid colour (multiplied by the Render2D blend).
 *
 * @tip Arrows point in direction of motion. Tighter `sep_radius` yields dense clustering.
 * @tip O(N²) compute — stays responsive to ~2000 boids. Above that spatial hashing would be needed.
 * @recipe Flocking2D -> Render2D -> Bloom -> video_out
 * @pitfall `alignment` + `cohesion` too high collapses the flock to one point.
 * @common_companions Render2D, Bloom, Particles2D
 * @best_used_with Render2D
 * @family 2D drawable pipeline
 * @see Render2D, Particles2D
 */
struct Flocking2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Flocking2D";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   count        {"count",        500, 1, 4096};
    vivid::Param<float> view_radius  {"view_radius",  0.25f, 0.01f, 2.0f};
    vivid::Param<float> sep_radius   {"sep_radius",   0.08f, 0.001f, 1.0f};
    vivid::Param<float> separation   {"separation",   1.5f,  0.0f, 10.0f};
    vivid::Param<float> alignment    {"alignment",    1.0f,  0.0f, 10.0f};
    vivid::Param<float> cohesion     {"cohesion",     1.0f,  0.0f, 10.0f};
    vivid::Param<float> max_speed    {"max_speed",    0.4f,  0.0f,  5.0f};
    vivid::Param<float> min_speed    {"min_speed",    0.1f,  0.0f,  5.0f};
    vivid::Param<int>   wrap         {"wrap",         1, {"Off", "On"}};
    vivid::Param<float> size         {"size",         0.012f, 0.001f, 0.1f};
    vivid::Param<float> softness     {"softness",     0.5f,  0.0f, 1.0f};
    vivid::Param<int>   seed         {"seed",         42, 0, 9999};

    vivid::Param<float> r {"r", 0.6f, 0.0f, 1.0f};
    vivid::Param<float> g {"g", 0.9f, 0.0f, 1.0f};
    vivid::Param<float> b {"b", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> a {"a", 0.9f, 0.0f, 1.0f};

    Flocking2D() {
        vivid::display_hint(r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(b, VIVID_DISPLAY_COLOR);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(count, "Flock");
        vivid::param_group(seed,  "Flock");
        vivid::param_group(wrap,  "Flock");

        vivid::param_group(view_radius, "Perception");
        vivid::param_group(sep_radius,  "Perception");

        vivid::param_group(separation, "Forces");
        vivid::param_group(alignment,  "Forces");
        vivid::param_group(cohesion,   "Forces");

        vivid::param_group(max_speed, "Speed");
        vivid::param_group(min_speed, "Speed");

        vivid::param_group(size,     "Appearance");
        vivid::param_group(softness, "Appearance");

        vivid::param_group(r, "Color");
        vivid::param_group(g, "Color");
        vivid::param_group(b, "Color");
        vivid::param_group(a, "Color");

        out.push_back(&count);
        out.push_back(&view_radius);
        out.push_back(&sep_radius);
        out.push_back(&separation);
        out.push_back(&alignment);
        out.push_back(&cohesion);
        out.push_back(&max_speed);
        out.push_back(&min_speed);
        out.push_back(&wrap);
        out.push_back(&size);
        out.push_back(&softness);
        out.push_back(&seed);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&a);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::drawable_port("drawable", VIVID_PORT_OUTPUT));
    }

    ~Flocking2D() override {
        vivid::gpu::release(compute_pipeline_);
        vivid::gpu::release(compute_shader_);
        vivid::gpu::release(compute_pipe_layout_);
        vivid::gpu::release(compute_bgl_);
        vivid::gpu::release(bind_group_a_);
        vivid::gpu::release(bind_group_b_);
        vivid::gpu::release(boid_buf_a_);
        vivid::gpu::release(boid_buf_b_);
        vivid::gpu::release(instance_buf_);
        vivid::gpu::release(params_ubo_);
    }

    void process_gpu(const VividGpuContext* ctx) override {
        uint32_t n = static_cast<uint32_t>(count.int_value());
        if (n == 0) n = 1;
        if (n > 4096) n = 4096;

        if (n != current_count_ || !compute_pipeline_ || seed.int_value() != current_seed_) {
            rebuild_gpu_resources(ctx, n);
        }
        if (!compute_pipeline_) return;

        // Upload params.
        FlockParams p{};
        p.count           = n;
        p.dt              = static_cast<float>(ctx->delta_time);
        elapsed_time_    += p.dt;
        p.time            = elapsed_time_;
        p.sep_radius      = sep_radius.value;
        p.view_radius     = view_radius.value;
        p.sep_weight      = separation.value;
        p.align_weight    = alignment.value;
        p.cohesion_weight = cohesion.value;
        p.max_speed       = max_speed.value;
        p.min_speed       = min_speed.value;
        p.size            = size.value;
        p.color[0]        = r.value;
        p.color[1]        = g.value;
        p.color[2]        = b.value;
        p.color[3]        = a.value;
        p.wrap            = wrap.int_value() ? 1u : 0u;
        wgpuQueueWriteBuffer(ctx->queue, params_ubo_, 0, &p, sizeof(p));

        // Compute pass.
        WGPUComputePassDescriptor cp{};
        cp.label = vivid_sv("Flocking2D Compute");
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(
            ctx->command_encoder, &cp);
        wgpuComputePassEncoderSetPipeline(pass, compute_pipeline_);
        wgpuComputePassEncoderSetBindGroup(pass, 0,
            ping_ ? bind_group_a_ : bind_group_b_, 0, nullptr);
        uint32_t workgroups = (n + 255) / 256;
        wgpuComputePassEncoderDispatchWorkgroups(pass, workgroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        ping_ = !ping_;

        // Emit drawable. Triangle (3 sides) pointed in +X; per-instance rotation
        // in the shader aligns each triangle with its velocity direction.
        // Alpha blend keeps arrows discrete — additive would wash out overlaps.
        vivid::gpu::drawable_identity(output_);
        output_.type             = vivid::gpu::VIVID_DRAWABLE2D_SHAPE;
        output_.blend_mode       = vivid::gpu::VIVID_BLEND_ALPHA;
        output_.shape_sides      = 3;
        output_.shape_softness   = softness.value;
        output_.shape_star_factor = 0.0f;
        output_.color[0] = 1.0f;
        output_.color[1] = 1.0f;
        output_.color[2] = 1.0f;
        output_.color[3] = 1.0f;
        output_.instance_buffer  = instance_buf_;
        output_.instance_count   = n;
        ctx->custom_outputs[0]   = &output_;
    }

private:
    vivid::gpu::VividDrawable2D output_{};

    WGPUComputePipeline  compute_pipeline_    = nullptr;
    WGPUShaderModule     compute_shader_      = nullptr;
    WGPUPipelineLayout   compute_pipe_layout_ = nullptr;
    WGPUBindGroupLayout  compute_bgl_         = nullptr;
    WGPUBindGroup        bind_group_a_        = nullptr;
    WGPUBindGroup        bind_group_b_        = nullptr;
    WGPUBuffer           boid_buf_a_          = nullptr;
    WGPUBuffer           boid_buf_b_          = nullptr;
    WGPUBuffer           instance_buf_        = nullptr;
    WGPUBuffer           params_ubo_          = nullptr;

    uint32_t current_count_ = 0;
    int      current_seed_  = 0;
    bool     ping_          = true;
    float    elapsed_time_  = 0.0f;

    static constexpr uint64_t kBoidBytes     = sizeof(BoidCpu);
    static constexpr uint64_t kInstanceBytes = sizeof(vivid::gpu::InstanceData2D);

    void rebuild_gpu_resources(const VividGpuContext* gpu, uint32_t n) {
        vivid::gpu::release(compute_pipeline_);
        vivid::gpu::release(compute_shader_);
        vivid::gpu::release(compute_pipe_layout_);
        vivid::gpu::release(compute_bgl_);
        vivid::gpu::release(bind_group_a_);
        vivid::gpu::release(bind_group_b_);
        vivid::gpu::release(boid_buf_a_);
        vivid::gpu::release(boid_buf_b_);
        vivid::gpu::release(instance_buf_);
        vivid::gpu::release(params_ubo_);

        current_count_ = n;
        current_seed_  = seed.int_value();
        ping_          = true;

        uint64_t boid_bytes = static_cast<uint64_t>(n) * kBoidBytes;
        if (boid_bytes < kBoidBytes) boid_bytes = kBoidBytes;

        auto make_storage = [&](const char* label, uint64_t bytes) -> WGPUBuffer {
            WGPUBufferDescriptor d{};
            d.label = vivid_sv(label);
            d.size  = bytes;
            d.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            return wgpuDeviceCreateBuffer(gpu->device, &d);
        };

        boid_buf_a_ = make_storage("Flocking2D Buf A", boid_bytes);
        boid_buf_b_ = make_storage("Flocking2D Buf B", boid_bytes);

        uint64_t inst_bytes = static_cast<uint64_t>(n) * kInstanceBytes;
        if (inst_bytes < kInstanceBytes) inst_bytes = kInstanceBytes;
        instance_buf_ = make_storage("Flocking2D Instances", inst_bytes);

        // CPU-side random init: distribute boids across NDC [-1, 1] with
        // random unit-vector velocities at max_speed. Deterministic from seed.
        std::vector<BoidCpu> init(n);
        uint32_t rng = static_cast<uint32_t>(current_seed_) * 2654435761u + 1u;
        auto rand01 = [&]() -> float {
            rng = rng * 1664525u + 1013904223u;
            return static_cast<float>(rng >> 8) / 16777216.0f;
        };
        const float sp = max_speed.value > 0.0f ? max_speed.value : 0.3f;
        for (uint32_t i = 0; i < n; ++i) {
            init[i].position[0] = rand01() * 2.0f - 1.0f;
            init[i].position[1] = rand01() * 2.0f - 1.0f;
            float angle = rand01() * 6.28318530718f;
            init[i].velocity[0] = std::cos(angle) * sp;
            init[i].velocity[1] = std::sin(angle) * sp;
            for (int k = 0; k < 4; ++k) init[i]._pad[k] = 0.0f;
        }
        wgpuQueueWriteBuffer(gpu->queue, boid_buf_a_, 0, init.data(), boid_bytes);
        wgpuQueueWriteBuffer(gpu->queue, boid_buf_b_, 0, init.data(), boid_bytes);

        params_ubo_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(FlockParams),
                                                       "Flocking2D Params");

        // Compile shader.
        std::string wgsl = std::string(vivid::gpu::WGSL_CONSTANTS) + kFlocking2DCompute;
        WGPUShaderSourceWGSL src{};
        src.chain.sType = WGPUSType_ShaderSourceWGSL;
        src.code        = vivid_sv(wgsl.c_str());
        WGPUShaderModuleDescriptor sm{};
        sm.nextInChain = &src.chain;
        sm.label       = vivid_sv("Flocking2D Compute Shader");
        compute_shader_ = wgpuDeviceCreateShaderModule(gpu->device, &sm);
        if (!compute_shader_) {
            std::fprintf(stderr, "[flocking_2d] shader compile failed\n");
            return;
        }

        WGPUBindGroupLayoutEntry entries[4]{};
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
        entries[3].buffer.minBindingSize = sizeof(FlockParams);

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label      = vivid_sv("Flocking2D BGL");
        bgl_desc.entryCount = 4;
        bgl_desc.entries    = entries;
        compute_bgl_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl{};
        pl.label                = vivid_sv("Flocking2D PL");
        pl.bindGroupLayoutCount = 1;
        pl.bindGroupLayouts     = &compute_bgl_;
        compute_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl);

        WGPUComputePipelineDescriptor cp{};
        cp.label              = vivid_sv("Flocking2D Compute Pipeline");
        cp.layout             = compute_pipe_layout_;
        cp.compute.module     = compute_shader_;
        cp.compute.entryPoint = vivid_sv("cs_main");
        compute_pipeline_ = wgpuDeviceCreateComputePipeline(gpu->device, &cp);
        if (!compute_pipeline_) {
            std::fprintf(stderr, "[flocking_2d] compute pipeline creation failed\n");
            return;
        }

        create_bind_group(gpu, boid_buf_a_, boid_buf_b_, boid_bytes, &bind_group_a_, "Flocking2D BG A");
        create_bind_group(gpu, boid_buf_b_, boid_buf_a_, boid_bytes, &bind_group_b_, "Flocking2D BG B");
    }

    void create_bind_group(const VividGpuContext* gpu,
                           WGPUBuffer read_buf, WGPUBuffer write_buf,
                           uint64_t boid_bytes,
                           WGPUBindGroup* out_bg, const char* label) {
        uint64_t inst_bytes = static_cast<uint64_t>(current_count_) * kInstanceBytes;
        if (inst_bytes < kInstanceBytes) inst_bytes = kInstanceBytes;

        WGPUBindGroupEntry entries[4]{};
        entries[0].binding = 0;
        entries[0].buffer  = read_buf;
        entries[0].size    = boid_bytes;
        entries[1].binding = 1;
        entries[1].buffer  = write_buf;
        entries[1].size    = boid_bytes;
        entries[2].binding = 2;
        entries[2].buffer  = instance_buf_;
        entries[2].size    = inst_bytes;
        entries[3].binding = 3;
        entries[3].buffer  = params_ubo_;
        entries[3].size    = sizeof(FlockParams);

        WGPUBindGroupDescriptor desc{};
        desc.label      = vivid_sv(label);
        desc.layout     = compute_bgl_;
        desc.entryCount = 4;
        desc.entries    = entries;
        *out_bg = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    }
};

VIVID_DEFINE_OP(Flocking2D) {
}


VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividDrawable2D)
