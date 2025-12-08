#pragma once

#include <webgpu/webgpu.h>
#include <string>

namespace vivid {
class Context;
}

namespace vivid::render3d {

/// Cubemap data for IBL (internal use)
struct CubemapData {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;      // Cube view (for sampling)
    int size = 0;
    int mipLevels = 1;

    bool valid() const { return texture != nullptr; }
};

/// Image-Based Lighting environment
///
/// Provides environment lighting through pre-computed cubemaps:
/// - Irradiance map for diffuse lighting
/// - Pre-filtered radiance map for specular reflections
/// - BRDF lookup table for split-sum approximation
///
/// Example usage:
/// ```cpp
/// IBLEnvironment ibl;
/// ibl.init(ctx);
/// ibl.loadHDR(ctx, "assets/hdris/studio.hdr");
///
/// chain.add<Render3D>("render")
///     .ibl(true)
///     .environment(&ibl);
/// ```
class IBLEnvironment {
public:
    IBLEnvironment();
    ~IBLEnvironment();

    // Non-copyable
    IBLEnvironment(const IBLEnvironment&) = delete;
    IBLEnvironment& operator=(const IBLEnvironment&) = delete;

    /// Initialize the IBL processor (creates compute pipelines)
    /// Must be called before loadHDR()
    bool init(Context& ctx);

    /// Load an HDR equirectangular image and generate IBL cubemaps
    /// @param ctx Vivid context
    /// @param hdrPath Path to .hdr file
    /// @return true if loading succeeded
    bool loadHDR(Context& ctx, const std::string& hdrPath);

    /// Release all GPU resources
    void cleanup();

    /// Check if environment is loaded and ready
    bool isLoaded() const { return m_irradianceMap.valid(); }

    /// Check if processor is initialized
    bool isInitialized() const { return m_initialized; }

    // IBL texture accessors (for Render3D)
    WGPUTextureView irradianceView() const;
    WGPUTextureView prefilteredView() const;
    WGPUTextureView brdfLUTView() const;

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

    bool m_initialized = false;
};

} // namespace vivid::render3d
