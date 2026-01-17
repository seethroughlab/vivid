#pragma once

/**
 * @file noise.h
 * @brief Fractal noise generator operator
 *
 * Generates animated procedural noise with multiple algorithms and fractal layering.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>

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
 * noise.type = NoiseType::Simplex;
 * noise.scale = 4.0f;
 * noise.speed = 0.5f;
 * noise.octaves = 4;
 *
 * // RGB noise (3 independent channels)
 * noise.colorNoise = true;
 *
 * // Scale from corner instead of center
 * noise.centerOrigin = false;
 * @endcode
 *
 * @par Inputs
 * None (generator)
 *
 * @par Output
 * Grayscale texture (or RGB when colorNoise=true)
 *
 * @see Gradient, Displace, FBM
 */
class Noise : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    EnumParam<NoiseType> type{"type", NoiseType::Perlin};    ///< Noise algorithm type
    Param<float> scale{"scale", 4.0f, 0.1f, 20.0f};         ///< Noise scale (higher = finer detail)
    Param<float> speed{"speed", 0.5f, 0.0f, 5.0f};          ///< Animation speed
    Param<int> octaves{"octaves", 4, 1, 8};                 ///< Fractal layers
    Param<float> lacunarity{"lacunarity", 2.0f, 1.0f, 4.0f}; ///< Frequency multiplier per octave
    Param<float> persistence{"persistence", 0.5f, 0.0f, 1.0f}; ///< Amplitude multiplier per octave
    Vec3Param offset{"offset", 0.0f, 0.0f, 0.0f, -100.0f, 100.0f}; ///< 3D spatial offset
    Param<bool> colorNoise{"colorNoise", false};            ///< When true, R/G/B are independent noise fields
    Param<bool> centerOrigin{"centerOrigin", true};         ///< Scale from center instead of corner (default: true)

    /// @}
    // -------------------------------------------------------------------------

    Noise() {
        registerParam(type);
        registerParam(scale);
        registerParam(speed);
        registerParam(octaves);
        registerParam(lacunarity);
        registerParam(persistence);
        registerParam(offset);
        registerParam(colorNoise);
        registerParam(centerOrigin);
    }
    ~Noise() override;

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

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

};

} // namespace vivid::effects
