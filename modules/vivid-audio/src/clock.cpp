#include <vivid/audio/clock.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <vivid/viz_helpers.h>
#include <cmath>
#include <cstdio>

namespace vivid::audio {

REGISTER(Clock);

void Clock::init(Context& ctx) {
    m_sampleRate = 48000;  // Standard audio sample rate
    reset();
    m_initialized = true;
}

void Clock::process(Context& ctx) {
    if (!m_initialized || !m_running) {
        m_triggered = false;
        return;
    }

    // Calculate samples per beat based on BPM and division
    float beatsPerSecond = static_cast<float>(bpm) / 60.0f;
    float divMultiplier = getDivisionMultiplier();
    float triggersPerSecond = beatsPerSecond * divMultiplier;

    // Phase increment per frame (sample-accurate, not frame-rate dependent)
    double phaseInc = triggersPerSecond * (ctx.audioFramesThisFrame() / static_cast<double>(m_sampleRate));

    // Check for trigger
    double oldPhase = m_phase;
    m_phase += phaseInc;

    m_triggered = false;

    // Get swing amount
    float swingAmt = static_cast<float>(swing);

    // Detect phase wrap (trigger point)
    if (m_phase >= 1.0) {
        m_phase -= 1.0;

        // Apply swing to even beats
        bool isOddBeat = (m_triggerCount % 2) == 1;

        if (!isOddBeat || swingAmt == 0.0f) {
            m_triggered = true;
            m_triggerCount++;

            if (m_callback) {
                m_callback();
            }
        }
        m_lastTickOdd = isOddBeat;
    }

    // Handle swing delay (trigger odd beats late)
    if (swingAmt > 0.0f && m_lastTickOdd) {
        float swingDelay = swingAmt * 0.33f;  // Max 33% of beat
        if (oldPhase < swingDelay && m_phase >= swingDelay) {
            m_triggered = true;
            m_triggerCount++;

            if (m_callback) {
                m_callback();
            }
            m_lastTickOdd = false;
        }
    }
}

void Clock::cleanup() {
    m_initialized = false;
}

void Clock::reset() {
    m_phase = 0.0;
    m_triggerCount = 0;
    m_triggered = false;
    m_lastTickOdd = false;
}

float Clock::getDivisionMultiplier() const {
    switch (m_division) {
        case ClockDiv::Whole:          return 0.25f;
        case ClockDiv::Half:           return 0.5f;
        case ClockDiv::Quarter:        return 1.0f;
        case ClockDiv::Eighth:         return 2.0f;
        case ClockDiv::Sixteenth:      return 4.0f;
        case ClockDiv::ThirtySecond:   return 8.0f;
        case ClockDiv::DottedQuarter:  return 0.667f;
        case ClockDiv::DottedEighth:   return 1.333f;
        case ClockDiv::TripletQuarter: return 1.5f;
        case ClockDiv::TripletEighth:  return 3.0f;
        default:                       return 1.0f;
    }
}

bool Clock::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    viz.drawBackground(bounds);

    // Draw beat grid (4 beats per bar)
    constexpr int BEATS_PER_BAR = 4;
    float dotSpacing = (bounds.w - 16) / (BEATS_PER_BAR - 1);
    float dotY = bounds.cy() - 8;
    float startX = bounds.x + 8;

    uint32_t currentBeat = beat();

    for (int i = 0; i < BEATS_PER_BAR; ++i) {
        float x = startX + i * dotSpacing;
        float radius = 5.0f;

        bool isCurrent = (i == static_cast<int>(currentBeat)) && m_running;
        bool isDownbeat = (i == 0);

        // Color based on state
        uint32_t color;
        if (isCurrent && m_triggered) {
            color = VIZ_COL32(255, 200, 100, 255);  // Gold flash on trigger
        } else if (isCurrent) {
            color = VIZ_COL32(100, 200, 255, 255);  // Blue for current beat
        } else if (isDownbeat) {
            color = VIZ_COL32(150, 150, 170, 255);  // Lighter for downbeat
        } else {
            color = VIZ_COL32(80, 80, 100, 255);    // Dim for other beats
        }

        dl->AddCircleFilled({x, dotY}, radius, color);

        // Draw outline on downbeat
        if (isDownbeat) {
            dl->AddCircle({x, dotY}, radius + 1.0f, VIZ_COL32(180, 180, 200, 200), 0, 1.0f);
        }
    }

    // Draw bar indicator at left
    uint32_t barNum = bar();
    char barStr[16];
    snprintf(barStr, sizeof(barStr), "%u", barNum + 1);
    dl->AddText({bounds.x + 2, bounds.y + 12}, VizColors::TextDim, barStr);

    // Draw BPM and status at bottom
    char label[32];
    snprintf(label, sizeof(label), "%.0f BPM", static_cast<float>(bpm));
    VizBounds labelBounds = bounds.splitBottom(0.25f);
    viz.drawLabel(labelBounds, label, m_running ? VizColors::TextSecondary : VizColors::TextDim);

    return true;
}

} // namespace vivid::audio
