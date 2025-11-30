#pragma once
#include "renderer.h"
#include <vivid/graphics3d.h>
#include <webgpu/webgpu.h>
#include <string>
#include <vector>

namespace vivid {

/**
 * @brief Internal cubemap texture data.
 *
 * Stores the GPU resources for a cubemap texture.
 * Used for both regular cubemaps and mip-mapped radiance maps.
 */
struct CubemapData {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;      // Cube view (for sampling)
    WGPUTextureView faceViews[6] = {};   // Individual face views (for rendering)
    int size = 0;
    int mipLevels = 1;
};

// Helper to get CubemapData from public Cubemap
inline CubemapData* getCubemapData(const Cubemap& cube) {
    return static_cast<CubemapData*>(cube.handle);
}

/**
 * @brief Cubemap processing for Image-Based Lighting.
 *
 * Handles all cubemap operations needed for IBL:
 * - Loading HDR equirectangular images
 * - Converting to cubemaps
 * - Computing irradiance maps (diffuse IBL)
 * - Pre-filtering radiance maps (specular IBL)
 * - Generating BRDF LUT
 */
class CubemapProcessor {
public:
    CubemapProcessor() = default;
    ~CubemapProcessor();

    // Non-copyable
    CubemapProcessor(const CubemapProcessor&) = delete;
    CubemapProcessor& operator=(const CubemapProcessor&) = delete;

    /**
     * @brief Initialize the processor with GPU device.
     * @param renderer The renderer for GPU resources.
     * @return true if initialization succeeded.
     */
    bool init(Renderer& renderer);

    /**
     * @brief Destroy GPU resources.
     */
    void destroy();

    /**
     * @brief Load an HDR equirectangular image and create a full IBL environment.
     *
     * This is the main entry point. It:
     * 1. Loads the HDR file
     * 2. Converts to cubemap
     * 3. Computes irradiance map
     * 4. Pre-filters radiance map
     * 5. Ensures BRDF LUT exists
     *
     * @param path Path to HDR file (.hdr format)
     * @return Complete Environment ready for IBL rendering
     */
    Environment loadEnvironment(const std::string& path);

    /**
     * @brief Create an empty cubemap.
     * @param size Size of each face in pixels.
     * @param mipLevels Number of mip levels (1 for no mipmaps).
     * @param hdr True for RGBA16Float, false for RGBA8Unorm.
     * @return Cubemap handle.
     */
    Cubemap createCubemap(int size, int mipLevels = 1, bool hdr = true);

    /**
     * @brief Destroy a cubemap and free GPU resources.
     */
    void destroyCubemap(Cubemap& cubemap);

    /**
     * @brief Convert an equirectangular HDR image to a cubemap.
     * @param hdrPixels HDR pixel data (RGB float).
     * @param width Image width.
     * @param height Image height.
     * @param cubemapSize Output cubemap face size.
     * @return Cubemap with the environment.
     */
    Cubemap equirectangularToCubemap(const float* hdrPixels, int width, int height, int cubemapSize = 512);

    /**
     * @brief Compute irradiance map from environment cubemap.
     *
     * The irradiance map stores the hemispherical integral of incoming
     * light for each direction, used for diffuse IBL.
     *
     * @param envCubemap Source environment cubemap.
     * @param size Output irradiance map size (typically 32-64).
     * @return Irradiance cubemap.
     */
    Cubemap computeIrradiance(const Cubemap& envCubemap, int size = 64);

    /**
     * @brief Pre-filter environment map for specular IBL.
     *
     * Creates a mip-mapped cubemap where each mip level corresponds
     * to a different roughness value. Used for specular reflections.
     *
     * @param envCubemap Source environment cubemap.
     * @param size Output radiance map base size (typically 256-512).
     * @param mipLevels Number of roughness levels (typically 5-6).
     * @return Pre-filtered radiance cubemap.
     */
    Cubemap computeRadiance(const Cubemap& envCubemap, int size = 256, int mipLevels = 5);

    /**
     * @brief Get or create the BRDF lookup table.
     *
     * The BRDF LUT is a 2D texture indexed by (NdotV, roughness)
     * that stores pre-computed Fresnel-geometry terms.
     * It's the same for all environments, so we cache it.
     *
     * @param size LUT size (typically 256 or 512).
     * @return Texture handle for BRDF LUT.
     */
    void* getBRDFLUT(int size = 256);

    /**
     * @brief Check if processor is initialized.
     */
    bool valid() const { return renderer_ != nullptr; }

private:
    Renderer* renderer_ = nullptr;

    // Cached BRDF LUT (shared across all environments)
    WGPUTexture brdfLUT_ = nullptr;
    WGPUTextureView brdfLUTView_ = nullptr;
    int brdfLUTSize_ = 0;

    // Compute pipelines for IBL processing
    WGPUComputePipeline equirectPipeline_ = nullptr;
    WGPUComputePipeline irradiancePipeline_ = nullptr;
    WGPUComputePipeline radiancePipeline_ = nullptr;
    WGPUComputePipeline brdfPipeline_ = nullptr;

    // Bind group layouts
    WGPUBindGroupLayout equirectLayout_ = nullptr;
    WGPUBindGroupLayout irradianceLayout_ = nullptr;
    WGPUBindGroupLayout radianceLayout_ = nullptr;
    WGPUBindGroupLayout brdfLayout_ = nullptr;

    // Sampler for cubemap processing
    WGPUSampler cubemapSampler_ = nullptr;

    bool createPipelines();
    void destroyPipelines();
    bool createBRDFLUT(int size);
};

} // namespace vivid
