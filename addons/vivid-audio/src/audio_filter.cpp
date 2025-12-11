#include <vivid/audio/audio_filter.h>
#include <vivid/context.h>
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

    if (m_needsUpdate) {
        updateCoefficients();
        m_needsUpdate = false;
    }

    const AudioBuffer* in = inputBuffer();
    uint32_t frames = m_output.frameCount;

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
    float freq = static_cast<float>(m_cutoff);
    float Q = static_cast<float>(m_resonance);
    float gainDB = static_cast<float>(m_gain);

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

} // namespace vivid::audio
