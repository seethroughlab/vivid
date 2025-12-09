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
 * @brief Fractal noise generator
 *
 * Generates animated procedural noise textures. Supports multiple noise
 * algorithms and fractal layering (octaves) for detail.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | scale | float | 0.1-20 | 4.0 | Noise scale (higher = finer detail) |
 * | speed | float | 0-5 | 0.5 | Animation speed |
 * | octaves | int | 1-8 | 4 | Fractal layers |
 * | lacunarity | float | 1-4 | 2.0 | Frequency multiplier per octave |
 * | persistence | float | 0-1 | 0.5 | Amplitude multiplier per octave |
 *
 * @par Example
 * @code
 * chain->add<Noise>("noise")
 *     .scale(4.0f)
 *     .speed(0.5f)
 *     .type(NoiseType::Simplex)
 *     .octaves(4);
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
    Noise() = default;
    ~Noise() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set noise algorithm
     * @param t Noise type (Perlin, Simplex, Worley, Value)
     * @return Reference for chaining
     */
    Noise& type(NoiseType t) { m_type = t; return *this; }

    /**
     * @brief Set noise scale
     * @param s Scale factor (0.1-20, default 4.0)
     * @return Reference for chaining
     */
    Noise& scale(float s) { m_scale = s; return *this; }

    /**
     * @brief Set animation speed
     * @param s Speed (0-5, default 0.5)
     * @return Reference for chaining
     */
    Noise& speed(float s) { m_speed = s; return *this; }

    /**
     * @brief Set number of fractal octaves
     * @param o Octaves (1-8, default 4)
     * @return Reference for chaining
     */
    Noise& octaves(int o) { m_octaves = o; return *this; }

    /**
     * @brief Set lacunarity (frequency multiplier per octave)
     * @param l Lacunarity (1-4, default 2.0)
     * @return Reference for chaining
     */
    Noise& lacunarity(float l) { m_lacunarity = l; return *this; }

    /**
     * @brief Set persistence (amplitude multiplier per octave)
     * @param p Persistence (0-1, default 0.5)
     * @return Reference for chaining
     */
    Noise& persistence(float p) { m_persistence = p; return *this; }

    /**
     * @brief Set UV offset
     * @param x X offset
     * @param y Y offset
     * @return Reference for chaining
     */
    Noise& offset(float x, float y) { m_offset.set(x, y); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Noise"; }

    std::vector<ParamDecl> params() override {
        return { m_scale.decl(), m_speed.decl(), m_octaves.decl(),
                 m_lacunarity.decl(), m_persistence.decl(), m_offset.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "scale") { out[0] = m_scale; return true; }
        if (name == "speed") { out[0] = m_speed; return true; }
        if (name == "octaves") { out[0] = m_octaves; return true; }
        if (name == "lacunarity") { out[0] = m_lacunarity; return true; }
        if (name == "persistence") { out[0] = m_persistence; return true; }
        if (name == "offset") { out[0] = m_offset.x(); out[1] = m_offset.y(); return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "scale") { m_scale = value[0]; return true; }
        if (name == "speed") { m_speed = value[0]; return true; }
        if (name == "octaves") { m_octaves = static_cast<int>(value[0]); return true; }
        if (name == "lacunarity") { m_lacunarity = value[0]; return true; }
        if (name == "persistence") { m_persistence = value[0]; return true; }
        if (name == "offset") { m_offset.set(value[0], value[1]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    // Parameters
    NoiseType m_type = NoiseType::Perlin;
    Param<float> m_scale{"scale", 4.0f, 0.1f, 20.0f};
    Param<float> m_speed{"speed", 0.5f, 0.0f, 5.0f};
    Param<int> m_octaves{"octaves", 4, 1, 8};
    Param<float> m_lacunarity{"lacunarity", 2.0f, 1.0f, 4.0f};
    Param<float> m_persistence{"persistence", 0.5f, 0.0f, 1.0f};
    Vec2Param m_offset{"offset", 0.0f, 0.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
