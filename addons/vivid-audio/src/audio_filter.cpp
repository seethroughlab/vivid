#include <vivid/audio/audio_filter.h>
#include <vivid/context.h>
#include <imgui.h>
#include <cmath>

namespace vivid::audio {

void AudioFilter::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();

    // Clear filter state
    for (int i = 0; i < 2; ++i) {
        m_x1[i] = m_x2[i] = 0.0f;
        m_y1[i] = m_y2[i] = 0.0f;
    }

    m_needsUpdate = true;
    m_initialized = true;
}

void AudioFilter::process(Context& ctx) {
    if (!m_initialized) return;

    // Check if params changed
    float cutoffVal = static_cast<float>(cutoff);
    float resonanceVal = static_cast<float>(resonance);
    float gainVal = static_cast<float>(gain);
    if (cutoffVal != m_cachedCutoff || resonanceVal != m_cachedResonance || gainVal != m_cachedGain) {
        m_cachedCutoff = cutoffVal;
        m_cachedResonance = resonanceVal;
        m_cachedGain = gainVal;
        m_needsUpdate = true;
    }

    if (m_needsUpdate) {
        updateCoefficients();
        m_needsUpdate = false;
    }

    const AudioBuffer* in = inputBuffer();

    // Get frame count from context (variable based on render framerate)
    uint32_t frames = ctx.audioFramesThisFrame();
    if (m_output.frameCount != frames) {
        m_output.resize(frames);
    }

    if (in && in->isValid()) {
        for (uint32_t i = 0; i < frames; ++i) {
            m_output.samples[i * 2] = processSample(in->samples[i * 2], 0);
            m_output.samples[i * 2 + 1] = processSample(in->samples[i * 2 + 1], 1);
        }
    } else {
        // No input - silence
        for (uint32_t i = 0; i < frames * 2; ++i) {
            m_output.samples[i] = 0.0f;
        }
    }
}

void AudioFilter::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void AudioFilter::updateCoefficients() {
    float freq = static_cast<float>(cutoff);
    float Q = static_cast<float>(resonance);
    float gainDB = static_cast<float>(gain);

    // Clamp frequency to valid range
    freq = std::max(20.0f, std::min(freq, m_sampleRate * 0.49f));

    float omega = 2.0f * 3.14159265358979f * freq / m_sampleRate;
    float sinOmega = std::sin(omega);
    float cosOmega = std::cos(omega);
    float alpha = sinOmega / (2.0f * Q);

    // For shelf and peak filters
    float A = std::pow(10.0f, gainDB / 40.0f);

    switch (m_type) {
        case FilterType::Lowpass:
            m_b0 = (1.0f - cosOmega) / 2.0f;
            m_b1 = 1.0f - cosOmega;
            m_b2 = (1.0f - cosOmega) / 2.0f;
            m_a0 = 1.0f + alpha;
            m_a1 = -2.0f * cosOmega;
            m_a2 = 1.0f - alpha;
            break;

        case FilterType::Highpass:
            m_b0 = (1.0f + cosOmega) / 2.0f;
            m_b1 = -(1.0f + cosOmega);
            m_b2 = (1.0f + cosOmega) / 2.0f;
            m_a0 = 1.0f + alpha;
            m_a1 = -2.0f * cosOmega;
            m_a2 = 1.0f - alpha;
            break;

        case FilterType::Bandpass:
            m_b0 = alpha;
            m_b1 = 0.0f;
            m_b2 = -alpha;
            m_a0 = 1.0f + alpha;
            m_a1 = -2.0f * cosOmega;
            m_a2 = 1.0f - alpha;
            break;

        case FilterType::Notch:
            m_b0 = 1.0f;
            m_b1 = -2.0f * cosOmega;
            m_b2 = 1.0f;
            m_a0 = 1.0f + alpha;
            m_a1 = -2.0f * cosOmega;
            m_a2 = 1.0f - alpha;
            break;

        case FilterType::Lowshelf: {
            float sqrtA = std::sqrt(A);
            m_b0 = A * ((A + 1.0f) - (A - 1.0f) * cosOmega + 2.0f * sqrtA * alpha);
            m_b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosOmega);
            m_b2 = A * ((A + 1.0f) - (A - 1.0f) * cosOmega - 2.0f * sqrtA * alpha);
            m_a0 = (A + 1.0f) + (A - 1.0f) * cosOmega + 2.0f * sqrtA * alpha;
            m_a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosOmega);
            m_a2 = (A + 1.0f) + (A - 1.0f) * cosOmega - 2.0f * sqrtA * alpha;
            break;
        }

        case FilterType::Highshelf: {
            float sqrtA = std::sqrt(A);
            m_b0 = A * ((A + 1.0f) + (A - 1.0f) * cosOmega + 2.0f * sqrtA * alpha);
            m_b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosOmega);
            m_b2 = A * ((A + 1.0f) + (A - 1.0f) * cosOmega - 2.0f * sqrtA * alpha);
            m_a0 = (A + 1.0f) - (A - 1.0f) * cosOmega + 2.0f * sqrtA * alpha;
            m_a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosOmega);
            m_a2 = (A + 1.0f) - (A - 1.0f) * cosOmega - 2.0f * sqrtA * alpha;
            break;
        }

        case FilterType::Peak:
            m_b0 = 1.0f + alpha * A;
            m_b1 = -2.0f * cosOmega;
            m_b2 = 1.0f - alpha * A;
            m_a0 = 1.0f + alpha / A;
            m_a1 = -2.0f * cosOmega;
            m_a2 = 1.0f - alpha / A;
            break;
    }

    // Normalize coefficients
    m_b0 /= m_a0;
    m_b1 /= m_a0;
    m_b2 /= m_a0;
    m_a1 /= m_a0;
    m_a2 /= m_a0;
    m_a0 = 1.0f;
}

float AudioFilter::processSample(float in, int ch) {
    // Direct Form II Transposed
    float out = m_b0 * in + m_x1[ch];
    m_x1[ch] = m_b1 * in - m_a1 * out + m_x2[ch];
    m_x2[ch] = m_b2 * in - m_a2 * out;
    return out;
}

bool AudioFilter::drawVisualization(ImDrawList* dl, float minX, float minY, float maxX, float maxY) {
    ImVec2 min(minX, minY);
    ImVec2 max(maxX, maxY);
    float width = maxX - minX;
    float height = maxY - minY;
    float cx = (minX + maxX) * 0.5f;

    // Dark blue background
    dl->AddRectFilled(min, max, IM_COL32(30, 35, 50, 255), 4.0f);

    float cutoffHz = static_cast<float>(cutoff);
    float q = static_cast<float>(resonance);

    // Draw frequency response curve
    float curveMargin = 6.0f;
    float curveW = width - curveMargin * 2;
    float curveH = height - curveMargin * 2 - 14;  // Room for label
    float curveX = min.x + curveMargin;
    float curveY = min.y + curveMargin;

    // Draw horizontal center line (0 dB)
    float zeroY = curveY + curveH * 0.5f;
    dl->AddLine(ImVec2(curveX, zeroY), ImVec2(curveX + curveW, zeroY),
        IM_COL32(60, 70, 90, 150), 1.0f);

    // Calculate cutoff position on log scale (20Hz to 20kHz)
    float cutoffNorm = std::log10(cutoffHz / 20.0f) / std::log10(1000.0f);
    cutoffNorm = std::max(0.0f, std::min(1.0f, cutoffNorm));
    float cutoffX = curveX + curveW * cutoffNorm;

    // Draw cutoff line
    dl->AddLine(ImVec2(cutoffX, curveY), ImVec2(cutoffX, curveY + curveH),
        IM_COL32(255, 180, 100, 100), 1.0f);

    // Draw filter curve based on type
    ImU32 curveColor = IM_COL32(100, 180, 255, 255);
    constexpr int NUM_POINTS = 32;

    auto computeFilterResponse = [&](float freqNorm) -> float {
        float freq = 20.0f * std::pow(1000.0f, freqNorm);
        float ratio = freq / cutoffHz;

        switch (m_type) {
            case FilterType::Lowpass: {
                float response = 1.0f / std::sqrt(1.0f + std::pow(ratio, 4.0f));
                if (ratio > 0.5f && ratio < 2.0f) {
                    response *= 1.0f + (q - 0.707f) * 0.3f * (1.0f - std::abs(ratio - 1.0f));
                }
                return response;
            }
            case FilterType::Highpass: {
                float invRatio = 1.0f / ratio;
                float response = 1.0f / std::sqrt(1.0f + std::pow(invRatio, 4.0f));
                if (ratio > 0.5f && ratio < 2.0f) {
                    response *= 1.0f + (q - 0.707f) * 0.3f * (1.0f - std::abs(ratio - 1.0f));
                }
                return response;
            }
            case FilterType::Bandpass: {
                float bw = 1.0f / q;
                float response = 1.0f / (1.0f + std::pow((ratio - 1.0f/ratio) / bw, 2.0f));
                return response;
            }
            case FilterType::Notch: {
                float bw = 1.0f / q;
                float response = 1.0f - 1.0f / (1.0f + std::pow((ratio - 1.0f/ratio) / bw, 2.0f));
                return response;
            }
            default:
                return 1.0f;
        }
    };

    // Draw the curve
    for (int i = 0; i < NUM_POINTS - 1; i++) {
        float t1 = i / (float)(NUM_POINTS - 1);
        float t2 = (i + 1) / (float)(NUM_POINTS - 1);

        float r1 = computeFilterResponse(t1);
        float r2 = computeFilterResponse(t2);

        float y1 = curveY + curveH * (1.0f - r1 * 0.9f);
        float y2 = curveY + curveH * (1.0f - r2 * 0.9f);
        float x1 = curveX + curveW * t1;
        float x2 = curveX + curveW * t2;

        dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), curveColor, 2.0f);
    }

    // Filter type label
    const char* typeLabel = "";
    switch (m_type) {
        case FilterType::Lowpass: typeLabel = "LP"; break;
        case FilterType::Highpass: typeLabel = "HP"; break;
        case FilterType::Bandpass: typeLabel = "BP"; break;
        case FilterType::Notch: typeLabel = "NOTCH"; break;
        case FilterType::Lowshelf: typeLabel = "LSHF"; break;
        case FilterType::Highshelf: typeLabel = "HSHF"; break;
        case FilterType::Peak: typeLabel = "PEAK"; break;
    }
    ImVec2 labelSize = ImGui::CalcTextSize(typeLabel);
    dl->AddText(ImVec2(cx - labelSize.x * 0.5f, max.y - 14),
        IM_COL32(180, 200, 255, 255), typeLabel);

    return true;
}

} // namespace vivid::audio
