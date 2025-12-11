#include <vivid/audio/ar.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void AR::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    reset();
    m_initialized = true;
}

void AR::process(Context& ctx) {
    if (!m_initialized) return;

    const AudioBuffer* in = inputBuffer();

    // Get frame count from context (variable based on render framerate)
    uint32_t frames = ctx.audioFramesThisFrame();
    if (m_output.frameCount != frames) {
        m_output.resize(frames);
    }

    for (uint32_t i = 0; i < frames; ++i) {
        // Compute envelope based on stage
        float stageDuration = 0.0f;
        switch (m_stage) {
            case ARStage::Idle:
                m_value = 0.0f;
                break;

            case ARStage::Attack:
                stageDuration = static_cast<float>(m_attack) * m_sampleRate;
                if (stageDuration > 0) {
                    m_progress += 1.0f / stageDuration;
                }
                m_value = m_progress;  // Linear attack
                if (m_progress >= 1.0f) {
                    m_stage = ARStage::Release;
                    m_progress = 0.0f;
                    m_value = 1.0f;
                }
                break;

            case ARStage::Release:
                stageDuration = static_cast<float>(m_release) * m_sampleRate;
                if (stageDuration > 0) {
                    m_progress += 1.0f / stageDuration;
                }
                // Exponential release
                m_value = std::exp(-5.0f * m_progress);
                if (m_progress >= 1.0f) {
                    m_stage = ARStage::Idle;
                    m_progress = 0.0f;
                    m_value = 0.0f;
                }
                break;
        }

        // Apply to input or output raw envelope
        if (in && in->isValid()) {
            m_output.samples[i * 2] = in->samples[i * 2] * m_value;
            m_output.samples[i * 2 + 1] = in->samples[i * 2 + 1] * m_value;
        } else {
            m_output.samples[i * 2] = m_value;
            m_output.samples[i * 2 + 1] = m_value;
        }
    }
}

void AR::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void AR::trigger() {
    m_stage = ARStage::Attack;
    m_progress = 0.0f;
}

void AR::reset() {
    m_stage = ARStage::Idle;
    m_value = 0.0f;
    m_progress = 0.0f;
}

} // namespace vivid::audio
