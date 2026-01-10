#pragma once

/**
 * @file optical_flow.h
 * @brief Optical flow motion detection using OpenCV
 *
 * Calculates motion vectors between consecutive frames using dense optical flow.
 */

#include <vivid/opencv/export.h>
#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>
#include <memory>

namespace vivid::opencv {

/**
 * @brief Optical flow visualization modes
 */
enum class FlowVizMode : int {
    Color = 0,      ///< HSV color wheel (hue=direction, saturation=magnitude)
    Arrows = 1,     ///< Arrow field overlay
    Magnitude = 2   ///< Grayscale magnitude only
};

/**
 * @brief Dense optical flow operator
 *
 * Calculates motion vectors between consecutive frames using Farneback's algorithm.
 * Outputs a visualization of the flow field.
 *
 * @note Requires CPU pixel data from input via cpuPixels().
 * Compatible sources: Webcam, VideoPlayer.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | pyrScale | float | 0.1-0.9 | 0.5 | Pyramid scale factor |
 * | levels | int | 1-10 | 3 | Number of pyramid levels |
 * | winSize | int | 3-50 | 15 | Averaging window size |
 * | iterations | int | 1-20 | 3 | Iterations per pyramid level |
 * | polyN | int | 5-7 | 5 | Polynomial expansion neighborhood |
 * | polySigma | float | 1.0-2.0 | 1.2 | Gaussian sigma for polynomial |
 * | vizMode | int | 0-2 | 0 | Visualization mode |
 * | sensitivity | float | 0.1-10 | 1.0 | Motion sensitivity multiplier |
 *
 * @par Example
 * @code
 * auto& flow = chain.add<vivid::opencv::OpticalFlow>("flow");
 * flow.input("cam");
 * flow.sensitivity = 2.0f;  // Amplify small motions
 * @endcode
 */
class VIVID_OPENCV_API OpticalFlow : public vivid::effects::TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> scale{"scale", 0.15f, 0.05f, 1.0f};        ///< Processing scale (0.15 = ~288x162 from 1080p)
    Param<float> pyrScale{"pyrScale", 0.5f, 0.1f, 0.9f};    ///< Pyramid scale
    Param<int> levels{"levels", 1, 1, 5};                    ///< Pyramid levels (1=fastest)
    Param<int> winSize{"winSize", 9, 3, 25};                ///< Window size (smaller=faster)
    Param<int> iterations{"iterations", 1, 1, 10};          ///< Iterations (1=fastest)
    Param<int> polyN{"polyN", 5, 5, 7};                     ///< Poly neighborhood
    Param<float> polySigma{"polySigma", 1.1f, 1.0f, 2.0f};  ///< Poly sigma
    Param<int> vizMode{"vizMode", 0, 0, 2};                 ///< Visualization mode
    Param<float> sensitivity{"sensitivity", 1.0f, 0.1f, 10.0f}; ///< Motion sensitivity

    /// @}
    // -------------------------------------------------------------------------

    OpticalFlow();
    ~OpticalFlow() override;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "OpticalFlow"; }

    // Override output accessors for custom texture
    WGPUTexture outputTexture() const override { return m_cvOutput; }
    WGPUTextureView outputView() const override { return m_cvOutputView; }

    /// @}

private:
    void createOutputWithCopyDst(Context& ctx, int width, int height);
    void releaseCustomOutput();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    WGPUTexture m_cvOutput = nullptr;
    WGPUTextureView m_cvOutputView = nullptr;
    int m_cvWidth = 0;
    int m_cvHeight = 0;
};

} // namespace vivid::opencv
