#include "texture_upload.h"

#include "operator_api/gpu_common.h"

#include <cstring>
#include <vector>

void movie_texture_release(MovieTextureState& state) {
    vivid::gpu::release(state.texture);
    vivid::gpu::release(state.view);
    vivid::gpu::release(state.bind_group);
    state.width = 0;
    state.height = 0;
    state.format = WGPUTextureFormat_BGRA8Unorm;
    state.compressed = false;
}

bool movie_texture_recreate(WGPUDevice device,
                            WGPUSampler sampler,
                            WGPUBindGroupLayout bind_layout,
                            MovieTextureState& state,
                            uint32_t width,
                            uint32_t height,
                            WGPUTextureFormat format,
                            bool compressed) {
    movie_texture_release(state);

    WGPUTextureDescriptor td{};
    td.label = vivid_sv("MovieLoaded Staging");
    td.size = {width, height, 1};
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.dimension = WGPUTextureDimension_2D;
    td.format = format;
    td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    if (!compressed) {
        td.usage |= WGPUTextureUsage_RenderAttachment;
    }
    state.texture = wgpuDeviceCreateTexture(device, &td);
    if (!state.texture) return false;

    WGPUTextureViewDescriptor vd{};
    vd.format = format;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_All;
    state.view = wgpuTextureCreateView(state.texture, &vd);
    if (!state.view) {
        movie_texture_release(state);
        return false;
    }

    WGPUBindGroupEntry entries[2]{};
    entries[0].binding = 0;
    entries[0].sampler = sampler;
    entries[1].binding = 1;
    entries[1].textureView = state.view;

    WGPUBindGroupDescriptor bg_desc{};
    bg_desc.label = vivid_sv("MovieLoaded BG");
    bg_desc.layout = bind_layout;
    bg_desc.entryCount = 2;
    bg_desc.entries = entries;
    state.bind_group = wgpuDeviceCreateBindGroup(device, &bg_desc);
    if (!state.bind_group) {
        movie_texture_release(state);
        return false;
    }

    state.width = width;
    state.height = height;
    state.format = format;
    state.compressed = compressed;
    return true;
}

void movie_texture_rebuild_bind_group(WGPUDevice device,
                                      WGPUSampler sampler,
                                      WGPUBindGroupLayout bind_layout,
                                      MovieTextureState& state) {
    if (!state.view) return;
    if (state.bind_group) wgpuBindGroupRelease(state.bind_group);

    WGPUBindGroupEntry entries[2]{};
    entries[0].binding = 0;
    entries[0].sampler = sampler;
    entries[1].binding = 1;
    entries[1].textureView = state.view;

    WGPUBindGroupDescriptor bg_desc{};
    bg_desc.label = vivid_sv("MovieLoaded BG");
    bg_desc.layout = bind_layout;
    bg_desc.entryCount = 2;
    bg_desc.entries = entries;
    state.bind_group = wgpuDeviceCreateBindGroup(device, &bg_desc);
}

uint32_t movie_aligned_bpr(uint32_t src_row_bytes) {
    return (src_row_bytes + 255u) & ~255u;
}

bool movie_upload_bgra(WGPUQueue queue,
                       const MovieTextureState& state,
                       const uint8_t* pixels,
                       uint32_t width,
                       uint32_t height) {
    if (!state.texture || !pixels || width == 0 || height == 0) return false;

    uint32_t src_row_bytes = width * 4;
    uint32_t aligned_bpr = movie_aligned_bpr(src_row_bytes);

    WGPUTexelCopyTextureInfo dest{};
    dest.texture = state.texture;
    dest.mipLevel = 0;
    dest.origin = {0, 0, 0};
    dest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow = aligned_bpr;
    layout.rowsPerImage = height;

    WGPUExtent3D extent = {width, height, 1};

    if (aligned_bpr == src_row_bytes) {
        wgpuQueueWriteTexture(queue, &dest, pixels,
                              static_cast<size_t>(src_row_bytes) * height,
                              &layout, &extent);
        return true;
    }

    std::vector<uint8_t> padded(static_cast<size_t>(aligned_bpr) * height, 0);
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(padded.data() + static_cast<size_t>(row) * aligned_bpr,
                    pixels + static_cast<size_t>(row) * src_row_bytes,
                    src_row_bytes);
    }
    wgpuQueueWriteTexture(queue, &dest, padded.data(), padded.size(), &layout, &extent);
    return true;
}

bool movie_upload_compressed(WGPUQueue queue,
                             const MovieTextureState& state,
                             const uint8_t* data,
                             size_t data_size,
                             uint32_t width,
                             uint32_t height,
                             WGPUTextureFormat format) {
    if (!state.texture || !data || data_size == 0 || width == 0 || height == 0) return false;

    size_t bytes_per_block = 0;
    if (format == WGPUTextureFormat_BC1RGBAUnorm || format == WGPUTextureFormat_BC4RUnorm) {
        bytes_per_block = 8;
    } else if (format == WGPUTextureFormat_BC3RGBAUnorm) {
        bytes_per_block = 16;
    } else {
        return false;
    }

    uint32_t blocks_w = (width + 3) / 4;
    uint32_t blocks_h = (height + 3) / 4;
    uint32_t src_row_bytes = static_cast<uint32_t>(blocks_w * bytes_per_block);
    size_t expected_size = static_cast<size_t>(src_row_bytes) * blocks_h;
    if (data_size < expected_size) return false;
    uint32_t aligned_bpr = movie_aligned_bpr(src_row_bytes);

    WGPUTexelCopyTextureInfo dest{};
    dest.texture = state.texture;
    dest.mipLevel = 0;
    dest.origin = {0, 0, 0};
    dest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow = aligned_bpr;
    layout.rowsPerImage = blocks_h;

    WGPUExtent3D extent = {width, height, 1};

    if (aligned_bpr == src_row_bytes) {
        wgpuQueueWriteTexture(queue, &dest, data, expected_size, &layout, &extent);
        return true;
    }

    std::vector<uint8_t> padded(static_cast<size_t>(aligned_bpr) * blocks_h, 0);
    for (uint32_t row = 0; row < blocks_h; ++row) {
        std::memcpy(padded.data() + static_cast<size_t>(row) * aligned_bpr,
                    data + static_cast<size_t>(row) * src_row_bytes,
                    src_row_bytes);
    }
    wgpuQueueWriteTexture(queue, &dest, padded.data(), padded.size(), &layout, &extent);
    return true;
}
