// Core visual package operator: Instancer — draws one glowing instance per element of an incoming
// signal's ACTIVE set (from a Notes node, or any VividSignal producer, on its input edge). pos → x +
// hue, amp → size + brightness; an element appearing spawns an instance, its leaving fades it out
// (chords bloom, arps trail). DRIVEN by the graph edge and agnostic to the source — it never refers to
// "notes", so a Beat or onset producer drives it unchanged. Vivid's first TRUE GPU-instanced op (a
// static unit quad + a per-instance buffer, one instanced draw). Additive over black so it composites (ADD) cleanly.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/element_geom.h"   // VividSignal, input_signal

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
struct U { res: vec2f, time: f32, shape: f32, sides: f32, p0: f32, p1: f32, p2: f32 };
@group(0) @binding(0) var<uniform> u: U;
struct VIn { @location(0) corner: vec2f, @location(1) pos: vec2f, @location(2) scale: f32, @location(3) col: vec3f };
struct VOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f, @location(1) col: vec3f };
@vertex fn vs_main(v: VIn) -> VOut {
    var o: VOut;
    var p = v.corner * v.scale;
    p.x = p.x * (u.res.y / max(u.res.x, 1.0));   // aspect-correct: round shapes stay round
    o.pos = vec4f(v.pos + p, 0.0, 1.0);
    o.uv = v.corner;
    o.col = v.col;
    return o;
}
// shape: 0 dot (soft glow), 1 ring (hollow), 2 polygon (crisp N-gon), 3 bar (crisp block).
@fragment fn fs_main(i: VOut) -> @location(0) vec4f {
    let uv = i.uv;
    let r = length(uv);
    var a = 0.0;
    if (u.shape < 0.5) {
        a = smoothstep(1.0, 0.15, r);                                  // dot: soft glowing disc
    } else if (u.shape < 1.5) {
        a = smoothstep(0.16, 0.0, abs(r - 0.7));                       // ring: thin hollow circle
    } else if (u.shape < 2.5) {
        let n = max(3.0, u.sides);                                     // polygon: crisp regular N-gon
        let ang = atan2(uv.y, uv.x);
        let seg = 6.2831853 / n;
        let d = cos(seg * 0.5) / cos((ang - floor(ang / seg + 0.5) * seg));
        a = smoothstep(1.0, 0.86, r / max(d, 1e-3));
    } else {
        let b = max(abs(uv.x), abs(uv.y) * 2.6);                       // bar: crisp wide block
        a = smoothstep(1.0, 0.88, b);
    }
    return vec4f(i.col * a, a);
}
)";
}  // namespace

struct InstancerOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Instancer";
    // ADR-0046: bundles lifecycle + layout + colour + geometry + rendering in one node — a RECIPE that
    // stands in for Signal -> InstancesFromSignal/Lanes -> Instancer3D. Ranked below primitives.
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_RECIPE;
    static constexpr const char* kDisplayName = "Instancer";
    static constexpr const char* kSummary = "Draws one glowing instance per note of an incoming Notes value (pitch->colour, velocity->size); chords bloom, arps trail.";
    static constexpr std::array<const char*, 3> kKeywords = {"notes", "instancer", "geometry"};
    vivid::Param<float> size{"size", 0.5f, 0.f, 1.f};        // base instance radius
    vivid::Param<float> spread{"spread", 0.8f, 0.f, 1.f};    // horizontal spread by pos
    vivid::Param<float> trail{"trail", 0.35f, 0.f, 1.f};     // fade time after an element leaves
    vivid::Param<float> shape{"shape", 0.f, 0.f, 3.f};       // 0 dot · 1 ring · 2 polygon · 3 bar
    vivid::Param<float> sides{"sides", 6.f, 3.f, 8.f};       // polygon sides (shape=2)
    vivid::Param<float> pulse{"pulse", 0.6f, 0.f, 1.f};      // pop amount on each note-on FIRE (re-strikes re-pop)

    bool tried_ = false; std::string err_;   // ADR-0019: surfaced per-frame via report_if_no_pipeline
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer quad_ = nullptr;                              // static unit quad (6 verts)
    WGPUBuffer inst_ = nullptr; uint32_t inst_cap_ = 0;      // per-instance buffer (grows)
    // Op-local aging set: a live persists while its element is present on the input edge (age reset each
    // frame it's in the set) and fades over `trail` seconds after it leaves. Keyed by element id.
    struct Live { int id; float pos; float amp; float age; float kick; };   // kick = decaying note-on pop
    std::vector<Live> lives_;
    std::vector<Inst> insts_;   // per-frame instance scratch, reused (cleared each frame)

    ~InstancerOp() override {
        if (inst_) wgpuBufferRelease(inst_); if (quad_) wgpuBufferRelease(quad_);
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&size); o.push_back(&spread); o.push_back(&trail); o.push_back(&shape); o.push_back(&sides); o.push_back(&pulse);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(VIVID_CUSTOM_REF_PORT("signal", VIVID_PORT_INPUT, VividSignal));   // driven by a Notes node (or any signal)
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }

    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kWGSL, "Instancer", err);
        if (!sh_ || !err.empty()) { err_ = "Instancer WGSL: " + vivid::gpu::concise_gpu_error(err); return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "Instancer U");
        const QVert quad[6] = { {-1,-1},{1,-1},{1,1}, {-1,-1},{1,1},{-1,1} };
        WGPUBufferDescriptor qd{}; qd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; qd.size = sizeof(quad);
        quad_ = wgpuDeviceCreateBuffer(c->device, &qd);
        wgpuQueueWriteBuffer(c->queue, quad_, 0, quad, sizeof(quad));
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 32;
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
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 32;
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
        if (vivid::gpu::report_if_no_pipeline(c, pipe_, err_)) return;
        const float* p = c->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        const float base = 0.02f + 0.22f * pv(0, size.value);
        const float spr  = pv(1, spread.value);
        const float maxAge = 0.04f + 1.4f * pv(2, trail.value);   // fade seconds
        const float dt = static_cast<float>(c->delta_time);

        // --- update the aging set from the incoming signal's ACTIVE set (the driving edge) ---
        const float kickAmt = pv(5, pulse.value);
        const VividSignal* sig = vivid::elements::input_signal(c, 0);
        for (auto& L : lives_) { L.age += dt; L.kick = std::max(0.f, L.kick - dt * 4.f); }   // ~0.25s pop decay
        if (sig && sig->active) {
            for (uint32_t i = 0; i < sig->active_count; ++i) {
                const VividElement& e = sig->active[i];
                auto it = std::find_if(lives_.begin(), lives_.end(), [&](const Live& L){ return L.id == e.id; });
                if (it != lives_.end()) { it->age = 0.f; it->amp = e.amp; it->pos = e.pos; }
                else if (lives_.size() < 64) lives_.push_back({ e.id, e.pos, e.amp, 0.f, 1.f });   // new note pops
            }
        }
        // A note-on FIRE re-pops the matching element (matched by pos) — so a re-struck HELD pitch,
        // which membership alone can't show, visibly kicks again.
        if (sig && sig->fired) {
            for (uint32_t i = 0; i < sig->fired_count; ++i) {
                const float fp = sig->fired[i].pos;
                auto it = std::find_if(lives_.begin(), lives_.end(), [&](const Live& L){ return std::fabs(L.pos - fp) < 0.006f; });
                if (it != lives_.end()) it->kick = 1.f;
            }
        }
        lives_.erase(std::remove_if(lives_.begin(), lives_.end(), [&](const Live& L){ return L.age > maxAge; }), lives_.end());

        // --- build the instance records (reused member scratch, no per-frame alloc) ---
        insts_.clear();
        for (const auto& L : lives_) {
            const float alpha = std::clamp(1.f - L.age / maxAge, 0.f, 1.f);
            const float h = std::clamp(L.pos, 0.f, 1.f);                    // primary axis → hue + x
            const float x = (h - 0.5f) * 2.f * spr;
            const float y = 0.14f * std::sin(static_cast<float>(c->time) * 1.7f + L.pos * 60.f);   // gentle float
            const float k = L.kick * kickAmt;
            const float sc = base * (0.45f + L.amp) * (0.5f + 0.5f * alpha) * (1.f + 0.8f * k);
            float r, g, b; pitch_colour(h, r, g, b);
            const float bright = (0.35f + 0.65f * L.amp) * alpha * (1.f + 1.6f * k);
            insts_.push_back({ x, y, sc, r * bright, g * bright, b * bright });
        }

        float u[8] = { float(c->output_width), float(c->output_height), float(c->time),
                       std::round(pv(3, shape.value)), pv(4, sides.value), 0.f, 0.f, 0.f };
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

VIVID_REGISTER(InstancerOp)
