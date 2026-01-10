#pragma once

/**
 * @file copy.h
 * @brief Copy/replicate operator (TouchDesigner-style)
 *
 * Creates multiple copies of an input texture with per-copy transforms.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>
#include <map>

namespace vivid::effects {

/**
 * @brief Arrangement modes for copies
 */
enum class CopyMode {
    Linear,   ///< Linear offset with rotation and scale steps
    Radial,   ///< Circular arrangement around center
    Grid      ///< Grid arrangement in rows and columns
};

/**
 * @brief Copy/replicate operator
 *
 * Creates multiple copies of an input texture arranged in linear, radial,
 * or grid patterns. Each copy can have independent transforms and opacity.
 * Useful for geometric patterns, grids, spirals, radial arrays, and trail effects.
 *
 * @par Example (Linear)
 * @code
 * auto& shape = chain.add<Shape>("shape");
 * shape.type = ShapeType::Ellipse;
 *
 * auto& copies = chain.add<Copy>("copies");
 * copies.input("shape");
 * copies.count = 5;
 * copies.offset.set(0.1f, 0.0f);
 * copies.rotationStep = 0.2f;
 * copies.opacityFalloff = 0.15f;
 * @endcode
 *
 * @par Example (Radial)
 * @code
 * auto& radial = chain.add<Copy>("radial");
 * radial.input("shape");
 * radial.mode = CopyMode::Radial;
 * radial.count = 8;
 * radial.radius = 0.3f;
 * @endcode
 *
 * @par Example (Grid)
 * @code
 * auto& grid = chain.add<Copy>("grid");
 * grid.input("shape");
 * grid.mode = CopyMode::Grid;
 * grid.count = 9;
 * grid.columns = 3;
 * grid.spacing.set(0.25f, 0.25f);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture to copy
 *
 * @par Output
 * Texture with all copies composited
 *
 * @note For cases where each copy needs different behavior (audio inputs,
 * colors), use loops in chain.cpp instead.
 */
class Copy : public TextureOperator {
public:
    static constexpr int MAX_COPIES = 16;

    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    EnumParam<CopyMode> mode{"mode", CopyMode::Linear};  ///< Arrangement mode
    Param<int> count{"count", 3, 1, MAX_COPIES};         ///< Number of copies

    // Linear mode parameters
    Vec2Param offset{"offset", 0.1f, 0.0f, -1.0f, 1.0f};           ///< Per-copy offset (x, y)
    Param<float> rotationStep{"rotationStep", 0.0f, -6.28f, 6.28f}; ///< Per-copy rotation (radians)
    Param<float> scaleStep{"scaleStep", 1.0f, 0.5f, 2.0f};          ///< Per-copy scale multiplier

    // Radial mode parameters
    Param<float> radius{"radius", 0.3f, 0.0f, 1.0f};               ///< Distance from center
    Param<float> startAngle{"startAngle", 0.0f, 0.0f, 6.28f};      ///< Starting angle (radians)
    Param<float> endAngle{"endAngle", 6.28f, 0.0f, 6.28f};         ///< Ending angle (radians)

    // Grid mode parameters
    Param<int> columns{"columns", 3, 1, MAX_COPIES};               ///< Number of columns
    Vec2Param spacing{"spacing", 0.2f, 0.2f, 0.0f, 1.0f};          ///< Cell spacing

    // Common parameters
    Param<float> opacityFalloff{"opacityFalloff", 0.0f, 0.0f, 1.0f}; ///< Per-copy opacity decay
    Vec2Param pivot{"pivot", 0.5f, 0.5f, 0.0f, 1.0f};                ///< Transform pivot point

    /// @}
    // -------------------------------------------------------------------------

    Copy() {
        registerParam(mode);
        registerParam(count);
        registerParam(offset);
        registerParam(rotationStep);
        registerParam(scaleStep);
        registerParam(radius);
        registerParam(startAngle);
        registerParam(endAngle);
        registerParam(columns);
        registerParam(spacing);
        registerParam(opacityFalloff);
        registerParam(pivot);
    }
    ~Copy() override;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Copy"; }

    /// @}

private:
    // Shader generation
    std::string generateShader(int copyCount);
    WGPURenderPipeline getOrCreatePipeline(Context& ctx, int copyCount);

    // Transform computation
    void computeTransforms();

    // GPU resources
    std::map<int, WGPURenderPipeline> m_pipelineCache;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Cached transform data (updated each frame)
    struct TransformData {
        float transforms[MAX_COPIES * 16];  // mat4 for alignment (only using 3x3)
        float opacities[MAX_COPIES];
        int32_t count;
        int32_t _pad[3];
    };
    TransformData m_transformData{};

    int m_lastCount = 0;
};

} // namespace vivid::effects
