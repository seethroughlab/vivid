#pragma once

/**
 * @file time_machine.h
 * @brief Temporal displacement effect using frame cache
 *
 * Samples from a FrameCache using a grayscale displacement map to create
 * temporal displacement effects where different parts of the image show
 * different points in time.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/effects/frame_cache.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Temporal displacement using grayscale control
 *
 * Samples from a FrameCache based on a grayscale displacement map.
 * Dark pixels show older frames, bright pixels show newer frames.
 * Creates effects like slit-scan, time displacement, and temporal echoes.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | depth | float | 0-1 | 1.0 | How deep into the cache to reach (0=current only, 1=full range) |
 * | offset | float | 0-1 | 0.0 | Bias offset for displacement lookup |
 * | invert | bool | 0-1 | 0 | Invert displacement (bright=old, dark=new) |
 *
 * @par Example
 * @code
 * auto& video = chain.add<VideoPlayer>("video");
 * auto& cache = chain.add<FrameCache>("cache");
 * auto& gradient = chain.add<Gradient>("gradient");  // Vertical gradient
 * auto& timeMachine = chain.add<TimeMachine>("timeMachine");
 *
 * cache.input(&video);
 * cache.frameCount = 64;
 *
 * timeMachine.cache(&cache);
 * timeMachine.displacementMap(&gradient);
 * timeMachine.depth = 1.0f;
 * @endcode
 *
 * @par Inputs
 * - cache(): FrameCache operator with cached frames
 * - displacementMap(): Grayscale texture controlling time selection
 *
 * @par Output
 * Temporally displaced texture
 */
class TimeMachine : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> depth{"depth", 1.0f, 0.0f, 1.0f};      ///< How deep into cache to reach
    Param<float> offset{"offset", 0.0f, 0.0f, 1.0f};    ///< Bias offset for lookup
    Param<bool> invert{"invert", false};                 ///< Invert displacement direction

    /// @}
    // -------------------------------------------------------------------------

    TimeMachine() {
        registerParam(depth);
        registerParam(offset);
        registerParam(invert);
    }
    ~TimeMachine() override;

    /// @brief Set the frame cache source
    void cache(FrameCache* op) {
        m_frameCache = op;
        setInput(0, op);
    }

    /// @brief Set the displacement map (grayscale controls time)
    void displacementMap(TextureOperator* op) { setInput(1, op); }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "TimeMachine"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    // Reference to frame cache
    FrameCache* m_frameCache = nullptr;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
};

} // namespace vivid::effects
