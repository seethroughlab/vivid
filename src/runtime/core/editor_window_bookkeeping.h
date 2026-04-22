#pragma once

// Pure helpers for EditorWindowManager — no GPU, no GLFW.
// Split out so the manager can reuse them directly and unit tests can validate
// state transitions and surface-metric derivation without a real second window.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace vivid {

struct EditorWindowSurfaceMetrics {
    uint32_t logical_width = 0;
    uint32_t logical_height = 0;
    uint32_t framebuffer_width = 0;
    uint32_t framebuffer_height = 0;
    float dpi_scale = 1.0f;
};

inline EditorWindowSurfaceMetrics make_editor_window_surface_metrics(
    int logical_width, int logical_height,
    int framebuffer_width, int framebuffer_height,
    float dpi_scale) {
    const float safe_scale = dpi_scale > 0.0f ? dpi_scale : 1.0f;

    EditorWindowSurfaceMetrics metrics{};
    metrics.logical_width = logical_width > 0
        ? static_cast<uint32_t>(logical_width)
        : (framebuffer_width > 0
            ? static_cast<uint32_t>(std::lround(
                  static_cast<float>(framebuffer_width) / safe_scale))
            : 0u);
    metrics.logical_height = logical_height > 0
        ? static_cast<uint32_t>(logical_height)
        : (framebuffer_height > 0
            ? static_cast<uint32_t>(std::lround(
                  static_cast<float>(framebuffer_height) / safe_scale))
            : 0u);
    metrics.framebuffer_width = framebuffer_width > 0
        ? static_cast<uint32_t>(framebuffer_width)
        : static_cast<uint32_t>(std::lround(metrics.logical_width * safe_scale));
    metrics.framebuffer_height = framebuffer_height > 0
        ? static_cast<uint32_t>(framebuffer_height)
        : static_cast<uint32_t>(std::lround(metrics.logical_height * safe_scale));
    metrics.dpi_scale = safe_scale;
    return metrics;
}

struct EditorStringParamView {
    const char* const* values = nullptr;
    uint32_t count = 0;
};

inline EditorStringParamView make_editor_string_param_view(
    const std::vector<const char*>& values) {
    return {values.empty() ? nullptr : values.data(),
            static_cast<uint32_t>(values.size())};
}

class EditorWindowBookkeeping {
public:
    // Record a newly opened window. Returns false if node_id was already open
    // (duplicate-open prevention); callers should refocus instead.
    bool record_open(const std::string& node_id) {
        if (is_open(node_id)) return false;
        entries_.push_back({node_id, /*mark_destroy*/ false});
        return true;
    }

    bool is_open(const std::string& node_id) const {
        for (const auto& e : entries_) {
            if (e.node_id == node_id) return true;
        }
        return false;
    }

    size_t size() const { return entries_.size(); }

    // Flag a window for destruction on the next sweep().
    void mark_for_destroy(const std::string& node_id) {
        for (auto& e : entries_) {
            if (e.node_id == node_id) { e.mark_destroy = true; return; }
        }
    }

    // Remove all windows flagged for destruction. Invokes the caller-supplied
    // destroyer for each (simulating the teardown of GLFW/WGPU resources).
    // Returns the count of entries destroyed.
    template <typename Fn>
    size_t sweep(Fn&& destroyer) {
        size_t count = 0;
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [&](const Entry& e) {
                    if (e.mark_destroy) {
                        destroyer(e.node_id);
                        ++count;
                        return true;
                    }
                    return false;
                }),
            entries_.end());
        return count;
    }

    // Tear every entry down unconditionally. Used on reload or shutdown.
    template <typename Fn>
    size_t close_all(Fn&& destroyer) {
        size_t count = entries_.size();
        for (const auto& e : entries_) destroyer(e.node_id);
        entries_.clear();
        return count;
    }

    // Explicit close for one node; returns true if an entry was removed.
    template <typename Fn>
    bool close_one(const std::string& node_id, Fn&& destroyer) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->node_id == node_id) {
                destroyer(node_id);
                entries_.erase(it);
                return true;
            }
        }
        return false;
    }

private:
    struct Entry {
        std::string node_id;
        bool mark_destroy = false;
    };
    std::vector<Entry> entries_;
};

} // namespace vivid
