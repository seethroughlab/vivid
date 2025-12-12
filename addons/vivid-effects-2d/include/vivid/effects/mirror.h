#pragma once

/**
 * @file mirror.h
 * @brief Mirror and kaleidoscope operator
 *
 * Applies axis mirroring and radial kaleidoscope effects.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Mirror mode types
 */
enum class MirrorMode {
    Horizontal,     ///< Left-right mirror
    Vertical,       ///< Top-bottom mirror
    Quad,           ///< Both axes (4 quadrants)
    Kaleidoscope    ///< Radial symmetry with segments
};

/**
 * @brief Mirror and kaleidoscope effects
 *
 * Applies various mirroring effects including simple axis mirroring
 * and kaleidoscope-style radial symmetry.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | segments | int | 2-32 | 6 | Kaleidoscope segment count |
 * | angle | float | -2π to 2π | 0.0 | Rotation angle (kaleidoscope) |
 * | center | vec2 | 0-1 | (0.5,0.5) | Mirror center point |
 *
 * @par Example
 * @code
 * chain.add<Mirror>("kaleido")
 *     .input("source")
 *     .mode(MirrorMode::Kaleidoscope)
 *     .segments(8)
 *     .angle(time * 0.1f);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Mirrored texture
 */
class Mirror : public TextureOperator {
public:
    Mirror() = default;
    ~Mirror() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Mirror& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set mirror mode
     * @param m Mirror mode (Horizontal, Vertical, Quad, Kaleidoscope)
     * @return Reference for chaining
     */
    Mirror& mode(MirrorMode m) {
        if (m_mode != m) { m_mode = m; markDirty(); }
        return *this;
    }

    /**
     * @brief Set kaleidoscope segment count
     * @param s Segments (2-32, default 6)
     * @return Reference for chaining
     */
    Mirror& segments(int s) {
        if (m_segments != s) { m_segments = s; markDirty(); }
        return *this;
    }

    /**
     * @brief Set rotation angle
     * @param a Angle in radians
     * @return Reference for chaining
     */
    Mirror& angle(float a) {
        if (m_angle != a) { m_angle = a; markDirty(); }
        return *this;
    }

    /**
     * @brief Set mirror center point
     * @param x X position (0-1)
     * @param y Y position (0-1)
     * @return Reference for chaining
     */
    Mirror& center(float x, float y) {
        if (m_center.x() != x || m_center.y() != y) { m_center.set(x, y); markDirty(); }
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Mirror"; }

    std::vector<ParamDecl> params() override {
        return { m_segments.decl(), m_angle.decl(), m_center.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "segments") { out[0] = m_segments; return true; }
        if (name == "angle") { out[0] = m_angle; return true; }
        if (name == "center") { out[0] = m_center.x(); out[1] = m_center.y(); return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "segments") { segments(static_cast<int>(value[0])); return true; }
        if (name == "angle") { angle(value[0]); return true; }
        if (name == "center") { center(value[0], value[1]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    MirrorMode m_mode = MirrorMode::Horizontal;
    Param<int> m_segments{"segments", 6, 2, 32};
    Param<float> m_angle{"angle", 0.0f, -6.28f, 6.28f};
    Vec2Param m_center{"center", 0.5f, 0.5f, 0.0f, 1.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
