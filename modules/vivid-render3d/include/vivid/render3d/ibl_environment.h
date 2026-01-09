#pragma once

#include <vivid/operator.h>
#include <vivid/operator_registry.h>
#include <webgpu/webgpu.h>
#include <string>

namespace vivid::render3d {

/// Cubemap data for IBL (internal use)
struct CubemapData {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;      // Cube view (for sampling)
    int size = 0;
    int mipLevels = 1;

    [[nodiscard]] bool valid() const { return texture != nullptr; }
};

/**
 * @brief Image-based lighting from HDR environment maps
 *
 * Provides physically-based environment lighting through pre-computed cubemaps:
 * irradiance for diffuse lighting, pre-filtered radiance for specular reflections,
 * and BRDF lookup table for split-sum approximation. Load an HDR environment map
 * or use the default procedural sky.
 *
 * @par Example
 * @code
 * auto& ibl = chain.add<IBLEnvironment>("ibl");
 * ibl.setHdrFile("assets/hdris/studio.hdr");
 *
 * auto& render = chain.add<Render3D>("render");
 * render.setEnvironmentInput(&ibl);
 * render.setIbl(true);
 * render.setShowSkybox(true);  // Optional: show environment as background
 * @endcode
 *
 * @see Render3D, DirectionalLight, PBR
 */
class IBLEnvironment : public Operator {
public:
    static OperatorDescriptor describe() {
        return OperatorDescriptor("IBLEnvironment", "3D Lighting", "HDRI environment map for image-based lighting")
            .output(OutputKind::Value)
            .withAliases({"HDRI", "Skybox", "EnvironmentMap"})
            .withUsage(
                "auto& ibl = chain.add<IBLEnvironment>(\"ibl\");\n"
                "ibl.setHdrFile(\"assets/hdris/studio.hdr\");\n"
                "\n"
                "auto& render = chain.add<Render3D>(\"render\");\n"
                "render.setEnvironmentInput(&ibl);\n"
                "render.setIbl(true);\n"
            );
    }

    IBLEnvironment();
    ~IBLEnvironment() override;

    // Non-copyable
    IBLEnvironment(const IBLEnvironment&) = delete;
    IBLEnvironment& operator=(const IBLEnvironment&) = delete;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /// Set HDR environment map file path
    void setHdrFile(const std::string& path);

    /// Use default procedural sky environment (called if no hdrFile set)
    void setUseDefault();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "IBLEnvironment"; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Legacy API (for backward compatibility)
    /// @{

    /// Load an HDR equirectangular image and generate IBL cubemaps
    /// @deprecated Use hdrFile() fluent setter instead
    bool loadHDR(Context& ctx, const std::string& hdrPath);

    /// Load a default procedural environment
    /// @deprecated Use useDefault() fluent setter instead
    bool loadDefault(Context& ctx);

    /// Initialize pipelines (called automatically by loadHDR/loadDefault)
    bool initPipelines(Context& ctx);

    /// Check if environment is loaded and ready
    [[nodiscard]] bool isLoaded() const { return m_irradianceMap.valid(); }

    /// Check if processor is initialized
    [[nodiscard]] bool isInitialized() const { return m_initialized; }

    // IBL texture accessors (for Render3D)
    [[nodiscard]] WGPUTextureView irradianceView() const;
    [[nodiscard]] WGPUTextureView prefilteredView() const;
    [[nodiscard]] WGPUTextureView brdfLUTView() const;

    // Configuration
    static constexpr int CUBEMAP_SIZE = 512;       // Source environment cubemap
    static constexpr int IRRADIANCE_SIZE = 64;     // Diffuse irradiance
    static constexpr int PREFILTER_SIZE = 256;     // Specular prefilter base
    static constexpr int PREFILTER_MIP_LEVELS = 5; // Roughness levels
    static constexpr int BRDF_LUT_SIZE = 256;      // BRDF lookup table

private:
    bool createPipelines();
    void destroyPipelines();

    CubemapData createCubemap(int size, int mipLevels, bool hdr);
    void destroyCubemap(CubemapData& cubemap);

    CubemapData equirectangularToCubemap(const float* hdrPixels, int width, int height, int cubemapSize);
    CubemapData computeIrradiance(const CubemapData& envCubemap, int size);
    CubemapData computeRadiance(const CubemapData& envCubemap, int size, int mipLevels);
    bool createBRDFLUT(int size);

    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;

    // IBL cubemaps
    CubemapData m_irradianceMap;   // Diffuse IBL
    CubemapData m_prefilteredMap;  // Specular IBL with roughness mips

    // BRDF LUT (shared, environment-independent)
    WGPUTexture m_brdfLUT = nullptr;
    WGPUTextureView m_brdfLUTView = nullptr;

    // Compute pipelines
    WGPUComputePipeline m_equirectPipeline = nullptr;
    WGPUComputePipeline m_irradiancePipeline = nullptr;
    WGPUComputePipeline m_radiancePipeline = nullptr;
    WGPUComputePipeline m_brdfPipeline = nullptr;

    // Bind group layouts
    WGPUBindGroupLayout m_equirectLayout = nullptr;
    WGPUBindGroupLayout m_irradianceLayout = nullptr;
    WGPUBindGroupLayout m_radianceLayout = nullptr;
    WGPUBindGroupLayout m_brdfLayout = nullptr;

    // Sampler for cubemap processing
    WGPUSampler m_cubemapSampler = nullptr;

    // Fluent API state
    std::string m_hdrPath;
    bool m_needsLoad = true;
    bool m_useDefaultEnv = true;  // Default to procedural sky
};

} // namespace vivid::render3d
