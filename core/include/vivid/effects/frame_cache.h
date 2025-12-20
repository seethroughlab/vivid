#pragma once

/**
 * @file frame_cache.h
 * @brief Frame caching operator for temporal effects
 *
 * Caches N frames in a 2D texture array for use with TimeMachine and other
 * temporal displacement effects.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Caches N frames for temporal effects
 *
 * Stores a rolling history of frames in a 2D texture array. Each frame,
 * the oldest frame is replaced with the newest input. The cache can be
 * sampled by the TimeMachine operator for temporal displacement effects.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | frameCount | int | 2-128 | 32 | Number of frames to cache |
 *
 * @par Example
 * @code
 * auto& video = chain.add<VideoPlayer>("video");
 * auto& cache = chain.add<FrameCache>("cache");
 * cache.input(&video);
 * cache.frameCount = 64;  // Cache 64 frames (~2 seconds at 30fps)
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture (video, camera, etc.)
 *
 * @par Output
 * Current frame (pass-through) - use cacheView() for array access
 */
class FrameCache : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<int> frameCount{"frameCount", 32, 2, 128};  ///< Number of frames to cache

    /// @}
    // -------------------------------------------------------------------------

    FrameCache() {
        registerParam(frameCount);
    }
    ~FrameCache() override;

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    /// @brief Get the 2D array texture view for temporal sampling
    WGPUTextureView cacheView() const { return m_cacheView; }

    /// @brief Get the raw cache texture (for binding)
    WGPUTexture cacheTexture() const { return m_cacheTexture; }

    /// @brief Get current write index (most recent frame)
    int currentIndex() const { return m_writeIndex; }

    /// @brief Get actual allocated frame count
    int allocatedFrames() const { return m_allocatedFrames; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "FrameCache"; }

    /// @}

private:
    void createCacheTexture(Context& ctx);
    void createBlitPipeline(Context& ctx);
    void recreateCacheIfNeeded(Context& ctx);
    void blitToTarget(Context& ctx, WGPUTextureView srcView, WGPUTextureView dstView);

    // Cache texture (2D array)
    WGPUTexture m_cacheTexture = nullptr;
    WGPUTextureView m_cacheView = nullptr;  // Full array view

    // Per-layer views for rendering
    std::vector<WGPUTextureView> m_layerViews;

    // Blit pipeline for format conversion
    WGPURenderPipeline m_blitPipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUSampler m_sampler = nullptr;

    int m_writeIndex = 0;           // Current write position (ring buffer)
    int m_allocatedFrames = 0;      // Actual allocated frame count
    int m_framesWritten = 0;        // Total frames written (for warm-up)
};

} // namespace vivid::effects
