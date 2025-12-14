#include <vivid/audio/pitch_env.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void PitchEnv::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    reset();
    m_initialized = true;
}

void PitchEnv::process(Context& ctx) {
    if (!m_initialized) return;

    // Get frame count from context (variable based on render framerate)
    uint32_t frames = ctx.audioFramesThisFrame();
    if (m_output.frameCount != frames) {
        m_output.resize(frames);
    }
    float sweepSamples = static_cast<float>(time) * m_sampleRate;
    float progressInc = (sweepSamples > 0) ? (1.0f / sweepSamples) : 1.0f;

    float startF = static_cast<float>(startFreq);
    float endF = static_cast<float>(endFreq);

    for (uint32_t i = 0; i < frames; ++i) {
        // Exponential interpolation between frequencies
        if (m_progress < 1.0f) {
            // Exponential curve for natural pitch sweep
            float t = 1.0f - std::exp(-5.0f * m_progress);
            m_currentFreq = startF + (endF - startF) * t;

            m_progress += progressInc;
            if (m_progress > 1.0f) {
                m_progress = 1.0f;
                m_currentFreq = endF;
            }
        }

        // Output current frequency as audio signal (useful for modulation)
        // Normalized: freq / 1000 so typical values are in -1 to 1 range
        float normalized = m_currentFreq / 1000.0f;
        m_output.samples[i * 2] = normalized;
        m_output.samples[i * 2 + 1] = normalized;
    }
}

void PitchEnv::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void PitchEnv::trigger() {
    m_progress = 0.0f;
    m_currentFreq = static_cast<float>(startFreq);
}

void PitchEnv::reset() {
    m_progress = 1.0f;
    m_currentFreq = static_cast<float>(endFreq);
}

} // namespace vivid::audio
