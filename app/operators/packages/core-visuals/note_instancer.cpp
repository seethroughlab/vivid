// Core visual package operator: NoteInstancer — draws one glowing instance per LIVE MIDI note of a
// track (the polyphonic note→visual payoff). Reads the active-notes bus (operator_api/note_bus.h),
// which the engine fills each frame from the track's currently-held notes. Pitch → x position + hue,
// velocity → size + brightness; a note-on spawns an instance, a note-off fades it out (arps trail,
// chords bloom). Vivid's first TRUE GPU-instanced op: one static unit-quad + a per-instance buffer,
// drawn with a single instanced draw. Additive blend over black so it composites (ADD) cleanly.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/note_bus.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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
const char* kWGSL = R"(
struct U { res: vec2f, time: f32, pad: f32 };
@group(0) @binding(0) var<uniform> u: U;
struct VIn { @location(0) corner: vec2f, @location(1) pos: vec2f, @location(2) scale: f32, @location(3) col: vec3f };
struct VOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f, @location(1) col: vec3f };
@vertex fn vs_main(v: VIn) -> VOut {
    var o: VOut;
    var p = v.corner * v.scale;
    p.x = p.x * (u.res.y / max(u.res.x, 1.0));   // aspect-correct: round dots stay round
    o.pos = vec4f(v.pos + p, 0.0, 1.0);
    o.uv = v.corner;
    o.col = v.col;
    return o;
}
@fragment fn fs_main(i: VOut) -> @location(0) vec4f {
    let d = length(i.uv);
    let a = smoothstep(1.0, 0.15, d);            // soft glowing disc
    return vec4f(i.col * a, a);
}
)";
}  // namespace

struct NoteInstancerOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "NoteInstancer";
    static constexpr const char* kDisplayName = "Note Instancer";
    static constexpr const char* kSummary = "One glowing instance per live MIDI note of a track (pitch->colour, velocity->size); chords bloom, arps trail.";
    static constexpr std::array<const char*, 3> kKeywords = {"notes", "instancer", "geometry"};
    vivid::Param<float> track{"track", 0.f, 0.f, 31.f};      // which track's held notes (index)
    vivid::Param<float> size{"size", 0.5f, 0.f, 1.f};        // base dot radius
    vivid::Param<float> spread{"spread", 0.8f, 0.f, 1.f};    // horizontal pitch spread
    vivid::Param<float> trail{"trail", 0.35f, 0.f, 1.f};     // note-off fade time

    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer quad_ = nullptr;                              // static unit quad (6 verts)
    WGPUBuffer inst_ = nullptr; uint32_t inst_cap_ = 0;      // per-instance buffer (grows)
    // Op-local aging set: a live persists while the note is held (age reset each frame it's in the bus)
    // and fades over `trail` seconds after release. Keyed by pitch (a held note owns its pitch).
    struct Live { int pitch; float vel; float age; };
    std::vector<Live> lives_;

    ~NoteInstancerOp() override {
        if (inst_) wgpuBufferRelease(inst_); if (quad_) wgpuBufferRelease(quad_);
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&track); o.push_back(&size); o.push_back(&spread); o.push_back(&trail);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }

    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kWGSL, "NoteInstancer", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 16, "NoteInstancer U");
        // static unit quad
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
        // two vertex buffers: [0] the unit quad (per-vertex), [1] the per-instance records
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
    static void pitch_colour(float h, float& r, float& g, float& b) {   // low=blue → mid=green → high=red
        r = std::clamp(1.6f * h - 0.3f, 0.f, 1.f);
        g = std::clamp(1.f - std::fabs(h - 0.5f) * 1.8f, 0.f, 1.f);
        b = std::clamp(1.6f * (1.f - h) - 0.3f, 0.f, 1.f);
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        const int trk = std::clamp(static_cast<int>(std::lround(pv(0, 0.f))), 0, VIVID_NOTE_BUS_TRACKS - 1);
        const float base = 0.02f + 0.22f * pv(1, size.value);
        const float spr  = pv(2, spread.value);
        const float maxAge = 0.04f + 1.4f * pv(3, trail.value);   // fade seconds
        const float dt = static_cast<float>(c->delta_time);

        // --- update the aging set from the bus ---
        VividActiveNote bus[VIVID_MAX_ACTIVE_NOTES];
        const uint32_t nb = vivid_track_active_notes(trk, bus, VIVID_MAX_ACTIVE_NOTES);
        for (auto& L : lives_) L.age += dt;
        for (uint32_t i = 0; i < nb; ++i) {
            auto it = std::find_if(lives_.begin(), lives_.end(), [&](const Live& L){ return L.pitch == bus[i].pitch; });
            if (it != lives_.end()) { it->age = 0.f; it->vel = bus[i].velocity; }
            else if (lives_.size() < 64) lives_.push_back({ bus[i].pitch, bus[i].velocity, 0.f });
        }
        lives_.erase(std::remove_if(lives_.begin(), lives_.end(), [&](const Live& L){ return L.age > maxAge; }), lives_.end());

        // --- build the instance records ---
        std::vector<Inst> insts; insts.reserve(lives_.size());
        for (const auto& L : lives_) {
            const float alpha = std::clamp(1.f - L.age / maxAge, 0.f, 1.f);
            const float h = std::clamp((L.pitch - 24) / 84.f, 0.f, 1.f);   // ~C1..C7 across the range
            const float x = (h - 0.5f) * 2.f * spr;
            const float y = 0.14f * std::sin(static_cast<float>(c->time) * 1.7f + L.pitch * 0.5f);   // gentle float
            const float sc = base * (0.45f + L.vel) * (0.5f + 0.5f * alpha);
            float r, g, b; pitch_colour(h, r, g, b);
            const float bright = (0.35f + 0.65f * L.vel) * alpha;
            insts.push_back({ x, y, sc, r * bright, g * bright, b * bright });
        }

        float u[4] = { float(c->output_width), float(c->output_height), float(c->time), 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        const uint32_t bytes = static_cast<uint32_t>(insts.size() * sizeof(Inst));
        if (bytes > inst_cap_) {
            if (inst_) wgpuBufferRelease(inst_);
            WGPUBufferDescriptor bd{}; bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = bytes;
            inst_ = wgpuDeviceCreateBuffer(c->device, &bd); inst_cap_ = bytes;
        }
        if (inst_ && bytes) wgpuQueueWriteBuffer(c->queue, inst_, 0, insts.data(), bytes);

        WGPURenderPassColorAttachment att{};
        att.view = c->output_texture_view; att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        att.loadOp = WGPULoadOp_Clear; att.storeOp = WGPUStoreOp_Store;
        att.clearValue = { 0.0, 0.0, 0.0, 1.0 };
        WGPURenderPassDescriptor rpd{}; rpd.colorAttachmentCount = 1; rpd.colorAttachments = &att;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(c->command_encoder, &rpd);
        wgpuRenderPassEncoderSetPipeline(pass, pipe_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg_, 0, nullptr);
        if (inst_ && !insts.empty()) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quad_, 0, sizeof(QVert) * 6);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 1, inst_, 0, bytes);
            wgpuRenderPassEncoderDraw(pass, 6, static_cast<uint32_t>(insts.size()), 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
};

VIVID_REGISTER(NoteInstancerOp)
