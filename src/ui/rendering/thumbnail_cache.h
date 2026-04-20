#pragma once

#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {
class MipmapGenerator;
}

namespace vivid::ui {

// Per-node thumbnail textures with a full 2D mipmap chain.
//
// - Capture/draw targets mip 0 via `get_or_create_render_view()` (returns a
//   view that covers only baseMipLevel=0, mipLevelCount=1).
// - Downstream mip levels 1..N-1 are filled each frame by `MipmapGenerator`
//   using the per-level arrays returned by `mip_render_views()` /
//   `mip_sample_views()`.
// - Display sampling uses `get_sample_view()`, which covers the whole chain so
//   the thumbnail renderer's trilinear sampler can pick a pre-filtered level.
//
// Textures are RGBA16Float to match the per-node offscreen format; a
// `MipmapGenerator*` may be passed in so released entries evict their bind
// groups from the generator's cache (prevents use-after-free of views).
class ThumbnailCache {
public:
    ~ThumbnailCache() { shutdown(); }
    ThumbnailCache() = default;
    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    bool init(WGPUDevice device, WGPUQueue queue, uint32_t thumb_w, uint32_t thumb_h);
    void shutdown();

    // Generator is consulted on release paths to evict stale bind-group cache
    // entries. Optional — may be nullptr if mipmaps are disabled.
    void set_mipmap_generator(vivid::MipmapGenerator* gen) { mip_generator_ = gen; }

    // Render target for the operator's output (mip 0 only). Creates the
    // texture + full view chain on first call for this node id.
    WGPUTextureView get_or_create_render_view(const std::string& node_id);

    // Full-chain view for sampling at display time. Returns nullptr if the
    // entry hasn't been created yet.
    WGPUTextureView get_sample_view(const std::string& node_id) const;

    WGPUTexture get_texture(const std::string& node_id) const;

    // Per-level views for mipmap generation. Both vectors are
    // empty if the entry doesn't exist. sample_views[i] covers mip i only
    // (for use as a texture binding); render_views[i] also covers mip i only
    // (for use as a render attachment).
    const std::vector<WGPUTextureView>& mip_render_views(const std::string& node_id) const;
    const std::vector<WGPUTextureView>& mip_sample_views(const std::string& node_id) const;

    void remove(const std::string& node_id);

    void clear() {
        for (auto& [id, e] : entries_) release_entry(e);
        entries_.clear();
    }

    template<typename Container>
    void retain_only(const Container& active_ids) {
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if (active_ids.count(it->first) == 0) {
                release_entry(it->second);
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    struct Entry {
        WGPUTexture texture = nullptr;
        WGPUTextureView sample_view = nullptr;  // full mip chain (for display)
        std::vector<WGPUTextureView> mip_render_views;  // one per level, baseMipLevel=i
        std::vector<WGPUTextureView> mip_sample_views;  // one per level, baseMipLevel=i
    };

    void release_entry(Entry& e);

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    uint32_t thumb_w_ = 0;
    uint32_t thumb_h_ = 0;
    vivid::MipmapGenerator* mip_generator_ = nullptr;
    std::unordered_map<std::string, Entry> entries_;

    static inline const std::vector<WGPUTextureView> kEmptyViews{};
};

} // namespace vivid::ui
