#include "ui/rendering/thumbnail_cache.h"
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

WGPUTextureView ThumbnailCache::get_view(const std::string& node_id) const {
    auto it = entries_.find(node_id);
    return (it != entries_.end()) ? it->second.view : nullptr;
}

WGPUTexture ThumbnailCache::get_texture(const std::string& node_id) const {
    auto it = entries_.find(node_id);
    return (it != entries_.end()) ? it->second.texture : nullptr;
}

void ThumbnailCache::remove(const std::string& node_id) {
    auto it = entries_.find(node_id);
    if (it == entries_.end()) return;
    if (it->second.view) wgpuTextureViewRelease(it->second.view);
    if (it->second.texture) wgpuTextureRelease(it->second.texture);
    entries_.erase(it);
}

} // namespace vivid::ui
