#pragma once

/**
 * @file visual_analysis.h
 * @brief Visual analysis tools: color harmony, symmetry, spatial balance
 *
 * Provides CPU-only analysis of RGBA8 pixel buffers for:
 * - Color harmony (k-means palette extraction, harmony model scoring)
 * - Symmetry (horizontal, vertical, radial)
 * - Spatial balance (rule-of-thirds, directional bias, quadrant balance)
 *
 * These metrics are computed during inspectAll() and exposed as assertion
 * paths: harmony.*, symmetry.*, balance.*
 */

#include <vivid/frame_analysis.h>
#include <string>
#include <vector>
#include <cstdint>

namespace vivid {

/**
 * @brief Color harmony analysis from k-means palette extraction
 */
struct ColorHarmonyAnalysis {
    float harmonyScore = 0.0f;         ///< Best harmony model score (0-1)
    std::string harmonyType = "none";  ///< Best-fit model: complementary, analogous, triadic, split-complementary, none
    float paletteContrast = 0.0f;      ///< Lightness range of palette (0-1)
    std::vector<std::string> palette;  ///< Hex color strings (e.g. "#ff0000"), sorted by population

    std::string toJSON() const;
};

/**
 * @brief Symmetry analysis comparing mirrored pixel regions
 */
struct SymmetryAnalysis {
    float horizontalSymmetry = 0.0f;   ///< Left-right mirror similarity (0-1)
    float verticalSymmetry = 0.0f;     ///< Top-bottom mirror similarity (0-1)
    float radialSymmetry = 0.0f;       ///< 4-fold rotational similarity (0-1)

    std::string toJSON() const;
};

/**
 * @brief Spatial balance analysis using region brightness
 */
struct SpatialBalanceAnalysis {
    float thirdsScore = 0.0f;          ///< Rule-of-thirds power point concentration (0-1)
    float horizontalBias = 0.0f;       ///< Left-right weight bias (-1 to +1, 0 = centered)
    float verticalBias = 0.0f;         ///< Top-bottom weight bias (-1 to +1, 0 = centered)
    float balanceScore = 0.0f;         ///< Quadrant uniformity (0-1, 1 = perfectly balanced)

    std::string toJSON() const;
};

/**
 * @brief Composite visual analysis containing all three sub-analyses
 */
struct VisualAnalysis {
    ColorHarmonyAnalysis harmony;
    SymmetryAnalysis symmetry;
    SpatialBalanceAnalysis balance;

    std::string toJSON() const;
};

/**
 * @brief Analyze color harmony from RGBA8 pixels
 *
 * Extracts a 5-color palette via k-means in Lab space, then scores
 * against harmony models (complementary, analogous, triadic, split-complementary).
 *
 * @param pixels RGBA8 pixel data
 * @param width Image width
 * @param height Image height
 * @return ColorHarmonyAnalysis with palette and harmony scores
 */
ColorHarmonyAnalysis analyzeColorHarmony(const uint8_t* pixels, uint32_t width, uint32_t height);

/**
 * @brief Analyze symmetry from RGBA8 pixels
 *
 * Compares mirrored regions at 64x64 downsampled luminance.
 *
 * @param pixels RGBA8 pixel data
 * @param width Image width
 * @param height Image height
 * @return SymmetryAnalysis with horizontal, vertical, and radial scores
 */
SymmetryAnalysis analyzeSymmetry(const uint8_t* pixels, uint32_t width, uint32_t height);

/**
 * @brief Analyze spatial balance from RGBA8 pixels
 *
 * Computes rule-of-thirds, directional bias, and quadrant balance
 * from a 3x3 region brightness grid.
 *
 * @param pixels RGBA8 pixel data
 * @param width Image width
 * @param height Image height
 * @return SpatialBalanceAnalysis with thirds, bias, and balance scores
 */
SpatialBalanceAnalysis analyzeSpatialBalance(const uint8_t* pixels, uint32_t width, uint32_t height);

/**
 * @brief Analyze spatial balance from pre-computed FrameAnalysis
 *
 * Avoids redundant pixel processing when FrameAnalysis is already available.
 *
 * @param fa FrameAnalysis with regionBrightness already computed
 * @return SpatialBalanceAnalysis with thirds, bias, and balance scores
 */
SpatialBalanceAnalysis analyzeSpatialBalance(const FrameAnalysis& fa);

/**
 * @brief Run all visual analyses on RGBA8 pixels
 *
 * Convenience function that calls analyzeColorHarmony(), analyzeSymmetry(),
 * and analyzeSpatialBalance().
 *
 * @param pixels RGBA8 pixel data
 * @param width Image width
 * @param height Image height
 * @return VisualAnalysis containing all three sub-analyses
 */
VisualAnalysis analyzeVisual(const uint8_t* pixels, uint32_t width, uint32_t height);

} // namespace vivid
