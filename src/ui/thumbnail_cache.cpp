#include "ui/thumbnail_cache.h"
#include "common/gpu_util.h"
#include <cstdio>
#include <cstring>

namespace vivid::ui {

using vivid::to_sv;

bool ThumbnailCache::init(WGPUDevice device, WGPUQueue queue, uint32_t thumb_w, uint32_t thumb_h) {
    device_ = device;
    queue_ = queue;
    thumb_w_ = thumb_w;
    thumb_h_ = thumb_h;
    return true;
}

void ThumbnailCache::shutdown() {
    for (auto& [id, entry] : entries_) {
        if (entry.view) { wgpuTextureViewRelease(entry.view); entry.view = nullptr; }
        if (entry.texture) { wgpuTextureRelease(entry.texture); entry.texture = nullptr; }
    }
    entries_.clear();
    device_ = nullptr;
}

WGPUTextureView ThumbnailCache::get_or_create(const std::string& node_id) {
    auto it = entries_.find(node_id);
    if (it != entries_.end()) return it->second.view;

    // Create thumbnail texture (RGBA16Float to match offscreen)
    std::string label = "Thumb:" + node_id;
    WGPUTextureDescriptor desc{};
    desc.label = to_sv(label.c_str());
    desc.size = { thumb_w_, thumb_h_, 1 };
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA16Float;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;

    WGPUTexture tex = wgpuDeviceCreateTexture(device_, &desc);
    if (!tex) {
        std::fprintf(stderr, "[vivid] ThumbnailCache: failed to create texture for '%s'\n", node_id.c_str());
        return nullptr;
    }

    WGPUTextureViewDescriptor view_desc{};
    view_desc.label = to_sv(label.c_str());
    view_desc.format = WGPUTextureFormat_RGBA16Float;
    view_desc.dimension = WGPUTextureViewDimension_2D;
    view_desc.baseMipLevel = 0;
    view_desc.mipLevelCount = 1;
    view_desc.baseArrayLayer = 0;
    view_desc.arrayLayerCount = 1;
    view_desc.aspect = WGPUTextureAspect_All;

    WGPUTextureView view = wgpuTextureCreateView(tex, &view_desc);

    entries_[node_id] = { tex, view };
    std::fprintf(stderr, "[vivid] ThumbnailCache: created %ux%u thumbnail for '%s'\n",
                 thumb_w_, thumb_h_, node_id.c_str());
    return view;
}

void ThumbnailCache::upload_cpu(const std::string& node_id, const uint8_t* pixels) {
    auto it = entries_.find(node_id);
    if (it == entries_.end()) {
        // Create RGBA8Unorm texture for CPU-uploaded thumbnails
        std::string label = "ThumbCPU:" + node_id;
        WGPUTextureDescriptor desc{};
        desc.label = to_sv(label.c_str());
        desc.size = { thumb_w_, thumb_h_, 1 };
        desc.mipLevelCount = 1;
        desc.sampleCount = 1;
        desc.dimension = WGPUTextureDimension_2D;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;

        WGPUTexture tex = wgpuDeviceCreateTexture(device_, &desc);
        if (!tex) {
            std::fprintf(stderr, "[vivid] ThumbnailCache: failed to create CPU texture for '%s'\n", node_id.c_str());
            return;
        }

        WGPUTextureViewDescriptor view_desc{};
        view_desc.label = to_sv(label.c_str());
        view_desc.format = WGPUTextureFormat_RGBA8Unorm;
        view_desc.dimension = WGPUTextureViewDimension_2D;
        view_desc.baseMipLevel = 0;
        view_desc.mipLevelCount = 1;
        view_desc.baseArrayLayer = 0;
        view_desc.arrayLayerCount = 1;
        view_desc.aspect = WGPUTextureAspect_All;

        WGPUTextureView view = wgpuTextureCreateView(tex, &view_desc);
        entries_[node_id] = { tex, view, true };
        it = entries_.find(node_id);
        std::fprintf(stderr, "[vivid] ThumbnailCache: created %ux%u CPU thumbnail for '%s'\n",
                     thumb_w_, thumb_h_, node_id.c_str());
    }

    // Upload pixel data
    WGPUTexelCopyTextureInfo dest{};
    dest.texture = it->second.texture;
    dest.mipLevel = 0;
    dest.origin = { 0, 0, 0 };
    dest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout data_layout{};
    data_layout.offset = 0;
    data_layout.bytesPerRow = thumb_w_ * 4;
    data_layout.rowsPerImage = thumb_h_;

    WGPUExtent3D extent = { thumb_w_, thumb_h_, 1 };
    wgpuQueueWriteTexture(queue_, &dest, pixels, thumb_w_ * thumb_h_ * 4, &data_layout, &extent);
}

WGPUTextureView ThumbnailCache::get_view(const std::string& node_id) const {
    auto it = entries_.find(node_id);
    return (it != entries_.end()) ? it->second.view : nullptr;
}

} // namespace vivid::ui
