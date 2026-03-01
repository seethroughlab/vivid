#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

// =============================================================================
// Plexus WGSL Shader — four entry points: vs_line, fs_line, vs_point, fs_point
// =============================================================================

static const char* kPlexusShader = R"(

struct Uniforms {
    resolution: vec2f,
    point_size: f32,
    line_thickness: f32,
    color_r: f32,
    color_g: f32,
    color_b: f32,
    _pad: f32,
};

struct PointInstance {
    pos: vec2f,
    size_factor: f32,
    _pad: f32,
};

struct LineInstance {
    a: vec2f,
    b: vec2f,
    alpha: f32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) alpha: f32,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(1) @binding(0) var<storage, read> points: array<PointInstance>;
@group(1) @binding(1) var<storage, read> lines: array<LineInstance>;

// --- Point pipeline ---

@vertex
fn vs_point(@builtin(vertex_index) vi: u32,
            @builtin(instance_index) ii: u32) -> VertexOutput {
    // 6 vertices per quad (2 triangles)
    var corner: vec2f;
    switch vi {
        case 0u: { corner = vec2f(-1.0, -1.0); }
        case 1u: { corner = vec2f( 1.0, -1.0); }
        case 2u: { corner = vec2f(-1.0,  1.0); }
        case 3u: { corner = vec2f(-1.0,  1.0); }
        case 4u: { corner = vec2f( 1.0, -1.0); }
        default: { corner = vec2f( 1.0,  1.0); }
    }

    let inst = points[ii];
    let aspect = uniforms.resolution.x / uniforms.resolution.y;
    let size = uniforms.point_size * inst.size_factor;

    // Instance center in NDC: map from (0..1) to (-1..1)
    let cx = inst.pos.x * 2.0 - 1.0;
    let cy = 1.0 - inst.pos.y * 2.0;

    let px = cx + corner.x * size / aspect;
    let py = cy + corner.y * size;

    var out: VertexOutput;
    out.position = vec4f(px, py, 0.0, 1.0);
    out.uv = corner;  // -1..1 range for SDF
    out.alpha = 1.0;
    return out;
}

@fragment
fn fs_point(input: VertexOutput) -> @location(0) vec4f {
    let d = length(input.uv);
    // Circle SDF with soft edge + glow
    let circle = 1.0 - smoothstep(0.5, 0.9, d);
    let glow = 0.3 * exp(-d * d * 4.0);
    let a = clamp(circle + glow, 0.0, 1.0);
    // Premultiplied alpha
    return vec4f(vec3f(uniforms.color_r, uniforms.color_g, uniforms.color_b) * a, a);
}

// --- Line pipeline ---

@vertex
fn vs_line(@builtin(vertex_index) vi: u32,
           @builtin(instance_index) ii: u32) -> VertexOutput {
    let line = lines[ii];
    let aspect = uniforms.resolution.x / uniforms.resolution.y;

    // Endpoints in NDC
    let a_ndc = vec2f(line.a.x * 2.0 - 1.0, 1.0 - line.a.y * 2.0);
    let b_ndc = vec2f(line.b.x * 2.0 - 1.0, 1.0 - line.b.y * 2.0);

    // Direction and normal (in screen space, correcting for aspect)
    let dir_screen = vec2f((b_ndc.x - a_ndc.x) * aspect, b_ndc.y - a_ndc.y);
    let len = length(dir_screen);
    var normal: vec2f;
    if (len > 0.0001) {
        let n = vec2f(-dir_screen.y, dir_screen.x) / len;
        normal = vec2f(n.x / aspect, n.y);
    } else {
        normal = vec2f(0.0, 0.0);
    }

    let half_thick = uniforms.line_thickness;

    // 6 vertices: oriented quad from a to b, extruded by normal
    var pos: vec2f;
    var local_v: f32;  // -1 to 1 across the width
    switch vi {
        case 0u: { pos = a_ndc - normal * half_thick; local_v = -1.0; }
        case 1u: { pos = a_ndc + normal * half_thick; local_v =  1.0; }
        case 2u: { pos = b_ndc - normal * half_thick; local_v = -1.0; }
        case 3u: { pos = b_ndc - normal * half_thick; local_v = -1.0; }
        case 4u: { pos = a_ndc + normal * half_thick; local_v =  1.0; }
        default: { pos = b_ndc + normal * half_thick; local_v =  1.0; }
    }

    var out: VertexOutput;
    out.position = vec4f(pos, 0.0, 1.0);
    out.uv = vec2f(0.0, local_v);
    out.alpha = line.alpha;
    return out;
}

@fragment
fn fs_line(input: VertexOutput) -> @location(0) vec4f {
    // Soft edges across the line width
    let d = abs(input.uv.y);
    let edge = 1.0 - smoothstep(0.4, 1.0, d);
    let a = edge * input.alpha;
    // Premultiplied alpha
    return vec4f(vec3f(uniforms.color_r, uniforms.color_g, uniforms.color_b) * a, a);
}
)";

// =============================================================================
// CPU-side data structures
// =============================================================================

struct Particle {
    float x, y, vx, vy;
};

struct PointInstanceData {
    float x, y;
    float size_factor;
    float _pad;
};

struct LineInstanceData {
    float ax, ay;
    float bx, by;
    float alpha;
    float _pad[3];
};

struct PlexusUniforms {
    float resolution[2];
    float point_size;
    float line_thickness;
    float color_r;
    float color_g;
    float color_b;
    float _pad;
};

// =============================================================================
// Plexus Operator
// =============================================================================

struct Plexus : vivid::OperatorBase {
    static constexpr const char* kName   = "Plexus";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   particle_count     {"particle_count",     80, 8, 256};
    vivid::Param<float> speed              {"speed",              0.3f, 0.0f, 2.0f};
    vivid::Param<float> connection_distance{"connection_distance", 0.25f, 0.01f, 1.0f};
    vivid::Param<float> point_size         {"point_size",         0.008f, 0.001f, 0.05f};
    vivid::Param<float> line_thickness     {"line_thickness",     0.002f, 0.0005f, 0.01f};
    vivid::Param<float> color_r            {"color_r",            0.4f, 0.0f, 1.0f};
    vivid::Param<float> color_g            {"color_g",            0.7f, 0.0f, 1.0f};
    vivid::Param<float> color_b            {"color_b",            1.0f, 0.0f, 1.0f};
    vivid::Param<int>   boundary           {"boundary",           0, {"wrap", "bounce"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&particle_count);
        out.push_back(&speed);
        out.push_back(&connection_distance);
        out.push_back(&point_size);
        out.push_back(&line_thickness);
        vivid::display_hint(color_r, VIVID_DISPLAY_COLOR);
        out.push_back(&color_r);
        out.push_back(&color_g);
        out.push_back(&color_b);
        out.push_back(&boundary);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"reactivity",  VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"texture",     VIVID_PORT_GPU_TEXTURE,    VIVID_PORT_OUTPUT});
        out.push_back({"positions",   VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});
        out.push_back({"connections", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!line_pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[plexus] lazy_init FAILED\n");
                return;
            }
        }

        int count = particle_count.int_value();
        if (count < 1) count = 1;

        // Resize particle array if count changed
        if (static_cast<int>(particles_.size()) != count) {
            resize_particles(count);
        }

        // Read optional reactivity spread (input port 0)
        const float* reactivity_data = nullptr;
        uint32_t reactivity_len = 0;
        if (ctx->input_spreads && ctx->input_spreads[0].length > 0) {
            reactivity_len = ctx->input_spreads[0].length;
            reactivity_data = ctx->input_spreads[0].data;
        }

        // --- CPU simulation ---
        float dt = static_cast<float>(ctx->delta_time);
        dt = std::min(dt, 1.0f / 30.0f);  // clamp to avoid explosion on first frame

        float spd = speed.value;
        int boundary_mode = boundary.int_value();

        for (int i = 0; i < count; i++) {
            float react = 1.0f;
            if (reactivity_data && reactivity_len > 0) {
                react = reactivity_data[i % reactivity_len];
            }

            particles_[i].x += particles_[i].vx * spd * react * dt;
            particles_[i].y += particles_[i].vy * spd * react * dt;

            if (boundary_mode == 0) {
                // Wrap
                if (particles_[i].x < 0.0f) particles_[i].x += 1.0f;
                if (particles_[i].x > 1.0f) particles_[i].x -= 1.0f;
                if (particles_[i].y < 0.0f) particles_[i].y += 1.0f;
                if (particles_[i].y > 1.0f) particles_[i].y -= 1.0f;
            } else {
                // Bounce
                if (particles_[i].x < 0.0f) { particles_[i].x = -particles_[i].x; particles_[i].vx = -particles_[i].vx; }
                if (particles_[i].x > 1.0f) { particles_[i].x = 2.0f - particles_[i].x; particles_[i].vx = -particles_[i].vx; }
                if (particles_[i].y < 0.0f) { particles_[i].y = -particles_[i].y; particles_[i].vy = -particles_[i].vy; }
                if (particles_[i].y > 1.0f) { particles_[i].y = 2.0f - particles_[i].y; particles_[i].vy = -particles_[i].vy; }
            }
        }

        // --- Build point instance data ---
        std::vector<PointInstanceData> point_data(count);
        for (int i = 0; i < count; i++) {
            point_data[i].x = particles_[i].x;
            point_data[i].y = particles_[i].y;
            float react = 1.0f;
            if (reactivity_data && reactivity_len > 0)
                react = reactivity_data[i % reactivity_len];
            point_data[i].size_factor = 0.5f + 0.5f * react;
            point_data[i]._pad = 0.0f;
        }

        // --- Build line instance data (O(N^2) pair check) ---
        float max_dist = connection_distance.value;
        float max_dist_sq = max_dist * max_dist;

        std::vector<LineInstanceData> line_data;
        line_data.reserve(count * (count - 1) / 2);

        // Track per-particle connection counts for audio output
        std::vector<int> conn_counts(count, 0);

        for (int i = 0; i < count; i++) {
            for (int j = i + 1; j < count; j++) {
                float dx = particles_[j].x - particles_[i].x;
                float dy = particles_[j].y - particles_[i].y;
                float dist_sq = dx * dx + dy * dy;
                if (dist_sq < max_dist_sq) {
                    float dist = std::sqrt(dist_sq);
                    float alpha = 1.0f - dist / max_dist;
                    alpha *= alpha;  // quadratic falloff for smoother fade
                    LineInstanceData ld{};
                    ld.ax = particles_[i].x;
                    ld.ay = particles_[i].y;
                    ld.bx = particles_[j].x;
                    ld.by = particles_[j].y;
                    ld.alpha = alpha;
                    line_data.push_back(ld);
                    conn_counts[i]++;
                    conn_counts[j]++;
                }
            }
        }

        uint32_t line_count = static_cast<uint32_t>(line_data.size());

        // --- Write output spreads: positions and connection counts ---
        if (ctx->output_spreads) {
            // positions spread (output port 1): [x0, y0, x1, y1, ...]
            auto& pos_out = ctx->output_spreads[1];
            uint32_t pos_len = static_cast<uint32_t>(count) * 2;
            if (pos_len > pos_out.capacity) pos_len = pos_out.capacity;
            for (uint32_t i = 0; i < pos_len / 2; ++i) {
                pos_out.data[i * 2]     = particles_[i].x;
                pos_out.data[i * 2 + 1] = particles_[i].y;
            }
            pos_out.length = pos_len;

            // Find max connections for normalization
            int max_conn = 1;
            for (int i = 0; i < count; i++) {
                if (conn_counts[i] > max_conn) max_conn = conn_counts[i];
            }

            // connections spread (output port 2): normalized 0–1
            auto& conn_out = ctx->output_spreads[2];
            uint32_t conn_len = static_cast<uint32_t>(count);
            if (conn_len > conn_out.capacity) conn_len = conn_out.capacity;
            float inv_max = 1.0f / static_cast<float>(max_conn);
            for (uint32_t i = 0; i < conn_len; ++i) {
                conn_out.data[i] = static_cast<float>(conn_counts[i]) * inv_max;
            }
            conn_out.length = conn_len;
        }

        // --- Rebuild storage buffers if point count changed ---
        if (static_cast<uint32_t>(count) != current_point_count_) {
            rebuild_storage(gpu, static_cast<uint32_t>(count));
        }

        // --- Rebuild line buffer if needed (grow only) ---
        uint32_t needed_line_bytes = line_count * sizeof(LineInstanceData);
        if (needed_line_bytes < 16) needed_line_bytes = 16;
        if (needed_line_bytes > current_line_buf_size_) {
            // Grow to max possible for current count to avoid frequent rebuilds
            uint32_t max_lines = static_cast<uint32_t>(count) * (static_cast<uint32_t>(count) - 1) / 2;
            uint32_t max_bytes = max_lines * sizeof(LineInstanceData);
            if (max_bytes < 16) max_bytes = 16;
            rebuild_line_buffer(gpu, max_bytes);
        }

        // --- Upload data ---
        if (point_buf_) {
            wgpuQueueWriteBuffer(gpu->queue, point_buf_, 0,
                                 point_data.data(),
                                 static_cast<uint32_t>(count) * sizeof(PointInstanceData));
        }
        if (line_buf_ && line_count > 0) {
            wgpuQueueWriteBuffer(gpu->queue, line_buf_, 0,
                                 line_data.data(),
                                 line_count * sizeof(LineInstanceData));
        }

        // --- Update uniforms ---
        PlexusUniforms u{};
        u.resolution[0] = static_cast<float>(gpu->output_width);
        u.resolution[1] = static_cast<float>(gpu->output_height);
        u.point_size = point_size.value;
        u.line_thickness = line_thickness.value;
        u.color_r = color_r.value;
        u.color_g = color_g.value;
        u.color_b = color_b.value;

        wgpuQueueWriteBuffer(gpu->queue, uniform_buf_, 0, &u, sizeof(u));

        // --- Render pass: lines first (clear), then points (load) ---
        if (!gpu->output_texture_view) return;

        WGPURenderPassColorAttachment color_att{};
        color_att.view = gpu->output_texture_view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.resolveTarget = nullptr;
        color_att.loadOp  = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = { 0.0, 0.0, 0.0, 1.0 };

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid_sv("Plexus Pass");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            gpu->command_encoder, &rp_desc);

        // Draw lines
        if (line_count > 0 && storage_bind_group_) {
            wgpuRenderPassEncoderSetPipeline(pass, line_pipeline_);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, uniform_bind_group_, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, storage_bind_group_, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 6, line_count, 0, 0);
        }

        // Draw points (on top)
        if (storage_bind_group_) {
            wgpuRenderPassEncoderSetPipeline(pass, point_pipeline_);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, uniform_bind_group_, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, storage_bind_group_, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 6, static_cast<uint32_t>(count), 0, 0);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    ~Plexus() override {
        vivid::gpu::release(line_pipeline_);
        vivid::gpu::release(point_pipeline_);
        vivid::gpu::release(uniform_bind_group_);
        vivid::gpu::release(storage_bind_group_);
        vivid::gpu::release(bind_layout0_);
        vivid::gpu::release(bind_layout1_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(point_buf_);
        vivid::gpu::release(line_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
    }

private:
    // GPU resources
    WGPURenderPipeline  line_pipeline_      = nullptr;
    WGPURenderPipeline  point_pipeline_     = nullptr;
    WGPUBindGroup       uniform_bind_group_ = nullptr;
    WGPUBindGroup       storage_bind_group_ = nullptr;
    WGPUBindGroupLayout bind_layout0_       = nullptr;
    WGPUBindGroupLayout bind_layout1_       = nullptr;
    WGPUBuffer          uniform_buf_        = nullptr;
    WGPUBuffer          point_buf_          = nullptr;
    WGPUBuffer          line_buf_           = nullptr;
    WGPUShaderModule    shader_             = nullptr;
    WGPUPipelineLayout  pipe_layout_        = nullptr;

    // CPU state
    std::vector<Particle> particles_;
    uint32_t current_point_count_ = 0;
    uint32_t current_line_buf_size_ = 0;

    // Deterministic hash for initial positions/velocities
    static float hash_float(float seed) {
        float x = std::fmod(seed * 0.1031f, 1.0f);
        if (x < 0) x += 1.0f;
        x += x * (x + 33.33f);
        return std::fmod(x * (x + x), 1.0f);
    }

    void resize_particles(int count) {
        int old_count = static_cast<int>(particles_.size());
        particles_.resize(count);
        // Initialize any new particles with deterministic hash
        for (int i = old_count; i < count; i++) {
            particles_[i].x  = hash_float(static_cast<float>(i) * 127.1f);
            particles_[i].y  = hash_float(static_cast<float>(i) * 311.7f);
            particles_[i].vx = hash_float(static_cast<float>(i) * 269.5f) * 2.0f - 1.0f;
            particles_[i].vy = hash_float(static_cast<float>(i) * 183.3f) * 2.0f - 1.0f;
            // Normalize velocity so all particles move at similar speeds
            float len = std::sqrt(particles_[i].vx * particles_[i].vx +
                                  particles_[i].vy * particles_[i].vy);
            if (len > 0.001f) {
                particles_[i].vx /= len;
                particles_[i].vy /= len;
            }
        }
    }

    void rebuild_storage(VividGpuState* gpu, uint32_t count) {
        vivid::gpu::release(point_buf_);
        vivid::gpu::release(storage_bind_group_);
        current_point_count_ = count;

        uint32_t point_size = count * sizeof(PointInstanceData);
        if (point_size < 16) point_size = 16;

        WGPUBufferDescriptor buf_desc{};
        buf_desc.label = vivid_sv("Plexus Points");
        buf_desc.size  = point_size;
        buf_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        point_buf_ = wgpuDeviceCreateBuffer(gpu->device, &buf_desc);

        // Ensure line buffer exists
        if (!line_buf_) {
            uint32_t max_lines = count * (count - 1) / 2;
            uint32_t line_bytes = max_lines * sizeof(LineInstanceData);
            if (line_bytes < 16) line_bytes = 16;
            rebuild_line_buffer(gpu, line_bytes);
            return;  // rebuild_line_buffer creates the bind group
        }

        recreate_storage_bind_group(gpu);
    }

    void rebuild_line_buffer(VividGpuState* gpu, uint32_t size_bytes) {
        vivid::gpu::release(line_buf_);
        vivid::gpu::release(storage_bind_group_);
        current_line_buf_size_ = size_bytes;

        WGPUBufferDescriptor buf_desc{};
        buf_desc.label = vivid_sv("Plexus Lines");
        buf_desc.size  = size_bytes;
        buf_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        line_buf_ = wgpuDeviceCreateBuffer(gpu->device, &buf_desc);

        recreate_storage_bind_group(gpu);
    }

    void recreate_storage_bind_group(VividGpuState* gpu) {
        vivid::gpu::release(storage_bind_group_);
        if (!point_buf_ || !line_buf_) return;

        WGPUBindGroupEntry entries[2]{};
        entries[0].binding = 0;
        entries[0].buffer  = point_buf_;
        entries[0].offset  = 0;
        entries[0].size    = wgpuBufferGetSize(point_buf_);
        entries[1].binding = 1;
        entries[1].buffer  = line_buf_;
        entries[1].offset  = 0;
        entries[1].size    = wgpuBufferGetSize(line_buf_);

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label      = vivid_sv("Plexus Storage BG");
        bg_desc.layout     = bind_layout1_;
        bg_desc.entryCount = 2;
        bg_desc.entries    = entries;
        storage_bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);
    }

    bool lazy_init(VividGpuState* gpu) {
        // Build shader module manually (no fullscreen vertex preamble)
        std::string wgsl = std::string(vivid::gpu::WGSL_CONSTANTS) + kPlexusShader;

        WGPUShaderSourceWGSL wgsl_src{};
        wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl_src.code = vivid_sv(wgsl.c_str());

        WGPUShaderModuleDescriptor sm_desc{};
        sm_desc.nextInChain = &wgsl_src.chain;
        sm_desc.label = vivid_sv("Plexus Shader");
        shader_ = wgpuDeviceCreateShaderModule(gpu->device, &sm_desc);
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(PlexusUniforms), "Plexus Uniforms");

        // --- Bind group layout 0: uniforms ---
        WGPUBindGroupLayoutEntry bg0_entry{};
        bg0_entry.binding = 0;
        bg0_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bg0_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bg0_entry.buffer.minBindingSize = sizeof(PlexusUniforms);

        WGPUBindGroupLayoutDescriptor bg0_layout_desc{};
        bg0_layout_desc.label = vivid_sv("Plexus BGL0");
        bg0_layout_desc.entryCount = 1;
        bg0_layout_desc.entries = &bg0_entry;
        bind_layout0_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bg0_layout_desc);

        // Create uniform bind group (static — uniform buffer never changes)
        WGPUBindGroupEntry bg0_bind_entry{};
        bg0_bind_entry.binding = 0;
        bg0_bind_entry.buffer  = uniform_buf_;
        bg0_bind_entry.offset  = 0;
        bg0_bind_entry.size    = sizeof(PlexusUniforms);

        WGPUBindGroupDescriptor bg0_desc{};
        bg0_desc.label = vivid_sv("Plexus Uniform BG");
        bg0_desc.layout = bind_layout0_;
        bg0_desc.entryCount = 1;
        bg0_desc.entries = &bg0_bind_entry;
        uniform_bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg0_desc);

        // --- Bind group layout 1: two storage buffers (points + lines) ---
        WGPUBindGroupLayoutEntry bg1_entries[2]{};
        bg1_entries[0].binding = 0;
        bg1_entries[0].visibility = WGPUShaderStage_Vertex;
        bg1_entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        bg1_entries[0].buffer.minBindingSize = 0;

        bg1_entries[1].binding = 1;
        bg1_entries[1].visibility = WGPUShaderStage_Vertex;
        bg1_entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        bg1_entries[1].buffer.minBindingSize = 0;

        WGPUBindGroupLayoutDescriptor bg1_layout_desc{};
        bg1_layout_desc.label = vivid_sv("Plexus BGL1");
        bg1_layout_desc.entryCount = 2;
        bg1_layout_desc.entries = bg1_entries;
        bind_layout1_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bg1_layout_desc);

        // --- Pipeline layout ---
        WGPUBindGroupLayout layouts[2] = { bind_layout0_, bind_layout1_ };
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Plexus Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 2;
        pl_desc.bindGroupLayouts = layouts;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // --- Alpha blending (premultiplied) ---
        WGPUBlendState blend{};
        blend.color.srcFactor = WGPUBlendFactor_One;
        blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.alpha.operation = WGPUBlendOperation_Add;

        WGPUColorTargetState color_target{};
        color_target.format = gpu->output_format;
        color_target.blend = &blend;
        color_target.writeMask = WGPUColorWriteMask_All;

        // --- Line pipeline ---
        {
            WGPUFragmentState fragment{};
            fragment.module = shader_;
            fragment.entryPoint = vivid_sv("fs_line");
            fragment.targetCount = 1;
            fragment.targets = &color_target;

            WGPURenderPipelineDescriptor rp_desc{};
            rp_desc.label = vivid_sv("Plexus Line Pipeline");
            rp_desc.layout = pipe_layout_;
            rp_desc.vertex.module = shader_;
            rp_desc.vertex.entryPoint = vivid_sv("vs_line");
            rp_desc.vertex.bufferCount = 0;
            rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
            rp_desc.primitive.cullMode = WGPUCullMode_None;
            rp_desc.multisample.count = 1;
            rp_desc.multisample.mask = 0xFFFFFFFF;
            rp_desc.fragment = &fragment;

            line_pipeline_ = wgpuDeviceCreateRenderPipeline(gpu->device, &rp_desc);
            if (!line_pipeline_) return false;
        }

        // --- Point pipeline ---
        {
            WGPUFragmentState fragment{};
            fragment.module = shader_;
            fragment.entryPoint = vivid_sv("fs_point");
            fragment.targetCount = 1;
            fragment.targets = &color_target;

            WGPURenderPipelineDescriptor rp_desc{};
            rp_desc.label = vivid_sv("Plexus Point Pipeline");
            rp_desc.layout = pipe_layout_;
            rp_desc.vertex.module = shader_;
            rp_desc.vertex.entryPoint = vivid_sv("vs_point");
            rp_desc.vertex.bufferCount = 0;
            rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
            rp_desc.primitive.cullMode = WGPUCullMode_None;
            rp_desc.multisample.count = 1;
            rp_desc.multisample.mask = 0xFFFFFFFF;
            rp_desc.fragment = &fragment;

            point_pipeline_ = wgpuDeviceCreateRenderPipeline(gpu->device, &rp_desc);
            if (!point_pipeline_) return false;
        }

        // Initial storage buffers (1 point, minimum line buffer)
        rebuild_storage(gpu, 1);

        return true;
    }
};

VIVID_REGISTER(Plexus)
