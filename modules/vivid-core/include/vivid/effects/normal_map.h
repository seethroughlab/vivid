#pragma once

/**
 * @file normal_map.h
 * @brief Height to normal map conversion operator
 *
 * Generates a normal map from a height/displacement texture using Sobel gradients.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>

namespace vivid::effects {

/**
 * @brief Height to normal map conversion effect
 *
 * Converts a grayscale height map to an RGB normal map suitable for
 * 3D lighting calculations. Uses a Sobel filter to compute surface gradients.
 *
 * The output encodes normals as RGB where:
 * - R = X component (0.5 = flat, <0.5 = left, >0.5 = right)
 * - G = Y component (0.5 = flat, <0.5 = down, >0.5 = up, or flipped)
 * - B = Z component (always points toward viewer for tangent space)
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | strength | float | 0-10 | 1.0 | Normal intensity (higher = steeper) |
 * | flipY | float | 0-1 | 0.0 | Flip Y axis (0 = OpenGL, 1 = DirectX) |
 *
 * @par Example
 * @code
 * auto& normal = chain.add<NormalMap>("normal");
 * normal.input(&heightMap);
 * normal.strength = 2.0f;  // Strong normals
 * normal.flipY = 0.0f;     // OpenGL convention
 * @endcode
 *
 * @par Inputs
 * - Input 0: Grayscale height map texture
 *
 * @par Output
 * RGB normal map (tangent space, 0-1 encoded)
 *
 * @par TouchDesigner Equivalent
 * Normal Map TOP
 */
class NormalMap : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> strength{"strength", 1.0f, 0.0f, 10.0f};  ///< Normal intensity
    Param<float> flipY{"flipY", 0.0f, 0.0f, 1.0f};         ///< Flip Y axis

    /// @}
    // -------------------------------------------------------------------------

    NormalMap() {
        registerParam(strength);
        registerParam(flipY);
    }
    ~NormalMap() override;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "NormalMap"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
};

} // namespace vivid::effects
