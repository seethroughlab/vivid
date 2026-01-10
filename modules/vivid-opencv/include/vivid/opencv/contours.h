#pragma once

/**
 * @file contours.h
 * @brief Contour detection operator using OpenCV
 *
 * Detects edges and extracts contours from input textures using OpenCV's
 * Canny edge detection and findContours algorithms.
 */

#include <vivid/opencv/export.h>
#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>
#include <memory>

namespace vivid::opencv {

/**
 * @brief Contour retrieval modes
 *
 * Maps to OpenCV's cv::RetrievalModes
 */
enum class ContourMode : int {
    External = 0,   ///< Retrieve only extreme outer contours
    List = 1,       ///< Retrieve all contours without hierarchy
    CComp = 2,      ///< Retrieve all contours with 2-level hierarchy
    Tree = 3        ///< Retrieve all contours with full hierarchy
};

/**
 * @brief Contour detection and drawing operator
 *
 * Applies Canny edge detection followed by OpenCV's findContours to detect
 * shapes in the input texture. Contours are drawn on a transparent background.
 *
 * @note This operator requires CPU pixel data from the input operator via
 * cpuPixels(). Compatible sources include Webcam and VideoPlayer.
 * Operators that only provide GPU textures will be skipped.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | threshold1 | float | 0-255 | 100 | Canny first threshold |
 * | threshold2 | float | 0-255 | 200 | Canny second threshold |
 * | mode | int | 0-3 | 0 | Contour retrieval mode |
 * | lineWidth | float | 1-20 | 2 | Contour line thickness |
 * | colorR | float | 0-1 | 0 | Contour color red component |
 * | colorG | float | 0-1 | 1 | Contour color green component |
 * | colorB | float | 0-1 | 0 | Contour color blue component |
 * | colorA | float | 0-1 | 1 | Contour color alpha component |
 *
 * @par Example
 * @code
 * auto& img = chain.add<Image>("img");
 * img.file = "photo.jpg";
 *
 * auto& contours = chain.add<vivid::opencv::Contours>("contours");
 * contours.input("img");
 * contours.threshold1 = 50.0f;
 * contours.threshold2 = 150.0f;
 * contours.lineWidth = 2.0f;
 *
 * chain.output("contours");
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture (any format)
 *
 * @par Output
 * Texture with contours drawn on transparent background
 */
class VIVID_OPENCV_API Contours : public vivid::effects::TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> threshold1{"threshold1", 100.0f, 0.0f, 255.0f};  ///< Canny first threshold
    Param<float> threshold2{"threshold2", 200.0f, 0.0f, 255.0f};  ///< Canny second threshold
    Param<int> mode{"mode", 0, 0, 3};                              ///< Contour retrieval mode
    Param<float> lineWidth{"lineWidth", 2.0f, 1.0f, 20.0f};       ///< Line thickness
    Param<float> colorR{"colorR", 0.0f, 0.0f, 1.0f};              ///< Color red
    Param<float> colorG{"colorG", 1.0f, 0.0f, 1.0f};              ///< Color green
    Param<float> colorB{"colorB", 0.0f, 0.0f, 1.0f};              ///< Color blue
    Param<float> colorA{"colorA", 1.0f, 0.0f, 1.0f};              ///< Color alpha

    /// @}
    // -------------------------------------------------------------------------

    Contours();
    ~Contours() override;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Contours"; }

    // Override output accessors to return custom texture with COPY_DST
    WGPUTexture outputTexture() const override { return m_cvOutput; }
    WGPUTextureView outputView() const override { return m_cvOutputView; }

    /// @}

    // -------------------------------------------------------------------------
    /// @name Accessors
    /// @{

    /**
     * @brief Get the number of detected contours
     * @return Contour count
     */
    size_t contourCount() const;

    /// @}

private:
    void createOutputWithCopyDst(Context& ctx, int width, int height);
    void releaseCustomOutput();

    struct Impl;  // Forward declaration - hides OpenCV types
    std::unique_ptr<Impl> m_impl;

    // Custom output texture with COPY_DST flag (for matToTexture uploads)
    WGPUTexture m_cvOutput = nullptr;
    WGPUTextureView m_cvOutputView = nullptr;
    int m_cvWidth = 0;
    int m_cvHeight = 0;
};

} // namespace vivid::opencv
