#pragma once

#include <webgpu/webgpu.h>
#include <unordered_map>
#include <vector>

namespace vivid {

// Generates a 2D mipmap chain by running a fullscreen downsample blit per
// level. Callers provide two parallel arrays of per-level views on the same
// texture: `render_views[i]` targets mip i, `sample_views[i]` reads mip i.
// For each i in [1, N), the generator reads sample_views[i-1] and writes
// render_views[i] with a linear-filtered fullscreen blit.
//
// WebGPU has no built-in generateMipmaps; this is the standard workaround.
// A dedicated class (rather than extending FullscreenBlit) keeps a stable
// per-view bind-group cache across frames so generating chains for many
// thumbnails each frame doesn't thrash a single-slot cache.
class MipmapGenerator {
public:
    bool init(WGPUDevice device, WGPUTextureFormat target_format);
    void shutdown();

    // Writes mips 1..N-1. sample_views[0] is unused (mip 0 is the source).
    // render_views[0] is unused (mip 0 is pre-filled by the caller).
    void generate(WGPUCommandEncoder encoder,
                  const std::vector<WGPUTextureView>& render_views,
                  const std::vector<WGPUTextureView>& sample_views);

    // Evict a source view from the bind-group cache before its texture is
    // released (ThumbnailCache::remove / retain_only / clear).
    void forget(WGPUTextureView sample_view);

private:
    WGPUBindGroup get_or_create_bind_group(WGPUTextureView sample_view);

    WGPUDevice          device_      = nullptr;
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUSampler         sampler_     = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUTextureFormat   target_format_ = WGPUTextureFormat_Undefined;

    std::unordered_map<WGPUTextureView, WGPUBindGroup> bind_cache_;
};

} // namespace vivid
