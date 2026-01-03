#include <vivid/viz_helpers.h>
#include <cstdio>
#include <cstring>

namespace vivid {

// =============================================================================
// Meters
// =============================================================================

void VizHelpers::drawMeter(const VizBounds& bounds, float value, bool horizontal) {
    value = std::clamp(value, 0.0f, 1.0f);

    // Draw border
    m_dl->AddRect({bounds.x, bounds.y}, {bounds.right(), bounds.bottom()},
                  VizColors::Border, 2.0f);

    if (horizontal) {
        // Horizontal meter: draw lines from left to right
        float fillW = bounds.w * value;
        for (int i = 0; i < static_cast<int>(fillW); i++) {
            float t = static_cast<float>(i) / bounds.w;
            uint32_t col = VizColors::meterGradient(t);
            float x = bounds.x + i;
            m_dl->AddLine({x, bounds.y + 1}, {x, bounds.bottom() - 1}, col);
        }
    } else {
        // Vertical meter: draw lines from bottom to top
        float fillH = bounds.h * value;
        for (int i = 0; i < static_cast<int>(fillH); i++) {
            float t = static_cast<float>(i) / bounds.h;
            uint32_t col = VizColors::meterGradient(t);
            float y = bounds.bottom() - 1 - i;
            m_dl->AddLine({bounds.x + 1, y}, {bounds.right() - 1, y}, col);
        }
    }
}

void VizHelpers::drawDualMeter(const VizBounds& bounds, float rms, float peak) {
    float barWidth = bounds.w * 0.35f;
    float gap = bounds.w * 0.1f;
    float startX = bounds.x + bounds.w * 0.1f;

    // Left bar: RMS
    VizBounds rmsB{startX, bounds.y, barWidth, bounds.h};
    drawMeter(rmsB, rms, false);

    // Right bar: Peak
    VizBounds peakB{startX + barWidth + gap, bounds.y, barWidth, bounds.h};
    drawMeter(peakB, peak, false);
}

// =============================================================================
// Spectrum & Waveform
// =============================================================================

void VizHelpers::drawSpectrum(const VizBounds& bounds, const float* bins, int binCount,
                               int numBars) {
    if (!bins || binCount <= 0) return;

    float barW = bounds.w / numBars - 1.0f;

    for (int i = 0; i < numBars; i++) {
        // Logarithmic distribution of bins
        int binIdx = static_cast<int>(std::pow(static_cast<float>(i + 1) / numBars, 2.0f) *
                                      binCount * 0.5f);
        binIdx = std::min(binIdx, binCount - 1);

        float mag = bins[binIdx] * 3.0f;  // Scale up for visibility
        mag = std::clamp(mag, 0.0f, 1.0f);

        float barH = mag * bounds.h;
        float barX = bounds.x + i * (barW + 1.0f);
        float barY = bounds.bottom() - barH;

        // Color based on height
        uint32_t col = VizColors::meterGradient(mag);
        m_dl->AddRectFilled({barX, barY}, {barX + barW, bounds.bottom()}, col, 2.0f);
    }
}

void VizHelpers::drawWaveform(const VizBounds& bounds, const float* samples, int count,
                               uint32_t color) {
    if (!samples || count < 2) return;

    float cy = bounds.cy();
    float halfH = bounds.h * 0.4f;

    float step = static_cast<float>(count) / bounds.w;
    float prevX = bounds.x;
    float prevY = cy - samples[0] * halfH;

    for (float x = bounds.x + 1; x < bounds.right(); x += 1.0f) {
        int idx = static_cast<int>((x - bounds.x) * step);
        idx = std::min(idx, count - 1);

        float y = cy - samples[idx] * halfH;
        m_dl->AddLine({prevX, prevY}, {x, y}, color, 1.0f);
        prevX = x;
        prevY = y;
    }
}

// =============================================================================
// Envelopes
// =============================================================================

void VizHelpers::drawEnvelopeADSR(const VizBounds& bounds,
                                   float attack, float decay, float sustain, float release,
                                   float currentValue) {
    // Normalize times to fit in bounds
    float totalTime = attack + decay + 0.3f + release;  // 0.3 = sustain phase display
    float timeScale = bounds.w / totalTime;

    float x0 = bounds.x;
    float x1 = x0 + attack * timeScale;                     // End of attack
    float x2 = x1 + decay * timeScale;                      // End of decay
    float x3 = x2 + 0.3f * timeScale;                       // End of sustain
    float x4 = bounds.right();                              // End of release

    float yBottom = bounds.bottom();
    float yTop = bounds.y;
    float ySustain = bounds.y + bounds.h * (1.0f - sustain);

    // Draw envelope shape
    uint32_t lineColor = VizColors::EnvelopeCool;

    // Attack: 0 → 1
    m_dl->AddLine({x0, yBottom}, {x1, yTop}, lineColor, 2.0f);
    // Decay: 1 → sustain
    m_dl->AddLine({x1, yTop}, {x2, ySustain}, lineColor, 2.0f);
    // Sustain: hold
    m_dl->AddLine({x2, ySustain}, {x3, ySustain}, lineColor, 2.0f);
    // Release: sustain → 0
    m_dl->AddLine({x3, ySustain}, {x4, yBottom}, lineColor, 2.0f);

    // Draw current value indicator if provided
    if (currentValue >= 0.0f) {
        float indicatorY = bounds.y + bounds.h * (1.0f - currentValue);
        m_dl->AddCircleFilled({bounds.x + 4, indicatorY}, 3.0f, VizColors::Highlight);
    }
}

void VizHelpers::drawEnvelopeBar(const VizBounds& bounds, float value,
                                  uint32_t color) {
    value = std::clamp(value, 0.0f, 1.0f);

    float barH = bounds.h * value;
    float barY = bounds.bottom() - barH;

    m_dl->AddRectFilled({bounds.x, barY}, {bounds.right(), bounds.bottom()}, color, 3.0f);

    // Border
    m_dl->AddRect({bounds.x, bounds.y}, {bounds.right(), bounds.bottom()},
                  VizColors::Border, 2.0f);
}

void VizHelpers::drawDualEnvelope(const VizBounds& bounds, float value1, float value2,
                                   uint32_t color1, uint32_t color2) {
    float cy = bounds.cy();
    float halfH = bounds.h * 0.45f;

    // Bottom half: value1
    float bar1H = halfH * std::clamp(value1, 0.0f, 1.0f);
    m_dl->AddRectFilled({bounds.x, cy + 2}, {bounds.right(), cy + 2 + bar1H}, color1, 2.0f);

    // Top half: value2
    float bar2H = halfH * std::clamp(value2, 0.0f, 1.0f);
    m_dl->AddRectFilled({bounds.x, cy - 2 - bar2H}, {bounds.right(), cy - 2}, color2, 2.0f);

    // Divider
    m_dl->AddLine({bounds.x, cy}, {bounds.right(), cy}, VizColors::Border, 1.0f);
}

// =============================================================================
// Keyboard
// =============================================================================

void VizHelpers::drawKeyboard(const VizBounds& bounds,
                               int lowNote, int highNote,
                               const std::vector<int>& activeNotes,
                               const std::vector<int>& availableNotes) {
    // Count white keys in range
    int whiteKeyCount = 0;
    for (int n = lowNote; n <= highNote; n++) {
        if (!isBlackKey(n)) whiteKeyCount++;
    }
    if (whiteKeyCount <= 0) return;

    float whiteKeyW = bounds.w / whiteKeyCount;
    float blackKeyW = whiteKeyW * 0.6f;
    float blackKeyH = bounds.h * 0.6f;

    // Build lookup for active/available notes
    auto isActive = [&](int n) {
        return std::find(activeNotes.begin(), activeNotes.end(), n) != activeNotes.end();
    };
    auto isAvailable = [&](int n) {
        return availableNotes.empty() ||
               std::find(availableNotes.begin(), availableNotes.end(), n) != availableNotes.end();
    };

    // Draw white keys first
    float whiteX = bounds.x;
    for (int n = lowNote; n <= highNote; n++) {
        if (isBlackKey(n)) continue;

        uint32_t color = VizColors::KeyWhite;
        if (isActive(n)) {
            color = VizColors::KeyActive;
        } else if (!isAvailable(n)) {
            color = VizColors::Inactive;
        }

        m_dl->AddRectFilled({whiteX, bounds.y}, {whiteX + whiteKeyW - 1, bounds.bottom()},
                            color, 2.0f);
        m_dl->AddRect({whiteX, bounds.y}, {whiteX + whiteKeyW - 1, bounds.bottom()},
                      VizColors::Border, 0.0f, 0, 1.0f);
        whiteX += whiteKeyW;
    }

    // Draw black keys on top
    whiteX = bounds.x;
    for (int n = lowNote; n <= highNote; n++) {
        if (isBlackKey(n)) {
            // Position black key between previous white key
            float blackX = whiteX - blackKeyW * 0.5f;

            uint32_t color = VizColors::KeyBlack;
            if (isActive(n)) {
                color = VizColors::KeyActive;
            } else if (!isAvailable(n)) {
                color = VIZ_COL32(50, 45, 40, 255);  // Darker unavailable
            }

            m_dl->AddRectFilled({blackX, bounds.y}, {blackX + blackKeyW, bounds.y + blackKeyH},
                                color, 2.0f);
        } else {
            whiteX += whiteKeyW;
        }
    }
}

// =============================================================================
// Gate & Status
// =============================================================================

void VizHelpers::drawGate(const VizBounds& bounds, bool isOpen, float openAmount) {
    openAmount = std::clamp(openAmount, 0.0f, 1.0f);

    // Draw gate frame
    m_dl->AddRect({bounds.x, bounds.y}, {bounds.right(), bounds.bottom()},
                  VizColors::Border, 2.0f, 0, 1.5f);

    // Draw vertical bars
    int numBars = 4;
    float barSpacing = bounds.w / (numBars + 1);
    uint32_t barColor = isOpen ? VizColors::StatusOpen : VizColors::StatusClosed;

    for (int i = 0; i < numBars; i++) {
        float bx = bounds.x + barSpacing * (i + 1);
        float separation = (bounds.h - 8) * (1.0f - openAmount) * 0.5f;
        float barTop = bounds.y + 4 + separation;
        float barBot = bounds.bottom() - 4 - separation;

        m_dl->AddLine({bx, barTop}, {bx, barBot}, barColor, 2.0f);
    }

    // Status text
    const char* status = isOpen ? "OPEN" : "GATE";
    VizBounds labelB = bounds.splitBottom(0.25f);
    drawLabel(labelB, status, barColor);
}

void VizHelpers::drawActivityDot(float cx, float cy, float intensity,
                                  uint32_t color) {
    intensity = std::clamp(intensity, 0.0f, 1.0f);
    float radius = 3.0f + intensity * 3.0f;

    // Outer glow
    if (intensity > 0.1f) {
        uint32_t glowColor = VizColors::lerp(VIZ_COL32(0, 0, 0, 0), color, intensity * 0.5f);
        m_dl->AddCircleFilled({cx, cy}, radius * 1.5f, glowColor);
    }

    // Core dot
    uint32_t coreColor = VizColors::lerp(VizColors::Inactive, color, intensity);
    m_dl->AddCircleFilled({cx, cy}, radius, coreColor);
}

// =============================================================================
// Text & Labels
// =============================================================================

void VizHelpers::drawLabel(const VizBounds& bounds, const char* text,
                            uint32_t color) {
    VizTextSize size = m_dl->CalcTextSize(text);
    float x = bounds.cx() - size.x * 0.5f;
    float y = bounds.cy() - size.y * 0.5f;
    m_dl->AddText({x, y}, color, text);
}

void VizHelpers::drawValue(const VizBounds& bounds, float value,
                            const char* suffix, int precision) {
    char buf[32];
    if (precision == 0) {
        snprintf(buf, sizeof(buf), "%.0f%s", value, suffix);
    } else if (precision == 1) {
        snprintf(buf, sizeof(buf), "%.1f%s", value, suffix);
    } else {
        snprintf(buf, sizeof(buf), "%.2f%s", value, suffix);
    }

    drawLabel(bounds, buf);
}

} // namespace vivid
