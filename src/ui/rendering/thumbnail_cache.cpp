#include "ui/rendering/thumbnail_cache.h"
#include "common/gpu_util.h"
#include "runtime/gpu/mipmap_generator.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace vivid::ui {

using vivid::to_sv;

namespace {

uint32_t compute_mip_levels(uint32_t w, uint32_t h) {
    uint32_t m = std::max<uint32_t>(1u, std::max(w, h));
    uint32_t levels = 1;
    while (m > 1) {
        m >>= 1;
        ++levels;
    }
    return levels;
}

} // namespace

bool ThumbnailCache::init(WGPUDevice device, WGPUQueue queue, uint32_t thumb_w, uint32_t thumb_h) {
    device_ = device;
    queue_ = queue;
    thumb_w_ = thumb_w;
    thumb_h_ = thumb_h;
    return true;
}

void ThumbnailCache::shutdown() {
    for (auto& [id, entry] : entries_) release_entry(entry);
    entries_.clear();
    device_ = nullptr;
    mip_generator_ = nullptr;
}

void ThumbnailCache::release_entry(Entry& e) {
    if (mip_generator_) {
        if (e.sample_view) mip_generator_->forget(e.sample_view);
        for (auto v : e.mip_sample_views) {
            if (v) mip_generator_->forget(v);
        }
    }
    for (auto v : e.mip_render_views) {
        if (v) wgpuTextureViewRelease(v);
    }
    e.mip_render_views.clear();
    for (auto v : e.mip_sample_views) {
        if (v) wgpuTextureViewRelease(v);
    }
    e.mip_sample_views.clear();
    if (e.sample_view) { wgpuTextureViewRelease(e.sample_view); e.sample_view = nullptr; }
    if (e.texture)     { wgpuTextureRelease(e.texture);         e.texture     = nullptr; }
}

WGPUTextureView ThumbnailCache::get_or_create_render_view(const std::string& node_id) {
    auto it = entries_.find(node_id);
    if (it != entries_.end()) {
        return it->second.mip_render_views.empty() ? nullptr : it->second.mip_render_views[0];
    }

    const uint32_t mip_levels = compute_mip_levels(thumb_w_, thumb_h_);

    std::string label = "Thumb:" + node_id;
    WGPUTextureDescriptor desc{};
    desc.label = to_sv(label.c_str());
    desc.size = { thumb_w_, thumb_h_, 1 };
    desc.mipLevelCount = mip_levels;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA16Float;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;

    WGPUTexture tex = wgpuDeviceCreateTexture(device_, &desc);
    if (!tex) {
        std::fprintf(stderr, "[vivid] ThumbnailCache: failed to create texture for '%s'\n", node_id.c_str());
        return nullptr;
    }

    Entry entry;
    entry.texture = tex;

    // Full-chain sample view (for display-time trilinear sampling)
    {
        WGPUTextureViewDescriptor v{};
        v.label = to_sv(label.c_str());
        v.format = WGPUTextureFormat_RGBA16Float;
        v.dimension = WGPUTextureViewDimension_2D;
        v.baseMipLevel = 0;
        v.mipLevelCount = mip_levels;
        v.baseArrayLayer = 0;
        v.arrayLayerCount = 1;
        v.aspect = WGPUTextureAspect_All;
        entry.sample_view = wgpuTextureCreateView(tex, &v);
    }

    // Per-level render + sample views
    entry.mip_render_views.reserve(mip_levels);
    entry.mip_sample_views.reserve(mip_levels);
    for (uint32_t level = 0; level < mip_levels; ++level) {
        WGPUTextureViewDescriptor v{};
        v.label = to_sv(label.c_str());
        v.format = WGPUTextureFormat_RGBA16Float;
        v.dimension = WGPUTextureViewDimension_2D;
        v.baseMipLevel = level;
        v.mipLevelCount = 1;
        v.baseArrayLayer = 0;
        v.arrayLayerCount = 1;
        v.aspect = WGPUTextureAspect_All;
        entry.mip_render_views.push_back(wgpuTextureCreateView(tex, &v));
        entry.mip_sample_views.push_back(wgpuTextureCreateView(tex, &v));
    }

    std::fprintf(stderr, "[vivid] ThumbnailCache: created %ux%u (%u mips) thumbnail for '%s'\n",
                 thumb_w_, thumb_h_, mip_levels, node_id.c_str());

    WGPUTextureView mip0 = entry.mip_render_views.empty() ? nullptr : entry.mip_render_views[0];
    entries_[node_id] = std::move(entry);
    return mip0;
}

WGPUTextureView ThumbnailCache::get_sample_view(const std::string& node_id) const {
    auto it = entries_.find(node_id);
    return (it != entries_.end()) ? it->second.sample_view : nullptr;
}

WGPUTexture ThumbnailCache::get_texture(const std::string& node_id) const {
    auto it = entries_.find(node_id);
    return (it != entries_.end()) ? it->second.texture : nullptr;
}

const std::vector<WGPUTextureView>& ThumbnailCache::mip_render_views(const std::string& node_id) const {
    auto it = entries_.find(node_id);
    return (it != entries_.end()) ? it->second.mip_render_views : kEmptyViews;
}

const std::vector<WGPUTextureView>& ThumbnailCache::mip_sample_views(const std::string& node_id) const {
    auto it = entries_.find(node_id);
    return (it != entries_.end()) ? it->second.mip_sample_views : kEmptyViews;
}

void ThumbnailCache::remove(const std::string& node_id) {
    auto it = entries_.find(node_id);
    if (it == entries_.end()) return;
    release_entry(it->second);
    entries_.erase(it);
}

} // namespace vivid::ui
