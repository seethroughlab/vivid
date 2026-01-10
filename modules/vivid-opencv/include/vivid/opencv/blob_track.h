#pragma once

/**
 * @file blob_track.h
 * @brief Blob detection and tracking using OpenCV
 *
 * Detects circular blobs in the image based on color, size, and shape criteria.
 */

#include <vivid/opencv/export.h>
#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>
#include <memory>

namespace vivid::opencv {

/**
 * @brief Blob detection operator
 *
 * Detects blobs (circular regions) in the input image using OpenCV's SimpleBlobDetector.
 * Useful for tracking objects, detecting lights, or finding colored regions.
 *
 * @note Requires CPU pixel data from input via cpuPixelView().
 * Compatible sources: Webcam, VideoPlayer.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | minArea | float | 10-10000 | 100 | Minimum blob area in pixels |
 * | maxArea | float | 100-100000 | 50000 | Maximum blob area in pixels |
 * | minCircularity | float | 0-1 | 0.1 | Minimum circularity (1=perfect circle) |
 * | minConvexity | float | 0-1 | 0.5 | Minimum convexity |
 * | minInertia | float | 0-1 | 0.1 | Minimum inertia ratio |
 * | detectBright | int | 0-1 | 1 | Detect bright blobs |
 * | detectDark | int | 0-1 | 1 | Detect dark blobs |
 * | threshold | float | 0-255 | 128 | Binarization threshold |
 *
 * @par Example
 * @code
 * auto& blobs = chain.add<vivid::opencv::BlobTrack>("blobs");
 * blobs.input("cam");
 * blobs.minArea = 500.0f;    // Detect larger blobs only
 * blobs.detectDark = 0;       // Only detect bright blobs
 * @endcode
 */
class VIVID_OPENCV_API BlobTrack : public vivid::effects::TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> minArea{"minArea", 100.0f, 10.0f, 10000.0f};      ///< Min blob area
    Param<float> maxArea{"maxArea", 50000.0f, 100.0f, 100000.0f};  ///< Max blob area
    Param<float> minCircularity{"minCircularity", 0.1f, 0.0f, 1.0f}; ///< Min circularity
    Param<float> minConvexity{"minConvexity", 0.5f, 0.0f, 1.0f};   ///< Min convexity
    Param<float> minInertia{"minInertia", 0.1f, 0.0f, 1.0f};       ///< Min inertia ratio
    Param<int> detectBright{"detectBright", 1, 0, 1};              ///< Detect bright blobs
    Param<int> detectDark{"detectDark", 1, 0, 1};                  ///< Detect dark blobs
    Param<float> threshold{"threshold", 128.0f, 0.0f, 255.0f};     ///< Binarization threshold

    /// @}
    // -------------------------------------------------------------------------

    BlobTrack();
    ~BlobTrack() override;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "BlobTrack"; }

    // Override output accessors for custom texture
    WGPUTexture outputTexture() const override { return m_cvOutput; }
    WGPUTextureView outputView() const override { return m_cvOutputView; }

    /// @}

private:
    void createOutputTexture(Context& ctx, int width, int height);
    void releaseOutput();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    WGPUTexture m_cvOutput = nullptr;
    WGPUTextureView m_cvOutputView = nullptr;
    int m_cvWidth = 0;
    int m_cvHeight = 0;
};

} // namespace vivid::opencv
