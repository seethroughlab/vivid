#include <vivid/audio/envelope.h>
#include <vivid/context.h>
#include <algorithm>
#include <cmath>

namespace vivid::audio {

void Envelope::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    reset();
    m_initialized = true;
}

void Envelope::process(Context& ctx) {
    // Main thread: no-op for pull-based audio
    // All processing happens in generateBlock()
}

void Envelope::generateBlock(uint32_t frameCount) {
    if (!m_initialized) return;

    // Resize output buffer if needed
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    const AudioBuffer* in = inputBuffer();

    // Process each sample
    for (uint32_t i = 0; i < frameCount; ++i) {
        // Compute envelope value for this sample
        float env = computeEnvelopeValue();

        // Apply envelope to input (or output envelope value if no input)
        if (in && in->isValid()) {
            m_output.samples[i * 2] = in->samples[i * 2] * env;
            m_output.samples[i * 2 + 1] = in->samples[i * 2 + 1] * env;
        } else {
            // No input - just output the envelope value itself
            // (useful for modulation)
            m_output.samples[i * 2] = env;
            m_output.samples[i * 2 + 1] = env;
        }

        // Advance envelope by 1 sample
        advanceEnvelope(1);
    }
}

void Envelope::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void Envelope::trigger() {
    m_stage = EnvelopeStage::Attack;
    m_stageProgress = 0.0f;
    // Note: don't reset m_currentValue - allows retriggering from current level
}

void Envelope::releaseNote() {
    if (m_stage != EnvelopeStage::Idle && m_stage != EnvelopeStage::Release) {
        m_releaseStartValue = m_currentValue;
        m_stage = EnvelopeStage::Release;
        m_stageProgress = 0.0f;
    }
}

void Envelope::reset() {
    m_stage = EnvelopeStage::Idle;
    m_currentValue = 0.0f;
    m_stageProgress = 0.0f;
    m_releaseStartValue = 0.0f;
}

float Envelope::computeEnvelopeValue() {
    switch (m_stage) {
        case EnvelopeStage::Idle:
            return 0.0f;

        case EnvelopeStage::Attack:
            // Linear ramp from current value to 1
            return m_currentValue + (1.0f - m_currentValue) * m_stageProgress;

        case EnvelopeStage::Decay: {
            // Exponential decay from 1 to sustain level
            float sustainLevel = static_cast<float>(m_sustain);
            return 1.0f + (sustainLevel - 1.0f) * m_stageProgress;
        }

        case EnvelopeStage::Sustain:
            return static_cast<float>(m_sustain);

        case EnvelopeStage::Release:
            // Linear ramp from release start value to 0
            return m_releaseStartValue * (1.0f - m_stageProgress);

        default:
            return 0.0f;
    }
}

void Envelope::advanceEnvelope(uint32_t samples) {
    if (m_stage == EnvelopeStage::Idle || m_stage == EnvelopeStage::Sustain) {
        m_currentValue = computeEnvelopeValue();
        return;
    }

    // Get stage duration in samples
    float stageDuration = 0.0f;
    switch (m_stage) {
        case EnvelopeStage::Attack:
            stageDuration = static_cast<float>(m_attack) * m_sampleRate;
            break;
        case EnvelopeStage::Decay:
            stageDuration = static_cast<float>(m_decay) * m_sampleRate;
            break;
        case EnvelopeStage::Release:
            stageDuration = static_cast<float>(m_release) * m_sampleRate;
            break;
        default:
            break;
    }

    // Advance progress
    if (stageDuration > 0.0f) {
        m_stageProgress += static_cast<float>(samples) / stageDuration;
    }

    // Update current value
    m_currentValue = computeEnvelopeValue();

    // Check for stage transitions
    if (m_stageProgress >= 1.0f) {
        switch (m_stage) {
            case EnvelopeStage::Attack:
                m_stage = EnvelopeStage::Decay;
                m_stageProgress = 0.0f;
                m_currentValue = 1.0f;
                break;

            case EnvelopeStage::Decay:
                m_stage = EnvelopeStage::Sustain;
                m_stageProgress = 0.0f;
                m_currentValue = static_cast<float>(m_sustain);
                break;

            case EnvelopeStage::Release:
                m_stage = EnvelopeStage::Idle;
                m_stageProgress = 0.0f;
                m_currentValue = 0.0f;
                break;

            default:
                break;
        }
    }
}

} // namespace vivid::audio
