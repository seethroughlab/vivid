#pragma once

// Vivid Effects 2D - Fluid Simulation Operator
// GPU-accelerated 2D fluid simulation based on Navier-Stokes solver
// Uses semi-Lagrangian advection, pressure projection, and vorticity confinement

#include <vivid/effects/texture_operator.h>
#include <vivid/effects/gpu_handle.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>
#include <vector>

namespace vivid::effects {

/**
 * @brief Force or dye injection point
 *
 * Used to queue force/dye additions for batch processing each frame.
 */
struct FluidImpulse {
    float x, y;           // Position (normalized 0-1)
    float dx, dy;         // Force direction (for forces) or color RG (for dye)
    float radius;         // Splat radius
    float b, a;           // Color BA (for dye only)
    bool isDye;           // true = dye, false = force
};

/**
 * @brief GPU-accelerated 2D fluid simulation
 *
 * Implements an incompressible Navier-Stokes solver using WebGPU compute shaders.
 * The simulation includes:
 * - Semi-Lagrangian advection for velocity and dye transport
 * - Jacobi iteration for pressure solving
 * - Vorticity confinement for swirling detail
 * - Force and dye injection for interaction
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | viscosity | float | 0-0.01 | 0.0001 | Fluid viscosity (higher = more resistance) |
 * | dissipation | float | 0.9-1 | 0.99 | Velocity dissipation per frame |
 * | vorticity | float | 0-1 | 0.3 | Vorticity confinement strength |
 * | dyeDissipation | float | 0.9-1 | 0.98 | Dye/color dissipation per frame |
 * | pressureIterations | int | 10-80 | 40 | Jacobi iterations for pressure solve |
 * | forceScale | float | 0-5 | 1.0 | Multiplier for injected forces |
 *
 * @par Example
 * @code
 * FluidSim* fluid;
 *
 * void setup(Context& ctx) {
 *     fluid = &ctx.chain().add<FluidSim>("fluid");
 *     fluid->viscosity = 0.0001f;
 *     fluid->vorticity = 0.4f;
 *     ctx.chain().output("fluid");
 * }
 *
 * void update(Context& ctx) {
 *     if (ctx.mousePressed()) {
 *         auto [mx, my] = ctx.mousePos();
 *         auto [dx, dy] = ctx.mouseDelta();
 *         fluid->addForce(mx, my, dx * 10, dy * 10);
 *         fluid->addDye(mx, my, 1.0f, 0.5f, 0.2f);
 *     }
 * }
 * @endcode
 *
 * @par Output
 * RGBA texture containing the dye density field
 */
class FluidSim : public TextureOperator {
public:
    FluidSim();
    ~FluidSim() override;

    // =========================================================================
    // Parameters - Simulation
    // =========================================================================

    /// Fluid viscosity (0 = inviscid, higher = more resistance)
    Param<float> viscosity{"viscosity", 0.0001f, 0.0f, 0.01f};

    /// Velocity dissipation per frame (1.0 = no dissipation)
    Param<float> dissipation{"dissipation", 0.99f, 0.9f, 1.0f};

    /// Vorticity confinement strength (higher = more swirls)
    Param<float> vorticity{"vorticity", 0.3f, 0.0f, 1.0f};

    /// Dye dissipation per frame (1.0 = no dissipation)
    Param<float> dyeDissipation{"dyeDissipation", 0.98f, 0.9f, 1.0f};

    /// Number of Jacobi iterations for pressure solve
    Param<int> pressureIterations{"pressureIterations", 40, 10, 80};

    /// Multiplier for injected forces
    Param<float> forceScale{"forceScale", 1.0f, 0.0f, 5.0f};

    // =========================================================================
    // Parameters - Background
    // =========================================================================

    /// Background color (visible where no dye)
    ColorParam clearColor{"clearColor", 0.0f, 0.0f, 0.0f, 1.0f};

    // =========================================================================
    // API Methods
    // =========================================================================

    /**
     * @brief Add force impulse at position
     * @param x X position (normalized 0-1)
     * @param y Y position (normalized 0-1)
     * @param dx Force X component
     * @param dy Force Y component
     * @param radius Splat radius (normalized, default 0.01)
     */
    void addForce(float x, float y, float dx, float dy, float radius = 0.01f);

    /**
     * @brief Add dye at position
     * @param x X position (normalized 0-1)
     * @param y Y position (normalized 0-1)
     * @param r Red component (0-1)
     * @param g Green component (0-1)
     * @param b Blue component (0-1)
     * @param radius Splat radius (normalized, default 0.01)
     * @param a Alpha component (0-1, default 1.0)
     */
    void addDye(float x, float y, float r, float g, float b, float radius = 0.01f, float a = 1.0f);

    /**
     * @brief Clear all simulation fields
     *
     * Resets velocity, pressure, and dye to zero.
     */
    void clear();

    // =========================================================================
    // Operator Interface
    // =========================================================================

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "FluidSim"; }

    // State preservation for hot-reload
    std::unique_ptr<OperatorState> saveState() override;
    void loadState(std::unique_ptr<OperatorState> state) override;

private:
    // =========================================================================
    // GPU Resources - Simulation Textures (ping-pong)
    // =========================================================================

    // Velocity field (RG16Float, ping-pong pair)
    TextureHandle m_velocityA;
    TextureViewHandle m_velocityViewA;
    TextureHandle m_velocityB;
    TextureViewHandle m_velocityViewB;
    int m_velocityRead = 0;  // Index of current read buffer (0=A, 1=B)

    // Pressure field (R16Float)
    TextureHandle m_pressure;
    TextureViewHandle m_pressureView;

    // Divergence field (R16Float)
    TextureHandle m_divergence;
    TextureViewHandle m_divergenceView;

    // Dye/color field (RGBA16Float, ping-pong pair)
    TextureHandle m_dyeA;
    TextureViewHandle m_dyeViewA;
    TextureHandle m_dyeB;
    TextureViewHandle m_dyeViewB;
    int m_dyeRead = 0;  // Index of current read buffer

    // Vorticity field (R16Float, for curl computation)
    TextureHandle m_vorticity;
    TextureViewHandle m_vorticityView;

    // =========================================================================
    // GPU Resources - Pipelines and Bind Groups
    // =========================================================================

    // Compute pipelines
    ComputePipelineHandle m_advectVelocityPipeline;
    ComputePipelineHandle m_advectDyePipeline;
    ComputePipelineHandle m_divergencePipeline;
    ComputePipelineHandle m_pressurePipeline;
    ComputePipelineHandle m_gradientSubtractPipeline;
    ComputePipelineHandle m_vorticityPipeline;
    ComputePipelineHandle m_vorticityForcePipeline;
    ComputePipelineHandle m_addForcePipeline;
    ComputePipelineHandle m_addDyePipeline;
    ComputePipelineHandle m_clearPipeline;

    // Render pipeline (for outputting dye to texture)
    RenderPipelineHandle m_renderPipeline;
    BindGroupLayoutHandle m_renderBindGroupLayout;

    // Uniform buffers
    BufferHandle m_uniformBuffer;
    BufferHandle m_impulseBuffer;

    // Sampler
    WGPUSampler m_sampler = nullptr;

    // Bind group layouts
    BindGroupLayoutHandle m_advectLayout;
    BindGroupLayoutHandle m_divergenceLayout;
    BindGroupLayoutHandle m_pressureLayout;
    BindGroupLayoutHandle m_gradientLayout;
    BindGroupLayoutHandle m_vorticityLayout;
    BindGroupLayoutHandle m_vorticityForceLayout;
    BindGroupLayoutHandle m_addForceLayout;
    BindGroupLayoutHandle m_addDyeLayout;
    BindGroupLayoutHandle m_clearLayout;

    // =========================================================================
    // State
    // =========================================================================

    std::vector<FluidImpulse> m_pendingImpulses;
    bool m_clearPending = false;
    int m_simWidth = 0;
    int m_simHeight = 0;

    // =========================================================================
    // Helper Methods
    // =========================================================================

    void createTextures(WGPUDevice device);
    void createPipelines(WGPUDevice device);
    void createAdvectPipeline(WGPUDevice device);
    void createDivergencePipeline(WGPUDevice device);
    void createPressurePipeline(WGPUDevice device);
    void createGradientSubtractPipeline(WGPUDevice device);
    void createVorticityPipelines(WGPUDevice device);
    void createAddForcePipeline(WGPUDevice device);
    void createClearPipeline(WGPUDevice device);
    void createRenderPipeline(WGPUDevice device);

    void dispatchAdvection(Context& ctx, float dt);
    void dispatchDivergence(Context& ctx);
    void dispatchPressureSolve(Context& ctx);
    void dispatchGradientSubtract(Context& ctx);
    void dispatchVorticity(Context& ctx, float dt);
    void dispatchImpulses(Context& ctx);
    void dispatchClear(Context& ctx);
    void renderDye(Context& ctx);

    WGPUTextureView velocityReadView() const {
        return m_velocityRead == 0 ? m_velocityViewA.get() : m_velocityViewB.get();
    }
    WGPUTextureView velocityWriteView() const {
        return m_velocityRead == 0 ? m_velocityViewB.get() : m_velocityViewA.get();
    }
    void swapVelocity() { m_velocityRead = 1 - m_velocityRead; }

    WGPUTextureView dyeReadView() const {
        return m_dyeRead == 0 ? m_dyeViewA.get() : m_dyeViewB.get();
    }
    WGPUTextureView dyeWriteView() const {
        return m_dyeRead == 0 ? m_dyeViewB.get() : m_dyeViewA.get();
    }
    void swapDye() { m_dyeRead = 1 - m_dyeRead; }
};

} // namespace vivid::effects
