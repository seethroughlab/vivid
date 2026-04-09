#pragma once

#include <webgpu/webgpu.h>
#include <cstddef>
#include <cstdint>

struct MovieTextureState {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    WGPUBindGroup bind_group = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;
    bool compressed = false;
};

void movie_texture_release(MovieTextureState& state);

bool movie_texture_recreate(WGPUDevice device,
                            WGPUSampler sampler,
                            WGPUBindGroupLayout bind_layout,
                            MovieTextureState& state,
                            uint32_t width,
                            uint32_t height,
                            WGPUTextureFormat format,
                            bool compressed);

// Recreate the bind group to pick up texture content changes.
// wgpu-native bind groups may not observe writes from prior render passes
// in the same command buffer, so this must be called before each blit.
void movie_texture_rebuild_bind_group(WGPUDevice device,
                                      WGPUSampler sampler,
                                      WGPUBindGroupLayout bind_layout,
                                      MovieTextureState& state);

uint32_t movie_aligned_bpr(uint32_t src_row_bytes);

bool movie_upload_bgra(WGPUQueue queue,
                       const MovieTextureState& state,
                       const uint8_t* pixels,
                       uint32_t width,
                       uint32_t height);

bool movie_upload_compressed(WGPUQueue queue,
                             const MovieTextureState& state,
                             const uint8_t* data,
                             size_t data_size,
                             uint32_t width,
                             uint32_t height,
                             WGPUTextureFormat format);
