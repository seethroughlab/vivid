#pragma once

// Per-GPU-node frame analysis. Computes lightweight metrics from a tiny
// readback of the output texture (4x4 = 16 pixels, double-buffered).
//
// Metrics:
//   frame_hash    — FNV-1a hash of the 16 sampled pixels; changes when content changes.
//   brightness    — Mean luminance (0–1).
//   contrast      — Stddev of per-pixel luminance (0–1).
//   dominant_hue  — Hue angle (0–360) of the average RGB colour.

#include "operator_api/texture_readback.h"
#include <cmath>
#include <cstdint>

namespace vivid {

class GpuFrameAnalysis {
public:
    void init(WGPUDevice device) {
        if (inited_) return;
        readback_.init(device, kSize, kSize);
        inited_ = true;
    }

    // Queue a readback of the output texture. Call after process_gpu().
    void queue_readback(WGPUCommandEncoder encoder, WGPUQueue queue,
                        WGPUTextureView view, uint32_t w, uint32_t h) {
        if (!inited_ || !view) return;
        readback_.readback(encoder, queue, view, w, h);
    }

    // Compute metrics from the most recent completed readback.
    // Call before queue_readback (reads previous frame's data).
    void compute_metrics() {
        if (!readback_.ready()) return;
        const float* data = readback_.data();  // RGB float, kSize*kSize*3
        if (!data) return;

        const uint32_t n = kSize * kSize;

        // Accumulate luminance and RGB
        float lum_sum = 0.0f;
        float lum_sq_sum = 0.0f;
        float r_sum = 0.0f, g_sum = 0.0f, b_sum = 0.0f;
        uint32_t hash = 2166136261u;  // FNV-1a offset basis

        for (uint32_t i = 0; i < n; ++i) {
            float r = data[i * 3 + 0];
            float g = data[i * 3 + 1];
            float b = data[i * 3 + 2];

            // FNV-1a hash on quantized RGB (8-bit each)
            uint8_t rb = static_cast<uint8_t>(r * 255.0f);
            uint8_t gb = static_cast<uint8_t>(g * 255.0f);
            uint8_t bb = static_cast<uint8_t>(b * 255.0f);
            hash ^= rb; hash *= 16777619u;
            hash ^= gb; hash *= 16777619u;
            hash ^= bb; hash *= 16777619u;

            float lum = r * 0.299f + g * 0.587f + b * 0.114f;
            lum_sum += lum;
            lum_sq_sum += lum * lum;
            r_sum += r;
            g_sum += g;
            b_sum += b;
        }

        frame_hash_ = static_cast<float>(hash);
        brightness_ = lum_sum / static_cast<float>(n);
        float variance = (lum_sq_sum / static_cast<float>(n)) - (brightness_ * brightness_);
        contrast_ = std::sqrt(std::max(0.0f, variance));

        // Dominant hue from average RGB → HSV hue
        float ar = r_sum / static_cast<float>(n);
        float ag = g_sum / static_cast<float>(n);
        float ab = b_sum / static_cast<float>(n);
        dominant_hue_ = rgb_to_hue(ar, ag, ab);
    }

    // Write metrics into the node's output_values at the given port indices.
    void inject(float* output_values,
                uint32_t hash_idx, uint32_t brightness_idx,
                uint32_t contrast_idx, uint32_t hue_idx) const {
        if (hash_idx != UINT32_MAX)       output_values[hash_idx]       = frame_hash_;
        if (brightness_idx != UINT32_MAX) output_values[brightness_idx] = brightness_;
        if (contrast_idx != UINT32_MAX)   output_values[contrast_idx]   = contrast_;
        if (hue_idx != UINT32_MAX)        output_values[hue_idx]        = dominant_hue_;
    }

    bool is_inited() const { return inited_; }
    float frame_hash()    const { return frame_hash_; }
    float brightness()    const { return brightness_; }
    float contrast()      const { return contrast_; }
    float dominant_hue()  const { return dominant_hue_; }

private:
    static constexpr uint32_t kSize = 4;  // 4x4 pixel readback

    static float rgb_to_hue(float r, float g, float b) {
        float mx = std::max({r, g, b});
        float mn = std::min({r, g, b});
        float d = mx - mn;
        if (d < 1e-6f) return 0.0f;
        float h;
        if (mx == r)      h = std::fmod((g - b) / d, 6.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else              h = (r - g) / d + 4.0f;
        h *= 60.0f;
        if (h < 0.0f) h += 360.0f;
        return h;
    }

    gpu::TextureReadback readback_;
    bool inited_ = false;
    float frame_hash_   = 0.0f;
    float brightness_   = 0.0f;
    float contrast_     = 0.0f;
    float dominant_hue_ = 0.0f;
};

}  // namespace vivid
