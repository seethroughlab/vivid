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
        ss << "}";
        return ss.str();
    }
};

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
