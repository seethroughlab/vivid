#include <vivid/effects/gpu_handle.h>

namespace vivid {

void WGPUReleaseTrait<WGPUTexture>::release(WGPUTexture h)                 { if (h) wgpuTextureRelease(h); }
void WGPUReleaseTrait<WGPUTextureView>::release(WGPUTextureView h)         { if (h) wgpuTextureViewRelease(h); }
void WGPUReleaseTrait<WGPUBuffer>::release(WGPUBuffer h)                   { if (h) wgpuBufferRelease(h); }
void WGPUReleaseTrait<WGPURenderPipeline>::release(WGPURenderPipeline h)   { if (h) wgpuRenderPipelineRelease(h); }
void WGPUReleaseTrait<WGPUComputePipeline>::release(WGPUComputePipeline h) { if (h) wgpuComputePipelineRelease(h); }
void WGPUReleaseTrait<WGPUBindGroup>::release(WGPUBindGroup h)             { if (h) wgpuBindGroupRelease(h); }
void WGPUReleaseTrait<WGPUBindGroupLayout>::release(WGPUBindGroupLayout h) { if (h) wgpuBindGroupLayoutRelease(h); }
void WGPUReleaseTrait<WGPUSampler>::release(WGPUSampler h)                 { if (h) wgpuSamplerRelease(h); }
void WGPUReleaseTrait<WGPUShaderModule>::release(WGPUShaderModule h)       { if (h) wgpuShaderModuleRelease(h); }
void WGPUReleaseTrait<WGPUPipelineLayout>::release(WGPUPipelineLayout h)   { if (h) wgpuPipelineLayoutRelease(h); }
void WGPUReleaseTrait<WGPUQuerySet>::release(WGPUQuerySet h)               { if (h) wgpuQuerySetRelease(h); }

} // namespace vivid
