#pragma once

/**
 * @file transform.h
 * @brief 2D transformation operator
 *
 * Scale, rotate, and translate textures with configurable pivot point.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief 2D texture transformation
 *
 * Applies scale, rotation, and translation transformations around a
 * configurable pivot point. Useful for repositioning, zooming, and
 * rotating textures.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | scale | vec2 | 0-10 | (1,1) | Scale factor (x, y) |
 * | rotation | float | -2π to 2π | 0.0 | Rotation in radians |
 * | translate | vec2 | -2 to 2 | (0,0) | Translation offset |
 * | pivot | vec2 | 0-1 | (0.5,0.5) | Transform pivot point |
 *
 * @par Example
 * @code
 * chain.add<Transform>("xform")
 *     .input("source")
 *     .scale(1.5f)
 *     .rotate(0.785f)      // 45 degrees
 *     .pivot(0.5f, 0.5f);  // Center pivot
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Transformed texture
 */
class Transform : public TextureOperator {
public:
    Transform() = default;
    ~Transform() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Transform& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set uniform scale
     * @param s Scale factor applied to both axes
     * @return Reference for chaining
     */
    Transform& scale(float s) {
        if (m_scale.x() != s || m_scale.y() != s) { m_scale.set(s, s); markDirty(); }
        return *this;
    }

    /**
     * @brief Set non-uniform scale
     * @param x X scale factor
     * @param y Y scale factor
     * @return Reference for chaining
     */
    Transform& scale(float x, float y) {
        if (m_scale.x() != x || m_scale.y() != y) { m_scale.set(x, y); markDirty(); }
        return *this;
    }

    /**
     * @brief Set rotation angle
     * @param radians Rotation in radians
     * @return Reference for chaining
     */
    Transform& rotate(float radians) {
        if (m_rotation != radians) { m_rotation = radians; markDirty(); }
        return *this;
    }

    /**
     * @brief Set translation offset
     * @param x X offset in UV space
     * @param y Y offset in UV space
     * @return Reference for chaining
     */
    Transform& translate(float x, float y) {
        if (m_translate.x() != x || m_translate.y() != y) { m_translate.set(x, y); markDirty(); }
        return *this;
    }

    /**
     * @brief Set transform pivot point
     * @param x X pivot (0 = left, 0.5 = center, 1 = right)
     * @param y Y pivot (0 = bottom, 0.5 = center, 1 = top)
     * @return Reference for chaining
     */
    Transform& pivot(float x, float y) {
        if (m_pivot.x() != x || m_pivot.y() != y) { m_pivot.set(x, y); markDirty(); }
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Transform"; }

    std::vector<ParamDecl> params() override {
        return { m_scale.decl(), m_rotation.decl(), m_translate.decl(), m_pivot.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "scale") { out[0] = m_scale.x(); out[1] = m_scale.y(); return true; }
        if (name == "rotation") { out[0] = m_rotation; return true; }
        if (name == "translate") { out[0] = m_translate.x(); out[1] = m_translate.y(); return true; }
        if (name == "pivot") { out[0] = m_pivot.x(); out[1] = m_pivot.y(); return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "scale") { scale(value[0], value[1]); return true; }
        if (name == "rotation") { rotate(value[0]); return true; }
        if (name == "translate") { translate(value[0], value[1]); return true; }
        if (name == "pivot") { pivot(value[0], value[1]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Vec2Param m_scale{"scale", 1.0f, 1.0f, 0.0f, 10.0f};
    Param<float> m_rotation{"rotation", 0.0f, -6.28f, 6.28f};
    Vec2Param m_translate{"translate", 0.0f, 0.0f, -2.0f, 2.0f};
    Vec2Param m_pivot{"pivot", 0.5f, 0.5f, 0.0f, 1.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
