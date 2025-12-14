#pragma once

/**
 * @file noise.h
 * @brief Fractal noise generator operator
 *
 * Generates animated procedural noise with multiple algorithms and fractal layering.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Noise algorithm types
 */
enum class NoiseType {
    Perlin,     ///< Classic gradient noise - smooth, natural looking
    Simplex,    ///< Improved gradient noise - fewer artifacts, faster
    Worley,     ///< Cellular/Voronoi noise - organic cell patterns
    Value       ///< Simple interpolated random values - blocky, retro
};

/**
 * @brief Fractal noise generator (3D)
 *
 * Generates animated procedural noise textures. Supports multiple noise
 * algorithms and fractal layering (octaves) for detail.
 *
 * @par Example
 * @code
 * auto& noise = chain.add<Noise>("noise");
 * noise.type(NoiseType::Simplex);
 * noise.scale = 4.0f;
 * noise.speed = 0.5f;
 * noise.octaves = 4;
 *
 * // In update(), animate offset:
 * noise.offset.set(0, 0, ctx.time());
 * @endcode
 *
 * @par Inputs
 * None (generator)
 *
 * @par Output
 * Grayscale texture
 */
class Noise : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> scale{"scale", 4.0f, 0.1f, 20.0f};         ///< Noise scale (higher = finer detail)
    Param<float> speed{"speed", 0.5f, 0.0f, 5.0f};          ///< Animation speed
    Param<int> octaves{"octaves", 4, 1, 8};                 ///< Fractal layers
    Param<float> lacunarity{"lacunarity", 2.0f, 1.0f, 4.0f}; ///< Frequency multiplier per octave
    Param<float> persistence{"persistence", 0.5f, 0.0f, 1.0f}; ///< Amplitude multiplier per octave
    Vec3Param offset{"offset", 0.0f, 0.0f, 0.0f, -100.0f, 100.0f}; ///< 3D spatial offset

    /// @}
    // -------------------------------------------------------------------------

    Noise() {
        registerParam(scale);
        registerParam(speed);
        registerParam(octaves);
        registerParam(lacunarity);
        registerParam(persistence);
        registerParam(offset);
    }
    ~Noise() override;

    /// @brief Set noise algorithm
    void type(NoiseType t) { if (m_type != t) { m_type = t; markDirty(); } }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Noise"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    NoiseType m_type = NoiseType::Perlin;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
