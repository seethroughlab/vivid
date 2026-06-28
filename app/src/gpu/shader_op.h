#pragma once
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>   // native extensions: WGPUShaderSourceGLSL

namespace vivid {

// The plasma generator exposes this many float uniforms (warp/hue/density/glow).
// Independent of the node graph's port count (which also drives the fx chain).
inline constexpr int kNumPlasmaUniforms = 4;

// A GLSL fullscreen fragment-shader pass (TouchDesigner TOP-style), rendered
// directly into a viewport sub-rect of the surface. GLSL is compiled by
// wgpu-native natively (WGPUShaderSourceGLSL) — no glslang/shaderc toolchain.
//
// The fragment must declare:
//   layout(location=0) in  vec2 v_uv;
//   layout(location=0) out vec4 o_color;
//   layout(set=0, binding=0) uniform U {
//       vec2 u_res; float u_time; float u_warp; float u_hue; float u_density; float u_glow; };
class ShaderOp {
public:
    bool init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat target_format,
              const char* glsl_fragment);
    void shutdown();
    bool ok() const { return pipeline_ != nullptr; }

    // Render the pass into (vx,vy,vw,vh) of `view`. `uniforms` holds
    // kNumPlasmaUniforms floats (the named u_* inputs); u_res = viewport size.
    // clear=true clears the target first (offscreen RT); false composes (LoadOp_Load).
    void render(WGPUCommandEncoder encoder, WGPUTextureView view,
                float vx, float vy, float vw, float vh, float time,
                const float* uniforms, bool clear);

private:
    WGPUDevice          device_   = nullptr;
    WGPUQueue           queue_    = nullptr;
    WGPUShaderModule    vert_     = nullptr;
    WGPUShaderModule    frag_     = nullptr;
    WGPUBindGroupLayout bgl_      = nullptr;
    WGPUPipelineLayout  layout_   = nullptr;
    WGPURenderPipeline  pipeline_ = nullptr;
    WGPUBuffer          ubo_      = nullptr;
    WGPUBindGroup       bind_     = nullptr;
};

}  // namespace vivid
