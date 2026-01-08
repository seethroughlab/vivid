#pragma once

/**
 * @file fog.h
 * @brief Fog post-processing effect
 *
 * Uses the depth buffer from Render3D to apply distance-based fog.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>

namespace vivid::render3d {

// Forward declaration
class Render3D;

/**
 * @brief Fog mode enumeration
 */
enum class FogMode {
    Linear = 0,       ///< Linear interpolation between start and end
    Exponential = 1,  ///< Exponential falloff (e^-density*dist)
    ExponentialSquared = 2  ///< Exponential squared (e^-(density*dist)^2)
};

/**
 * @brief Fog post-processing effect
 *
 * Applies depth-based fog to create atmospheric perspective. Objects farther
 * from the camera blend toward the fog color.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | fogColorR | float | 0-1 | 0.5 | Fog color red component |
 * | fogColorG | float | 0-1 | 0.5 | Fog color green component |
 * | fogColorB | float | 0-1 | 0.6 | Fog color blue component |
 * | fogStart | float | 0-1000 | 10 | Distance where fog begins (linear mode) |
 * | fogEnd | float | 0-1000 | 100 | Distance where fog is fully opaque (linear mode) |
 * | fogDensity | float | 0-1 | 0.02 | Density for exponential modes |
 *
 * @par Example
 * @code
 * auto& render = chain.add<Render3D>("render");
 * render.setDepthOutput(true);  // Enable depth output
 *
 * auto& fog = chain.add<Fog>("fog");
 * fog.input(&render);
 * fog.fogColor = {0.7f, 0.75f, 0.8f};  // Light gray-blue
 * fog.fogStart = 5.0f;
 * fog.fogEnd = 50.0f;
 * fog.fogMode = FogMode::Linear;
 * @endcode
 
 * @see Render3D, DepthOfField, CameraOperator
 */
class Fog : public effects::TextureOperator {
public:
    static OperatorDescriptor describe() {
        return OperatorDescriptor("Fog", "3D Post-Processing", "Depth-based atmospheric fog effect")
            .output(OutputKind::Texture)
            .requireInput()
            .withAliases({"AtmosphericFog", "DistanceFog"})
            .withUsage(
                "auto& fog = chain.add<Fog>(\"fog\");\n"
                "fog.input(&render);  // Requires depthOutput(true)\n"
                "fog.fogColor[0] = 0.7f;  // RGB\n"
                "fog.fogStart = 5.0f;\n"
                "fog.fogEnd = 50.0f;\n"
            );
    }

    Fog() = default;
    ~Fog() override;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set input from Render3D (uses both color and depth output)
     * @param render Render3D operator with setDepthOutput(true)
     */
    void input(Render3D* render);

    /// Fog color (RGB, 0-1 range)
    float fogColor[3] = {0.5f, 0.5f, 0.6f};

    /// Fog start distance (for linear mode)
    Param<float> fogStart{"fogStart", 10.0f, 0.0f, 1000.0f};

    /// Fog end distance (for linear mode)
    Param<float> fogEnd{"fogEnd", 100.0f, 0.0f, 1000.0f};

    /// Fog density (for exponential modes)
    Param<float> fogDensity{"fogDensity", 0.02f, 0.0f, 1.0f};

    /// Fog mode (linear, exponential, or exponential squared)
    FogMode fogMode = FogMode::Linear;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Fog"; }

    std::vector<ParamDecl> params() override {
        return {
            fogStart.decl(),
            fogEnd.decl(),
            fogDensity.decl()
        };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "fogStart") { out[0] = fogStart; return true; }
        if (name == "fogEnd") { out[0] = fogEnd; return true; }
        if (name == "fogDensity") { out[0] = fogDensity; return true; }
        if (name == "fogColorR") { out[0] = fogColor[0]; return true; }
        if (name == "fogColorG") { out[0] = fogColor[1]; return true; }
        if (name == "fogColorB") { out[0] = fogColor[2]; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "fogStart") { fogStart = value[0]; markDirty(); return true; }
        if (name == "fogEnd") { fogEnd = value[0]; markDirty(); return true; }
        if (name == "fogDensity") { fogDensity = value[0]; markDirty(); return true; }
        if (name == "fogColorR") { fogColor[0] = value[0]; markDirty(); return true; }
        if (name == "fogColorG") { fogColor[1] = value[0]; markDirty(); return true; }
        if (name == "fogColorB") { fogColor[2] = value[0]; markDirty(); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Render3D* m_render3d = nullptr;

    // Near/far planes from the renderer's camera
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
};

} // namespace vivid::render3d
