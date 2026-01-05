#include <vivid/audio/decay.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Decay::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    reset();
    m_initialized = true;
}

void Decay::process(Context& ctx) {
    if (!m_initialized) return;

    const AudioBuffer* in = inputBuffer();

    // Get frame count from context (variable based on render framerate)
    uint32_t frames = ctx.audioFramesThisFrame();
    if (m_output.frameCount != frames) {
        m_output.resize(frames);
    }

    float decaySamples = static_cast<float>(time) * m_sampleRate;
    float progressInc = (decaySamples > 0) ? (1.0f / decaySamples) : 1.0f;

    for (uint32_t i = 0; i < frames; ++i) {
        // Compute envelope value
        m_value = computeValue(m_progress);

        // Apply to input or output raw envelope
        if (in && in->isValid()) {
            m_output.samples[i * 2] = in->samples[i * 2] * m_value;
            m_output.samples[i * 2 + 1] = in->samples[i * 2 + 1] * m_value;
        } else {
            m_output.samples[i * 2] = m_value;
            m_output.samples[i * 2 + 1] = m_value;
        }

        // Advance progress
        if (m_progress < 1.0f) {
            m_progress += progressInc;
            if (m_progress > 1.0f) m_progress = 1.0f;
        }
    }
}

void Decay::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void Decay::trigger() {
    m_progress = 0.0f;
    m_value = 1.0f;
}

void Decay::reset() {
    m_progress = 1.0f;
    m_value = 0.0f;
}

float Decay::computeValue(float progress) const {
    if (progress >= 1.0f) return 0.0f;

    switch (m_curve) {
        case DecayCurve::Linear:
            return 1.0f - progress;

        case DecayCurve::Exponential:
            // Exponential decay: e^(-5*progress) gives ~0.007 at progress=1
            return std::exp(-5.0f * progress);

        case DecayCurve::Logarithmic:
            // Logarithmic: slow start, fast end
            return 1.0f - std::log(1.0f + progress * 9.0f) / std::log(10.0f);

        default:
            return 1.0f - progress;
    }
}

} // namespace vivid::audio
