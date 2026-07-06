#include "gpu/shader_op.h"
#include "gpu/gpu_util.h"
#include <cstdio>
#include <cstdint>

namespace vivid {

// std140: res@0, time@8, then kNumPlasmaUniforms floats @12.. ; padded to 16.
struct Uniforms { float res[2]; float time; float u[kNumPlasmaUniforms]; float _pad; };

// Fullscreen triangle — no vertex buffers; emits a vec2 uv in [0,1].
static const char* kVertGLSL = R"(#version 450
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = p;
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

bool ShaderOp::init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat target_format,
                    const char* glsl_fragment) {
    device_ = device;
    queue_ = queue;

    vert_ = make_glsl(device_, WGPUShaderStage_Vertex, kVertGLSL, "shaderop.vert");
    frag_ = make_glsl(device_, WGPUShaderStage_Fragment, glsl_fragment, "shaderop.frag");
    if (!vert_ || !frag_) { std::fprintf(stderr, "[ShaderOp] GLSL module creation failed\n"); return false; }

    WGPUBindGroupLayoutEntry e{};
    e.binding = 0;
    e.visibility = WGPUShaderStage_Fragment;
    e.buffer.type = WGPUBufferBindingType_Uniform;
    e.buffer.minBindingSize = sizeof(Uniforms);
    WGPUBindGroupLayoutDescriptor bgld{};
    bgld.entryCount = 1;
    bgld.entries = &e;
    bgl_ = wgpuDeviceCreateBindGroupLayout(device_, &bgld);

    WGPUPipelineLayoutDescriptor pld{};
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &bgl_;
    layout_ = wgpuDeviceCreatePipelineLayout(device_, &pld);

    WGPUBufferDescriptor bd{};
    bd.size = sizeof(Uniforms);
    bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    ubo_ = wgpuDeviceCreateBuffer(device_, &bd);

    WGPUBindGroupEntry be{};
    be.binding = 0;
    be.buffer = ubo_;
    be.offset = 0;
    be.size = sizeof(Uniforms);
    WGPUBindGroupDescriptor bgd{};
    bgd.layout = bgl_;
    bgd.entryCount = 1;
    bgd.entries = &be;
    bind_ = wgpuDeviceCreateBindGroup(device_, &bgd);

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
    pd.multisample.count = 1;
    pd.multisample.mask = 0xFFFFFFFFu;
    pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pd);
    if (!pipeline_) { std::fprintf(stderr, "[ShaderOp] pipeline creation failed\n"); return false; }

    std::fprintf(stderr, "[ShaderOp] GLSL shader pipeline ready\n");
    return true;
}

void ShaderOp::render(WGPUCommandEncoder encoder, WGPUTextureView view,
                      float vx, float vy, float vw, float vh, float time,
                      const float* uniforms, bool clear) {
    if (!pipeline_) return;
    Uniforms u{};
    u.res[0] = vw; u.res[1] = vh; u.time = time;
    for (int i = 0; i < kNumPlasmaUniforms; ++i) u.u[i] = uniforms ? uniforms[i] : 0.f;
    wgpuQueueWriteBuffer(queue_, ubo_, 0, &u, sizeof u);

    WGPURenderPassColorAttachment att{};
    att.view = view;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    att.loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    att.storeOp = WGPUStoreOp_Store;
    att.clearValue = WGPUColor{ 0, 0, 0, 1 };
    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp);
    wgpuRenderPassEncoderSetViewport(pass, vx, vy, vw, vh, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, static_cast<uint32_t>(vx), static_cast<uint32_t>(vy),
                                        static_cast<uint32_t>(vw), static_cast<uint32_t>(vh));
    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

void ShaderOp::shutdown() {
    if (bind_)     { wgpuBindGroupRelease(bind_);          bind_     = nullptr; }
    if (ubo_)      { wgpuBufferRelease(ubo_);              ubo_      = nullptr; }
    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (layout_)   { wgpuPipelineLayoutRelease(layout_);   layout_   = nullptr; }
    if (bgl_)      { wgpuBindGroupLayoutRelease(bgl_);     bgl_      = nullptr; }
    if (frag_)     { wgpuShaderModuleRelease(frag_);       frag_     = nullptr; }
    if (vert_)     { wgpuShaderModuleRelease(vert_);       vert_     = nullptr; }
}

}  // namespace vivid
