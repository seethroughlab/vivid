#pragma once

#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>

namespace vivid::ui {

class ThumbnailCache {
public:
    ~ThumbnailCache() { shutdown(); }
    ThumbnailCache() = default;
    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    bool init(WGPUDevice device, WGPUQueue queue, uint32_t thumb_w, uint32_t thumb_h);
    void shutdown();

    // Returns a texture view for rendering into (creates if needed) — RGBA16Float for GPU capture
    WGPUTextureView get_or_create(const std::string& node_id);
    // Upload CPU-side RGBA8 pixels to a thumbnail texture
    void upload_cpu(const std::string& node_id, const uint8_t* pixels);
    // Returns the existing view for reading (nullptr if not created yet)
    WGPUTextureView get_view(const std::string& node_id) const;
    // Remove a thumbnail entry (call when a node is deleted from the graph)
    void remove(const std::string& node_id);
    // Evict entries for nodes not in the active set (call after topology changes)
    template<typename Container>
    void retain_only(const Container& active_ids) {
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if (active_ids.count(it->first) == 0) {
                if (it->second.view) wgpuTextureViewRelease(it->second.view);
                if (it->second.texture) wgpuTextureRelease(it->second.texture);
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    struct Entry {
        WGPUTexture texture = nullptr;
        WGPUTextureView view = nullptr;
        bool is_cpu = false;
    };

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    uint32_t thumb_w_ = 0;
    uint32_t thumb_h_ = 0;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace vivid::ui
