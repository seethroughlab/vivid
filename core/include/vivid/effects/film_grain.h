#pragma once

/**
 * @file film_grain.h
 * @brief Photographic film grain effect
 *
 * Adds animated noise simulating the grain texture of analog film stock.
 * Essential for retro/vintage aesthetics.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for FilmGrain effect
struct FilmGrainUniforms {
    float intensity;
    float size;
    float speed;
    float time;
    float colored;
    float _pad1;
    float _pad2;
    float _pad3;
};

/**
 * @brief Photographic film grain effect
 *
 * Adds organic noise that mimics the silver halide crystals in analog film.
 * The grain animates over time and can be monochrome or subtly colored.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | intensity | float | 0-1 | 0.15 | Grain visibility strength |
 * | size | float | 0.5-4 | 1.0 | Grain size (lower = finer) |
 * | speed | float | 0-10 | 24.0 | Animation speed (fps-like) |
 * | colored | float | 0-1 | 0.0 | Color variation: 0=mono, 1=full color |
 *
 * @par Example
 * @code
 * auto& grain = chain.add<FilmGrain>("grain");
 * grain.input("source");
 * grain.intensity = 0.2f;   // Noticeable but not overwhelming
 * grain.size = 1.5f;        // Medium grain
 * grain.colored = 0.3f;     // Slight color variation
 * @endcode
 */
class FilmGrain : public SimpleTextureEffect<FilmGrain, FilmGrainUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> intensity{"intensity", 0.15f, 0.0f, 1.0f};  ///< Grain strength
    Param<float> size{"size", 1.0f, 0.5f, 4.0f};             ///< Grain size (lower = finer)
    Param<float> speed{"speed", 24.0f, 0.0f, 60.0f};         ///< Animation speed
    Param<float> colored{"colored", 0.0f, 0.0f, 1.0f};       ///< 0=mono, 1=colored

    /// @}
    // -------------------------------------------------------------------------

    FilmGrain() {
        registerParam(intensity);
        registerParam(size);
        registerParam(speed);
        registerParam(colored);
    }

    /// @brief Get uniform values for GPU
    FilmGrainUniforms getUniforms() const {
        return {
            intensity,
            size,
            speed,
            m_time,
            colored,
            0.0f, 0.0f, 0.0f
        };
    }

    std::string name() const override { return "FilmGrain"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;

    /// @brief Called each frame to update time
    void process(Context& ctx) override;

    /// @brief Set time for grain animation (used internally)
    void setTime(float t) { m_time = t; }

private:
    mutable float m_time = 0.0f;
};

#ifdef _WIN32
extern template class SimpleTextureEffect<FilmGrain, FilmGrainUniforms>;
#endif

} // namespace vivid::effects
