#pragma once
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>   // native extensions: WGPUShaderSourceGLSL

namespace vivid {

// A fullscreen GLSL fragment pass that samples 1-2 input textures and writes to
// a target view — the primitive for the FBO effect chain (feedback, blur, …).
//
// The fragment must declare (Vulkan-GLSL separate texture/sampler):
//   layout(set=0, binding=0) uniform U { vec2 u_res; float u_time; float p0,p1,p2,p3; };
//   layout(set=0, binding=1) uniform texture2D u_tex0;
//   layout(set=0, binding=2) uniform sampler   u_samp;
//   layout(set=0, binding=3) uniform texture2D u_tex1;   // only if num_inputs == 2
// and sample via texture(sampler2D(u_tex0, u_samp), uv).
class EffectOp {
public:
    bool init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat target_format,
              const char* glsl_fragment, int num_inputs);
    void shutdown();
    bool ok() const { return pipeline_ != nullptr; }

    // Render into `target` at viewport (vx,vy,vw,vh). `inputs` holds num_inputs
    // texture views; `params` holds up to 4 floats for p0..p3 (u_res = viewport).
    // Optional scissor (scw>0) crops output to (scx,scy,scw,sch) independently of
    // the viewport — lets a blit be cropped to a clip region. Defaults to viewport.
    void render(WGPUCommandEncoder encoder, WGPUTextureView target,
                float vx, float vy, float vw, float vh, bool clear,
                const WGPUTextureView* inputs, int num_inputs,
                float time, const float* params, int nparams,
                float scx = -1.f, float scy = 0.f, float scw = 0.f, float sch = 0.f);

private:
    WGPUDevice          device_   = nullptr;
    WGPUQueue           queue_    = nullptr;
    WGPUShaderModule    vert_     = nullptr;
    WGPUShaderModule    frag_     = nullptr;
    WGPUBindGroupLayout bgl_      = nullptr;
    WGPUPipelineLayout  layout_   = nullptr;
    WGPURenderPipeline  pipeline_ = nullptr;
    WGPUBuffer          ubo_      = nullptr;
    WGPUSampler         sampler_  = nullptr;
    int                 num_inputs_ = 1;
};

}  // namespace vivid
