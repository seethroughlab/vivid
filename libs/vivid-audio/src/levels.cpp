#include <vivid/audio/levels.h>
#include <vivid/viz_helpers.h>

#include <cmath>
#include <algorithm>

namespace vivid::audio {

void Levels::analyze(const float* input, uint32_t frames, uint32_t channels) {
    if (frames == 0) return;

    float sumSqLeft = 0.0f;
    float sumSqRight = 0.0f;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;

    if (channels == 2) {
        // Stereo: analyze left and right separately
        for (uint32_t i = 0; i < frames; i++) {
            float left = input[i * 2];
            float right = input[i * 2 + 1];

            sumSqLeft += left * left;
            sumSqRight += right * right;

            peakLeft = std::max(peakLeft, std::abs(left));
            peakRight = std::max(peakRight, std::abs(right));
        }
    } else {
        // Mono: use same value for both channels
        for (uint32_t i = 0; i < frames; i++) {
            float sample = input[i];
            sumSqLeft += sample * sample;
            peakLeft = std::max(peakLeft, std::abs(sample));
        }
        sumSqRight = sumSqLeft;
        peakRight = peakLeft;
    }

    // Calculate RMS
    float rmsLeft = std::sqrt(sumSqLeft / frames);
    float rmsRight = std::sqrt(sumSqRight / frames);
    float rmsTotal = std::sqrt((sumSqLeft + sumSqRight) / (frames * 2));
    float peakTotal = std::max(peakLeft, peakRight);

    // Apply smoothing
    float smooth = static_cast<float>(smoothing);
    float attack = 1.0f - smooth;

    m_rmsLeft = m_rmsLeft * smooth + rmsLeft * attack;
    m_rmsRight = m_rmsRight * smooth + rmsRight * attack;
    m_rms = m_rms * smooth + rmsTotal * attack;

    // Peak has faster attack, slower release
    if (peakTotal > m_peak) {
        m_peak = peakTotal;  // Instant attack
    } else {
        m_peak = m_peak * smooth + peakTotal * attack;  // Smooth release
    }
}

float Levels::rmsDb() const {
    if (m_rms <= 0.0f) return -100.0f;
    return 20.0f * std::log10(m_rms);
}

float Levels::peakDb() const {
    if (m_peak <= 0.0f) return -100.0f;
    return 20.0f * std::log10(m_peak);
}

bool Levels::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    viz.drawBackground(bounds);
    viz.drawDualMeter(bounds.inset(4), m_rms, m_peak);
    return true;
}

} // namespace vivid::audio
