#pragma once

/**
 * @file frame_analysis.h
 * @brief GPU texture analysis for LLM-driven visual evaluation
 *
 * FrameAnalysis provides statistical analysis of a texture's pixel content:
 * brightness, contrast, dominant color, histogram, and spatial distribution.
 * Computed via CPU readback from GPU textures.
 */

#include <webgpu/webgpu.h>
#include <array>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>

namespace vivid {

/**
 * @brief Statistical analysis of a rendered frame
 */
struct FrameAnalysis {
    float meanBrightness = 0.0f;          ///< Average luminance (0-1)
    float contrast = 0.0f;                ///< Standard deviation of luminance (0-1)
    float dominantColor[3] = {0, 0, 0};   ///< Dominant RGB color (0-1)
    float dominantHue = 0.0f;             ///< Dominant hue in degrees (0-360)
    float saturationAvg = 0.0f;           ///< Average saturation (0-1)
    std::array<int, 8> histogram = {};    ///< 8-bucket luminance histogram
    std::array<float, 9> regionBrightness = {};  ///< 3x3 spatial grid brightness

    // --- Tier 1 additions ---
    float textureEntropy = 0.0f;         ///< Normalized Shannon entropy of 64-bin histogram (0-1)
    float edgeDensity = 0.0f;            ///< Fraction of edge pixels (0-1)
    float avgGradientMag = 0.0f;         ///< Mean gradient magnitude
    float clipBlackPct = 0.0f;           ///< Fraction of pixels near pure black (lum < 0.005)
    float clipWhitePct = 0.0f;           ///< Fraction of pixels near pure white (lum > 0.995)
    float headroom = 1.0f;              ///< 1.0 - maxLuminance
    float rangeSpan = 0.0f;             ///< maxLuminance - minLuminance
    float sharpness = 0.0f;             ///< Laplacian variance
    float noiseLevel = 0.0f;            ///< Mean absolute Laplacian
    float visualCenterX = 0.5f;         ///< Brightness-weighted centroid X (0-1)
    float visualCenterY = 0.5f;         ///< Brightness-weighted centroid Y (0-1)
    float colorTemperature = 0.5f;      ///< 0=cool, 0.5=neutral, 1=warm
    std::array<float, 12> hueHistogram = {};  ///< 12-bin hue distribution (30 degree bins)
    int uniqueHueCount = 0;              ///< Hue bins above 5% threshold
    float hueEntropy = 0.0f;            ///< Normalized Shannon entropy of hue histogram (0-1)
    float alphaOpaquePct = 1.0f;        ///< Fraction fully opaque pixels
    float alphaTransparentPct = 0.0f;   ///< Fraction fully transparent pixels
    float alphaPartialPct = 0.0f;       ///< Fraction partially transparent pixels
    float alphaMean = 1.0f;             ///< Mean alpha value

    std::string toJSON() const {
        std::ostringstream ss;
        ss << "{";
        ss << "\"meanBrightness\":" << meanBrightness;
        ss << ",\"contrast\":" << contrast;
        ss << ",\"dominantColor\":[" << dominantColor[0] << "," << dominantColor[1] << "," << dominantColor[2] << "]";
        ss << ",\"dominantHue\":" << dominantHue;
        ss << ",\"saturationAvg\":" << saturationAvg;
        ss << ",\"histogram\":[";
        for (int i = 0; i < 8; i++) {
            if (i > 0) ss << ",";
            ss << histogram[i];
        }
        ss << "]";
        ss << ",\"regionBrightness\":[";
        for (int i = 0; i < 9; i++) {
            if (i > 0) ss << ",";
            ss << regionBrightness[i];
        }
        ss << "]";

        // Tier 1 additions
        ss << ",\"textureEntropy\":" << textureEntropy;
        ss << ",\"edgeDensity\":" << edgeDensity;
        ss << ",\"avgGradientMag\":" << avgGradientMag;
        ss << ",\"clipBlackPct\":" << clipBlackPct;
        ss << ",\"clipWhitePct\":" << clipWhitePct;
        ss << ",\"headroom\":" << headroom;
        ss << ",\"rangeSpan\":" << rangeSpan;
        ss << ",\"sharpness\":" << sharpness;
        ss << ",\"noiseLevel\":" << noiseLevel;
        ss << ",\"visualCenterX\":" << visualCenterX;
        ss << ",\"visualCenterY\":" << visualCenterY;
        ss << ",\"colorTemperature\":" << colorTemperature;
        ss << ",\"hueHistogram\":[";
        for (int i = 0; i < 12; i++) {
            if (i > 0) ss << ",";
            ss << hueHistogram[i];
        }
        ss << "]";
        ss << ",\"uniqueHueCount\":" << uniqueHueCount;
        ss << ",\"hueEntropy\":" << hueEntropy;
        ss << ",\"alphaOpaquePct\":" << alphaOpaquePct;
        ss << ",\"alphaTransparentPct\":" << alphaTransparentPct;
        ss << ",\"alphaPartialPct\":" << alphaPartialPct;
        ss << ",\"alphaMean\":" << alphaMean;

        ss << "}";
        return ss.str();
    }
};

/**
 * @brief Read back GPU texture pixels as RGBA8
 *
 * Performs GPU readback and format conversion (supports RGBA8, BGRA8, RGBA16Float, RGBA32Float).
 * Returns an empty vector on failure.
 *
 * @param device WebGPU device
 * @param queue WebGPU queue
 * @param texture Texture to read
 * @param outWidth Set to texture width
 * @param outHeight Set to texture height
 * @return RGBA8 pixel data (width * height * 4 bytes), or empty on failure
 */
std::vector<uint8_t> readbackTexturePixels(WGPUDevice device, WGPUQueue queue, WGPUTexture texture,
                                            uint32_t& outWidth, uint32_t& outHeight);

/**
 * @brief Analyze RGBA8 pixel data (CPU-only, no GPU needed)
 *
 * Computes all FrameAnalysis metrics from a raw RGBA8 pixel buffer.
 * Useful when pixels are already available from readbackTexturePixels().
 *
 * @param pixels RGBA8 pixel data
 * @param width Image width
 * @param height Image height
 * @return FrameAnalysis with computed statistics
 */
FrameAnalysis analyzePixels(const uint8_t* pixels, uint32_t width, uint32_t height);

/**
 * @brief Analyze a GPU texture and produce frame statistics
 *
 * Reads back texture pixels from the GPU and computes statistical analysis.
 * Uses the same staging buffer pattern as VideoExporter::saveSnapshot().
 *
 * @param device WebGPU device
 * @param queue WebGPU queue
 * @param texture Texture to analyze
 * @return FrameAnalysis with computed statistics, or default values on failure
 */
FrameAnalysis analyzeTexture(WGPUDevice device, WGPUQueue queue, WGPUTexture texture);

} // namespace vivid
