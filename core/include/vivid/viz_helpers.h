#pragma once

/**
 * @file viz_helpers.h
 * @brief High-level visualization helpers for operator drawVisualization()
 *
 * Provides reusable drawing functions for common visualization patterns:
 * - Level meters with gradient coloring
 * - Spectrum/FFT bar displays
 * - Waveform rendering
 * - ADSR envelope shapes
 * - Mini keyboard displays
 *
 * @par Example Usage
 * @code
 * bool MyOperator::drawVisualization(VizDrawList* dl, float minX, float minY,
 *                                     float maxX, float maxY) {
 *     VizHelpers viz(dl);
 *     VizBounds bounds{minX, minY, maxX - minX, maxY - minY};
 *
 *     viz.drawBackground(bounds);
 *     viz.drawMeter(bounds.inset(4), m_level);
 *     return true;
 * }
 * @endcode
 */

#include <vivid/viz_draw_list.h>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace vivid {

// ============================================================================
// VizBounds - Layout helper
// ============================================================================

/**
 * @brief Rectangle bounds for layout calculations
 *
 * Simplifies common layout operations like insetting, splitting, and centering.
 */
struct VizBounds {
    float x = 0, y = 0, w = 0, h = 0;

    /// @brief Center X coordinate
    float cx() const { return x + w * 0.5f; }

    /// @brief Center Y coordinate
    float cy() const { return y + h * 0.5f; }

    /// @brief Right edge
    float right() const { return x + w; }

    /// @brief Bottom edge
    float bottom() const { return y + h; }

    /// @brief Create bounds inset by margin on all sides
    VizBounds inset(float margin) const {
        return {x + margin, y + margin, w - margin * 2, h - margin * 2};
    }

    /// @brief Create bounds inset by different horizontal/vertical margins
    VizBounds inset(float hMargin, float vMargin) const {
        return {x + hMargin, y + vMargin, w - hMargin * 2, h - vMargin * 2};
    }

    /// @brief Get left portion (0-1 ratio)
    VizBounds splitLeft(float ratio) const {
        return {x, y, w * ratio, h};
    }

    /// @brief Get right portion (0-1 ratio)
    VizBounds splitRight(float ratio) const {
        float leftW = w * (1.0f - ratio);
        return {x + leftW, y, w * ratio, h};
    }

    /// @brief Get top portion (0-1 ratio)
    VizBounds splitTop(float ratio) const {
        return {x, y, w, h * ratio};
    }

    /// @brief Get bottom portion (0-1 ratio)
    VizBounds splitBottom(float ratio) const {
        float topH = h * (1.0f - ratio);
        return {x, y + topH, w, h * ratio};
    }

    /// @brief Create sub-bounds at specific position within this bounds
    VizBounds sub(float relX, float relY, float subW, float subH) const {
        return {x + relX, y + relY, subW, subH};
    }
};

// ============================================================================
// VizColors - Standard color palette
// ============================================================================

/**
 * @brief Standard color palette for visualizations
 *
 * Use these colors for consistent appearance across operators.
 * All colors are in ABGR format (use with VIZ_COL32 macro).
 */
namespace VizColors {
    // Backgrounds
    constexpr uint32_t Background     = VIZ_COL32(40, 30, 50, 255);    ///< Dark purple
    constexpr uint32_t BackgroundDark = VIZ_COL32(25, 20, 35, 255);    ///< Darker variant
    constexpr uint32_t BackgroundAlt  = VIZ_COL32(35, 40, 50, 255);    ///< Blue-gray

    // Meters (green-yellow-red gradient)
    constexpr uint32_t MeterGreen  = VIZ_COL32(80, 180, 80, 255);
    constexpr uint32_t MeterYellow = VIZ_COL32(200, 180, 60, 255);
    constexpr uint32_t MeterRed    = VIZ_COL32(200, 80, 80, 255);

    // Accents
    constexpr uint32_t Highlight  = VIZ_COL32(255, 200, 100, 255);     ///< Warm gold
    constexpr uint32_t Active     = VIZ_COL32(100, 180, 255, 255);     ///< Bright blue
    constexpr uint32_t Inactive   = VIZ_COL32(80, 80, 100, 150);       ///< Dim gray
    constexpr uint32_t Border     = VIZ_COL32(80, 80, 100, 200);       ///< Border gray

    // Status
    constexpr uint32_t StatusOpen   = VIZ_COL32(80, 200, 120, 255);    ///< Green (gate open)
    constexpr uint32_t StatusClosed = VIZ_COL32(200, 80, 80, 255);     ///< Red (gate closed)

    // Envelope colors
    constexpr uint32_t EnvelopeWarm  = VIZ_COL32(255, 150, 80, 255);   ///< Orange envelope
    constexpr uint32_t EnvelopeCool  = VIZ_COL32(100, 150, 255, 255);  ///< Blue envelope

    // Piano keyboard
    constexpr uint32_t KeyWhite      = VIZ_COL32(240, 235, 220, 255);  ///< Ivory white key
    constexpr uint32_t KeyBlack      = VIZ_COL32(30, 25, 20, 255);     ///< Black key
    constexpr uint32_t KeyActive     = VIZ_COL32(255, 200, 100, 255);  ///< Playing note
    constexpr uint32_t KeyAvailable  = VIZ_COL32(180, 175, 160, 255);  ///< Has sample

    // Text
    constexpr uint32_t TextPrimary   = VIZ_COL32(255, 255, 255, 255);
    constexpr uint32_t TextSecondary = VIZ_COL32(180, 180, 200, 255);
    constexpr uint32_t TextDim       = VIZ_COL32(120, 120, 140, 200);

    /**
     * @brief Get meter color for normalized value (0-1)
     * @param t Normalized value where 0=bottom, 1=top
     * @return Gradient color (green→yellow→red)
     */
    inline uint32_t meterGradient(float t) {
        if (t < 0.5f) return MeterGreen;
        if (t < 0.8f) return MeterYellow;
        return MeterRed;
    }

    /**
     * @brief Interpolate between two colors
     * @param a First color
     * @param b Second color
     * @param t Blend factor (0=a, 1=b)
     */
    inline uint32_t lerp(uint32_t a, uint32_t b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        uint8_t ra = (a >> 0) & 0xFF, ga = (a >> 8) & 0xFF;
        uint8_t ba = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
        uint8_t rb = (b >> 0) & 0xFF, gb = (b >> 8) & 0xFF;
        uint8_t bb = (b >> 16) & 0xFF, ab = (b >> 24) & 0xFF;

        return VIZ_COL32(
            static_cast<uint8_t>(ra + (rb - ra) * t),
            static_cast<uint8_t>(ga + (gb - ga) * t),
            static_cast<uint8_t>(ba + (bb - ba) * t),
            static_cast<uint8_t>(aa + (ab - aa) * t)
        );
    }
}

// ============================================================================
// VizHelpers - High-level drawing functions
// ============================================================================

/**
 * @brief High-level visualization drawing helpers
 *
 * Wraps VizDrawList to provide common visualization patterns with minimal code.
 * Each helper handles layout, colors, and styling automatically.
 */
class VizHelpers {
public:
    explicit VizHelpers(VizDrawList* dl) : m_dl(dl) {}

    // -------------------------------------------------------------------------
    /// @name Background
    /// @{

    /**
     * @brief Draw standard dark background
     */
    void drawBackground(const VizBounds& b, uint32_t color = VizColors::Background) {
        m_dl->AddRectFilled({b.x, b.y}, {b.right(), b.bottom()}, color, 4.0f);
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Meters
    /// @{

    /**
     * @brief Draw a level meter with gradient coloring
     * @param bounds Drawing area
     * @param value Normalized value (0-1)
     * @param horizontal If true, draw horizontal bar; otherwise vertical
     *
     * Draws a bar that fills based on value, with color gradient:
     * green (0-0.5) → yellow (0.5-0.8) → red (0.8-1.0)
     */
    void drawMeter(const VizBounds& bounds, float value, bool horizontal = false);

    /**
     * @brief Draw dual RMS/Peak meters (like Levels operator)
     * @param bounds Drawing area
     * @param rms RMS level (0-1)
     * @param peak Peak level (0-1)
     */
    void drawDualMeter(const VizBounds& bounds, float rms, float peak);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Spectrum & Waveform
    /// @{

    /**
     * @brief Draw FFT spectrum bars
     * @param bounds Drawing area
     * @param bins Spectrum data array
     * @param binCount Number of bins in array
     * @param numBars Number of bars to display (samples logarithmically from bins)
     */
    void drawSpectrum(const VizBounds& bounds, const float* bins, int binCount,
                      int numBars = 24);

    /**
     * @brief Draw audio waveform
     * @param bounds Drawing area
     * @param samples Sample data array
     * @param count Number of samples
     * @param color Waveform color
     */
    void drawWaveform(const VizBounds& bounds, const float* samples, int count,
                      uint32_t color = VizColors::Highlight);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Envelopes
    /// @{

    /**
     * @brief Draw ADSR envelope shape
     * @param bounds Drawing area
     * @param attack Attack time (seconds, for display scaling)
     * @param decay Decay time
     * @param sustain Sustain level (0-1)
     * @param release Release time
     * @param currentValue Current envelope value (-1 to hide indicator)
     */
    void drawEnvelopeADSR(const VizBounds& bounds,
                          float attack, float decay, float sustain, float release,
                          float currentValue = -1.0f);

    /**
     * @brief Draw simple vertical envelope bar (for drums)
     * @param bounds Drawing area
     * @param value Current envelope value (0-1)
     * @param color Bar color
     */
    void drawEnvelopeBar(const VizBounds& bounds, float value,
                         uint32_t color = VizColors::EnvelopeWarm);

    /**
     * @brief Draw dual envelope bars (tone + noise, like Snare)
     * @param bounds Drawing area
     * @param value1 First envelope value (bottom half)
     * @param value2 Second envelope value (top half)
     * @param color1 First bar color
     * @param color2 Second bar color
     */
    void drawDualEnvelope(const VizBounds& bounds, float value1, float value2,
                          uint32_t color1 = VizColors::EnvelopeWarm,
                          uint32_t color2 = VizColors::TextPrimary);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Keyboard
    /// @{

    /**
     * @brief Draw mini piano keyboard with active notes highlighted
     * @param bounds Drawing area
     * @param lowNote Lowest MIDI note to display
     * @param highNote Highest MIDI note to display
     * @param activeNotes Currently playing notes (highlighted gold)
     * @param availableNotes Notes that have samples (highlighted lighter)
     */
    void drawKeyboard(const VizBounds& bounds,
                      int lowNote, int highNote,
                      const std::vector<int>& activeNotes,
                      const std::vector<int>& availableNotes = {});

    /// @}
    // -------------------------------------------------------------------------
    /// @name Gate & Status
    /// @{

    /**
     * @brief Draw gate indicator (open/closed bars)
     * @param bounds Drawing area
     * @param isOpen Whether gate is open
     * @param openAmount How far open (0-1), affects bar separation
     */
    void drawGate(const VizBounds& bounds, bool isOpen, float openAmount = 1.0f);

    /**
     * @brief Draw activity indicator dot
     * @param cx Center X
     * @param cy Center Y
     * @param intensity Brightness (0-1)
     * @param color Dot color
     */
    void drawActivityDot(float cx, float cy, float intensity,
                         uint32_t color = VizColors::Highlight);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Text & Labels
    /// @{

    /**
     * @brief Draw centered label text
     * @param bounds Area to center text within
     * @param text Label text
     * @param color Text color
     */
    void drawLabel(const VizBounds& bounds, const char* text,
                   uint32_t color = VizColors::TextPrimary);

    /**
     * @brief Draw formatted value with suffix (e.g., "-6.2 dB")
     * @param bounds Area to center text within
     * @param value Numeric value
     * @param suffix Text suffix (e.g., " dB", " Hz")
     * @param precision Decimal places
     */
    void drawValue(const VizBounds& bounds, float value,
                   const char* suffix = "", int precision = 1);

    /// @}

private:
    VizDrawList* m_dl;

    // Helper to check if MIDI note is a black key
    static bool isBlackKey(int midiNote) {
        int n = midiNote % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }
};

} // namespace vivid
