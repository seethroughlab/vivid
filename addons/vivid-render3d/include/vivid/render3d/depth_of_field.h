#pragma once

/**
 * @file depth_of_field.h
 * @brief Depth of field post-processing effect
 *
 * Uses the depth buffer from Render3D to apply realistic DOF blur.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::render3d {

// Forward declaration
class Render3D;

/**
 * @brief Depth of field post-processing effect
 *
 * Applies depth-based blur to create realistic depth of field. Objects at the
 * focus distance remain sharp while objects closer or farther get progressively blurred.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | focusDistance | float | 0-1 | 0.5 | Normalized focus depth (0=near, 1=far) |
 * | focusRange | float | 0-1 | 0.1 | Range around focus that stays sharp |
 * | blurStrength | float | 0-1 | 0.5 | Maximum blur amount |
 *
 * @par Example
 * @code
 * auto& render = chain.add<Render3D>("render")
 *     .input(&scene)
 *     .depthOutput(true);  // Enable depth output
 *
 * auto& dof = chain.add<DepthOfField>("dof")
 *     .input(&render)  // Takes color and depth from Render3D
 *     .focusDistance(0.3f)
 *     .focusRange(0.1f)
 *     .blurStrength(0.8f);
 * @endcode
 */
class DepthOfField : public effects::TextureOperator {
public:
    DepthOfField() = default;
    ~DepthOfField() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input from Render3D (uses both color and depth output)
     * @param render Render3D operator with depthOutput(true)
     * @return Reference for chaining
     */
    DepthOfField& input(Render3D* render);

    /**
     * @brief Set normalized focus distance (0 = near plane, 1 = far plane)
     * @param d Focus depth (0-1, default 0.5)
     * @return Reference for chaining
     */
    DepthOfField& focusDistance(float d) {
        if (m_focusDistance != d) {
            m_focusDistance = d;
            markDirty();
        }
        return *this;
    }

    /**
     * @brief Set focus range (depth range that stays sharp)
     * @param r Focus range (0-1, default 0.1)
     * @return Reference for chaining
     */
    DepthOfField& focusRange(float r) {
        if (m_focusRange != r) {
            m_focusRange = r;
            markDirty();
        }
        return *this;
    }

    /**
     * @brief Set maximum blur strength
     * @param s Blur strength (0-1, default 0.5)
     * @return Reference for chaining
     */
    DepthOfField& blurStrength(float s) {
        if (m_blurStrength != s) {
            m_blurStrength = s;
            markDirty();
        }
        return *this;
    }

    /**
     * @brief Enable debug mode to visualize depth buffer
     * @param show true to show depth as grayscale, false for normal DOF
     * @return Reference for chaining
     */
    DepthOfField& showDepth(bool show) {
        if (m_showDepth != show) {
            m_showDepth = show;
            markDirty();
        }
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "DepthOfField"; }

    std::vector<ParamDecl> params() override {
        return { m_focusDistance.decl(), m_focusRange.decl(), m_blurStrength.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "focusDistance") { out[0] = m_focusDistance; return true; }
        if (name == "focusRange") { out[0] = m_focusRange; return true; }
        if (name == "blurStrength") { out[0] = m_blurStrength; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "focusDistance") { focusDistance(value[0]); return true; }
        if (name == "focusRange") { focusRange(value[0]); return true; }
        if (name == "blurStrength") { blurStrength(value[0]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Render3D* m_render3d = nullptr;

    Param<float> m_focusDistance{"focusDistance", 0.5f, 0.0f, 1.0f};
    Param<float> m_focusRange{"focusRange", 0.1f, 0.0f, 1.0f};
    Param<float> m_blurStrength{"blurStrength", 0.5f, 0.0f, 1.0f};
    bool m_showDepth = false;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::render3d
