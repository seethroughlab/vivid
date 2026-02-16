#include <vivid/audio/ar.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

REGISTER_OPERATOR_EX(AR, "Audio Envelope", "Attack-release envelope generator", false, vivid::OutputKind::Audio);

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
                stageDuration = static_cast<float>(attack) * m_sampleRate;
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
                stageDuration = static_cast<float>(release) * m_sampleRate;
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

void AR::handleEvent(const AudioEvent& event) {
    if (event.type == AudioEventType::Trigger) {
        float velocity = (event.value1 > 0.0f) ? event.value1 : 1.0f;
        midiNoteOn(0, velocity, 0);
    } else if (event.type == AudioEventType::Reset) {
        reset();
    }
}

void AR::midiNoteOn(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    m_stage = ARStage::Attack;
    m_progress = 0.0f;
}

void AR::midiNoteOff(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    // One-shot envelope — no-op on note off
}

void AR::reset() {
    m_stage = ARStage::Idle;
    m_value = 0.0f;
    m_progress = 0.0f;
}

} // namespace vivid::audio
