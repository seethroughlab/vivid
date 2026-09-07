// Core visual package operator: Emitter — a one-shot PARTICLE burst per FIRE of an incoming signal.
// It reads the signal's `fired` stream (discrete this-frame events) on its input edge: each fire spawns
// a short-lived burst of glowing particles (pos -> emit x + hue, amp -> energy) that fly out, fall under
// gravity and fade. Agnostic to the source — a note-on fires it today, a beat pulse or an audio onset
// could tomorrow, unchanged (it never refers to "notes"). Because it consumes FIRES, not membership, a
// re-struck note re-bursts (a held-set could never express that). Additive over black, like Instancer.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/element_geom.h"   // VividSignal, input_signal

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
struct QVert { float x, y; };                       // unit quad corner in [-1,1]
struct Inst  { float px, py, scale, r, g, b; };     // per-instance: center (NDC), radius, colour
// Same glowing-disc instanced pipeline as Instancer.
const char* kWGSL = R"(
struct U { res: vec2f, time: f32, pad: f32 };
@group(0) @binding(0) var<uniform> u: U;
struct VIn { @location(0) corner: vec2f, @location(1) pos: vec2f, @location(2) scale: f32, @location(3) col: vec3f };
struct VOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f, @location(1) col: vec3f };
@vertex fn vs_main(v: VIn) -> VOut {
    var o: VOut;
    var p = v.corner * v.scale;
    p.x = p.x * (u.res.y / max(u.res.x, 1.0));   // aspect-correct: round particles stay round
    o.pos = vec4f(v.pos + p, 0.0, 1.0);
    o.uv = v.corner;
    o.col = v.col;
    return o;
}
@fragment fn fs_main(i: VOut) -> @location(0) vec4f {
    let d = length(i.uv);
    let a = smoothstep(1.0, 0.1, d);            // soft glowing point
    return vec4f(i.col * a, a);
}
)";
}  // namespace

struct EmitterOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Emitter";
    // ADR-0046: bundles particle lifecycle + layout + colour + rendering in one node — a RECIPE.
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_RECIPE;
    static constexpr const char* kDisplayName = "Emitter";
    static constexpr const char* kSummary = "A one-shot particle burst per fire of an incoming signal (pos->hue, amp->energy). Re-struck notes re-burst; drives off notes, beats or onsets.";
    static constexpr std::array<const char*, 3> kKeywords = {"particles", "burst", "emitter"};
    vivid::Param<float> count{"count", 0.4f, 0.f, 1.f};    // particles per burst
    vivid::Param<float> speed{"speed", 0.5f, 0.f, 1.f};    // launch speed
    vivid::Param<float> gravity{"gravity", 0.4f, 0.f, 1.f};// downward pull
    vivid::Param<float> life{"life", 0.5f, 0.f, 1.f};      // particle lifetime
    vivid::Param<float> size{"size", 0.4f, 0.f, 1.f};      // particle radius
    vivid::Param<float> spread{"spread", 0.8f, 0.f, 1.f};  // horizontal emit spread by pos

    bool tried_ = false; std::string err_;   // ADR-0019: surfaced per-frame via report_if_no_pipeline
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer quad_ = nullptr;
    WGPUBuffer inst_ = nullptr; uint32_t inst_cap_ = 0;
    // Op-local particle pool: born on a fire, integrated + faded each frame, recycled when dead.
    struct Particle { float x, y, vx, vy, age, life, r, g, b; };
    std::vector<Particle> parts_;
    std::vector<Inst> insts_;
    uint32_t rng_ = 0x9e3779b9u;   // deterministic xorshift for spray directions (visual, not audio)

    float rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ & 0xffffff) / float(0x1000000); }

    ~EmitterOp() override {
        if (inst_) wgpuBufferRelease(inst_); if (quad_) wgpuBufferRelease(quad_);
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&count); o.push_back(&speed); o.push_back(&gravity);
        o.push_back(&life); o.push_back(&size); o.push_back(&spread);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(VIVID_CUSTOM_REF_PORT("signal", VIVID_PORT_INPUT, VividSignal));   // driven by a Notes node (or any signal)
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }

    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kWGSL, "Emitter", err);
        if (!sh_ || !err.empty()) { err_ = "Emitter WGSL: " + vivid::gpu::concise_gpu_error(err); return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 16, "Emitter U");
        const QVert quad[6] = { {-1,-1},{1,-1},{1,1}, {-1,-1},{1,1},{-1,1} };
        WGPUBufferDescriptor qd{}; qd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; qd.size = sizeof(quad);
        quad_ = wgpuDeviceCreateBuffer(c->device, &qd);
        wgpuQueueWriteBuffer(c->queue, quad_, 0, quad, sizeof(quad));
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 16;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        WGPUVertexAttribute qa[1]{}; qa[0].format = WGPUVertexFormat_Float32x2; qa[0].offset = 0; qa[0].shaderLocation = 0;
        WGPUVertexAttribute ia[3]{};
        ia[0].format = WGPUVertexFormat_Float32x2; ia[0].offset = offsetof(Inst, px);    ia[0].shaderLocation = 1;
        ia[1].format = WGPUVertexFormat_Float32;   ia[1].offset = offsetof(Inst, scale); ia[1].shaderLocation = 2;
        ia[2].format = WGPUVertexFormat_Float32x3; ia[2].offset = offsetof(Inst, r);     ia[2].shaderLocation = 3;
        WGPUVertexBufferLayout vbl[2]{};
        vbl[0].arrayStride = sizeof(QVert); vbl[0].stepMode = WGPUVertexStepMode_Vertex;   vbl[0].attributeCount = 1; vbl[0].attributes = qa;
        vbl[1].arrayStride = sizeof(Inst);  vbl[1].stepMode = WGPUVertexStepMode_Instance; vbl[1].attributeCount = 3; vbl[1].attributes = ia;
        WGPUBlendState blend{};
        blend.color.srcFactor = WGPUBlendFactor_One; blend.color.dstFactor = WGPUBlendFactor_One; blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One; blend.alpha.dstFactor = WGPUBlendFactor_One; blend.alpha.operation = WGPUBlendOperation_Add;
        WGPUColorTargetState ct{}; ct.format = c->output_format; ct.blend = &blend; ct.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs{}; fs.module = sh_; fs.entryPoint = vivid_sv("fs_main"); fs.targetCount = 1; fs.targets = &ct;
        WGPURenderPipelineDescriptor rp{}; rp.layout = pl_;
        rp.vertex.module = sh_; rp.vertex.entryPoint = vivid_sv("vs_main");
        rp.vertex.bufferCount = 2; rp.vertex.buffers = vbl;
        rp.primitive.topology = WGPUPrimitiveTopology_TriangleList; rp.primitive.frontFace = WGPUFrontFace_CCW;
        rp.primitive.cullMode = WGPUCullMode_None;
        rp.multisample.count = 1; rp.multisample.mask = 0xFFFFFFFF;
        rp.fragment = &fs;
        pipe_ = wgpuDeviceCreateRenderPipeline(c->device, &rp);
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 16;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    static void pos_colour(float h, float& r, float& g, float& b) {   // low=blue → mid=green → high=red
        r = std::clamp(1.6f * h - 0.3f, 0.f, 1.f);
        g = std::clamp(1.f - std::fabs(h - 0.5f) * 1.8f, 0.f, 1.f);
        b = std::clamp(1.6f * (1.f - h) - 0.3f, 0.f, 1.f);
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (vivid::gpu::report_if_no_pipeline(c, pipe_, err_)) return;
        const float* p = c->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        const int   nper  = 6 + static_cast<int>(54.f * pv(0, count.value));   // particles per burst
        const float spd   = 0.3f + 1.4f * pv(1, speed.value);
        const float grav  = 2.2f * pv(2, gravity.value);
        const float lifeS = 0.25f + 1.75f * pv(3, life.value);
        const float base  = 0.015f + 0.09f * pv(4, size.value);
        const float spr   = pv(5, spread.value);
        const float dt    = static_cast<float>(c->delta_time);

        // --- spawn on each FIRE of the incoming signal (the driving edge) ---
        const VividSignal* sig = vivid::elements::input_signal(c, 0);
        if (sig && sig->fired) {
            for (uint32_t i = 0; i < sig->fired_count; ++i) {
                const VividElement& e = sig->fired[i];
                const float h = std::clamp(e.pos, 0.f, 1.f);
                const float ex = (h - 0.5f) * 2.f * spr;
                const float ey = -0.15f;   // emit from a low band; particles fly up + out
                float r, g, b; pos_colour(h, r, g, b);
                const float bright = 0.4f + 0.6f * e.amp;
                const int n = 4 + static_cast<int>((nper - 4) * (0.4f + 0.6f * e.amp));
                for (int k = 0; k < n && parts_.size() < 4096; ++k) {
                    const float ang = 6.2831853f * rnd();
                    const float mag = spd * (0.3f + 0.7f * rnd()) * (0.5f + e.amp);
                    parts_.push_back({ ex, ey, std::cos(ang) * mag, std::sin(ang) * mag + spd * 0.5f,
                                       0.f, lifeS * (0.6f + 0.5f * rnd()),
                                       r * bright, g * bright, b * bright });
                }
            }
        }

        // --- integrate + fade, cull dead ---
        for (auto& q : parts_) { q.vy -= grav * dt; q.x += q.vx * dt; q.y += q.vy * dt; q.age += dt; }
        parts_.erase(std::remove_if(parts_.begin(), parts_.end(),
                     [](const Particle& q){ return q.age >= q.life; }), parts_.end());

        insts_.clear();
        for (const auto& q : parts_) {
            const float a = std::clamp(1.f - q.age / q.life, 0.f, 1.f);
            insts_.push_back({ q.x, q.y, base * (0.5f + 0.5f * a), q.r * a, q.g * a, q.b * a });
        }

        float u[4] = { float(c->output_width), float(c->output_height), float(c->time), 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        const uint32_t bytes = static_cast<uint32_t>(insts_.size() * sizeof(Inst));
        if (bytes > inst_cap_) {
            if (inst_) wgpuBufferRelease(inst_);
            WGPUBufferDescriptor bd{}; bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = bytes;
            inst_ = wgpuDeviceCreateBuffer(c->device, &bd); inst_cap_ = bytes;
        }
        if (inst_ && bytes) wgpuQueueWriteBuffer(c->queue, inst_, 0, insts_.data(), bytes);

        WGPURenderPassColorAttachment att{};
        att.view = c->output_texture_view; att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        att.loadOp = WGPULoadOp_Clear; att.storeOp = WGPUStoreOp_Store;
        att.clearValue = { 0.0, 0.0, 0.0, 1.0 };
        WGPURenderPassDescriptor rpd{}; rpd.colorAttachmentCount = 1; rpd.colorAttachments = &att;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(c->command_encoder, &rpd);
        wgpuRenderPassEncoderSetPipeline(pass, pipe_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg_, 0, nullptr);
        if (inst_ && !insts_.empty()) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quad_, 0, sizeof(QVert) * 6);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 1, inst_, 0, bytes);
            wgpuRenderPassEncoderDraw(pass, 6, static_cast<uint32_t>(insts_.size()), 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
};

VIVID_REGISTER(EmitterOp)
