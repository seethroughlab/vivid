#include "gpu/effect_op.h"
#include "gpu/gpu_util.h"
#include <cstdio>
#include <cstdint>
#include <algorithm>

namespace vivid {

struct EUniforms { float res[2]; float time; float p[4]; float _pad; };  // 32 bytes (std140)

// Fullscreen triangle emitting a vec2 uv in [0,1]. uv.y is flipped vs clip-space y so the TOP of the
// screen samples the TOP of the source (uv.y=0) — an identity present, matching the canonical texture
// convention (see fullscreenTriangle in operator_api/gpu_common.h). Used only by the final present
// blit and the reduce-motion blend, both of which sample RTs that are now stored top-down.
static const char* kVertGLSL = R"(#version 450
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

static WGPUShaderModule make_glsl(WGPUDevice d, WGPUShaderStage stage,
                                  const char* src, const char* label) {
    WGPUShaderSourceGLSL g{};
    g.chain.sType = static_cast<WGPUSType>(WGPUSType_ShaderSourceGLSL);
    g.stage = stage;
    g.code = to_sv(src);
    g.defineCount = 0;
    g.defines = nullptr;
    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &g.chain;
    desc.label = to_sv(label);
    return wgpuDeviceCreateShaderModule(d, &desc);
}

bool EffectOp::init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat target_format,
                    const char* glsl_fragment, int num_inputs, int sample_count) {
    device_ = device;
    queue_  = queue;
    num_inputs_ = (num_inputs < 1) ? 1 : (num_inputs > 2 ? 2 : num_inputs);

    vert_ = make_glsl(device_, WGPUShaderStage_Vertex, kVertGLSL, "effect.vert");
    frag_ = make_glsl(device_, WGPUShaderStage_Fragment, glsl_fragment, "effect.frag");
    if (!vert_ || !frag_) { std::fprintf(stderr, "[EffectOp] GLSL module creation failed\n"); return false; }

    // bind group layout: 0=uniform, 1=texture0, 2=sampler, [3=texture1]
    WGPUBindGroupLayoutEntry e[4]{};
    e[0].binding = 0; e[0].visibility = WGPUShaderStage_Fragment;
    e[0].buffer.type = WGPUBufferBindingType_Uniform;
    e[0].buffer.minBindingSize = sizeof(EUniforms);
    e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
    e[1].texture.sampleType = WGPUTextureSampleType_Float;
    e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    e[1].texture.multisampled = 0;
    e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment;
    e[2].sampler.type = WGPUSamplerBindingType_Filtering;
    uint32_t entry_count = 3;
    if (num_inputs_ >= 2) {
        e[3].binding = 3; e[3].visibility = WGPUShaderStage_Fragment;
        e[3].texture.sampleType = WGPUTextureSampleType_Float;
        e[3].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[3].texture.multisampled = 0;
        entry_count = 4;
    }
    WGPUBindGroupLayoutDescriptor bgld{};
    bgld.entryCount = entry_count;
    bgld.entries = e;
    bgl_ = wgpuDeviceCreateBindGroupLayout(device_, &bgld);

    WGPUPipelineLayoutDescriptor pld{};
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &bgl_;
    layout_ = wgpuDeviceCreatePipelineLayout(device_, &pld);

    WGPUBufferDescriptor bd{};
    bd.size = sizeof(EUniforms);
    bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    ubo_ = wgpuDeviceCreateBuffer(device_, &bd);

    WGPUSamplerDescriptor sd{};
    sd.addressModeU = WGPUAddressMode_ClampToEdge;
    sd.addressModeV = WGPUAddressMode_ClampToEdge;
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = WGPUFilterMode_Linear;
    sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.lodMinClamp = 0.f; sd.lodMaxClamp = 1.f;
    sd.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device_, &sd);

    WGPUColorTargetState target{};
    target.format = target_format;
    target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs{};
    fs.module = frag_;
    fs.entryPoint = to_sv("main");
    fs.targetCount = 1;
    fs.targets = &target;

    WGPURenderPipelineDescriptor pd{};
    pd.layout = layout_;
    pd.vertex.module = vert_;
    pd.vertex.entryPoint = to_sv("main");
    pd.vertex.bufferCount = 0;
    pd.fragment = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.multisample.count = static_cast<uint32_t>(sample_count);
    pd.multisample.mask = 0xFFFFFFFFu;
    pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pd);
    if (!pipeline_) { std::fprintf(stderr, "[EffectOp] pipeline creation failed\n"); return false; }

    std::fprintf(stderr, "[EffectOp] pipeline ready (%d input%s)\n", num_inputs_, num_inputs_ == 1 ? "" : "s");
    return true;
}

void EffectOp::render(WGPUCommandEncoder encoder, WGPUTextureView target,
                      float vx, float vy, float vw, float vh, bool clear,
                      const WGPUTextureView* inputs, int num_inputs,
                      float time, const float* params, int nparams,
                      float scx, float scy, float scw, float sch) {
    if (!pipeline_) return;

    EUniforms u{};
    u.res[0] = vw; u.res[1] = vh; u.time = time;
    for (int i = 0; i < 4; ++i) u.p[i] = (params && i < nparams) ? params[i] : 0.f;
    wgpuQueueWriteBuffer(queue_, ubo_, 0, &u, sizeof u);

    // Bind group references the current input textures (which change per frame).
    WGPUBindGroupEntry be[4]{};
    be[0].binding = 0; be[0].buffer = ubo_; be[0].offset = 0; be[0].size = sizeof(EUniforms);
    be[1].binding = 1; be[1].textureView = inputs[0];
    be[2].binding = 2; be[2].sampler = sampler_;
    uint32_t entry_count = 3;
    if (num_inputs_ >= 2) {
        be[3].binding = 3;
        be[3].textureView = (num_inputs >= 2) ? inputs[1] : inputs[0];
        entry_count = 4;
    }
    WGPUBindGroupDescriptor bgd{};
    bgd.layout = bgl_;
    bgd.entryCount = entry_count;
    bgd.entries = be;
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(device_, &bgd);

    WGPURenderPassColorAttachment att{};
    att.view = target;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    att.loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    att.storeOp = WGPUStoreOp_Store;
    att.clearValue = WGPUColor{ 0, 0, 0, 1 };
    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp);
    wgpuRenderPassEncoderSetViewport(pass, vx, vy, vw, vh, 0.0f, 1.0f);
    // Scissor: explicit crop region when scw>0, else the viewport itself.
    const float ssx = scw > 0.f ? scx : vx, ssy = scw > 0.f ? scy : vy;
    const float ssw = scw > 0.f ? scw : vw, ssh = scw > 0.f ? sch : vh;
    wgpuRenderPassEncoderSetScissorRect(pass, static_cast<uint32_t>(std::max(0.f, ssx)),
                                        static_cast<uint32_t>(std::max(0.f, ssy)),
                                        static_cast<uint32_t>(std::max(0.f, ssw)),
                                        static_cast<uint32_t>(std::max(0.f, ssh)));
    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bg);  // retained by the command buffer until submit
}

void EffectOp::shutdown() {
    if (sampler_)  { wgpuSamplerRelease(sampler_);         sampler_  = nullptr; }
    if (ubo_)      { wgpuBufferRelease(ubo_);              ubo_      = nullptr; }
    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (layout_)   { wgpuPipelineLayoutRelease(layout_);   layout_   = nullptr; }
    if (bgl_)      { wgpuBindGroupLayoutRelease(bgl_);     bgl_      = nullptr; }
    if (frag_)     { wgpuShaderModuleRelease(frag_);       frag_     = nullptr; }
    if (vert_)     { wgpuShaderModuleRelease(vert_);       vert_     = nullptr; }
}

}  // namespace vivid
