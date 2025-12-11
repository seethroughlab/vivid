#include <vivid/audio/synth.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Synth::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    reset();
    m_initialized = true;
}

void Synth::process(Context& ctx) {
    if (!m_initialized) return;

    // Calculate effective frequency with detune
    float baseFreq = static_cast<float>(m_frequency);
    float detuneRatio = centsToRatio(static_cast<float>(m_detune));
    float freq = baseFreq * detuneRatio;

    // Phase increment per sample
    float phaseInc = freq / static_cast<float>(m_sampleRate);

    float vol = static_cast<float>(m_volume);
    uint32_t frames = m_output.frameCount;

    for (uint32_t i = 0; i < frames; ++i) {
        // Compute envelope
        float env = computeEnvelope();

        // Generate sample with envelope
        float sample = generateSample(m_phase) * vol * env;

        // Write stereo
        m_output.samples[i * 2] = sample;
        m_output.samples[i * 2 + 1] = sample;

        // Advance phase (wrap at 1.0)
        m_phase += phaseInc;
        if (m_phase >= 1.0f) m_phase -= 1.0f;

        // Advance envelope
        advanceEnvelope(1);
    }
}

void Synth::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void Synth::noteOn() {
    m_envStage = EnvelopeStage::Attack;
    m_envProgress = 0.0f;
}

void Synth::noteOn(float hz) {
    m_frequency = hz;
    noteOn();
}

void Synth::noteOff() {
    if (m_envStage != EnvelopeStage::Idle && m_envStage != EnvelopeStage::Release) {
        m_releaseStartValue = m_envValue;
        m_envStage = EnvelopeStage::Release;
        m_envProgress = 0.0f;
    }
}

void Synth::reset() {
    m_phase = 0.0f;
    m_envStage = EnvelopeStage::Idle;
    m_envValue = 0.0f;
    m_envProgress = 0.0f;
    m_releaseStartValue = 0.0f;
}

float Synth::generateSample(float phase) const {
    switch (m_waveform) {
        case Waveform::Sine:
            return std::sin(phase * TWO_PI);

        case Waveform::Triangle:
            if (phase < 0.5f) {
                return 4.0f * phase - 1.0f;
            } else {
                return 3.0f - 4.0f * phase;
            }

        case Waveform::Square:
            return (phase < 0.5f) ? 1.0f : -1.0f;

        case Waveform::Saw:
            return 2.0f * phase - 1.0f;

        case Waveform::Pulse:
            return (phase < static_cast<float>(m_pulseWidth)) ? 1.0f : -1.0f;

        default:
            return 0.0f;
    }
}

float Synth::centsToRatio(float cents) const {
    return std::pow(2.0f, cents / 1200.0f);
}

float Synth::computeEnvelope() {
    switch (m_envStage) {
        case EnvelopeStage::Idle:
            return 0.0f;

        case EnvelopeStage::Attack:
            return m_envValue + (1.0f - m_envValue) * m_envProgress;

        case EnvelopeStage::Decay: {
            float sustainLevel = static_cast<float>(m_sustain);
            return 1.0f + (sustainLevel - 1.0f) * m_envProgress;
        }

        case EnvelopeStage::Sustain:
            return static_cast<float>(m_sustain);

        case EnvelopeStage::Release:
            return m_releaseStartValue * (1.0f - m_envProgress);

        default:
            return 0.0f;
    }
}

void Synth::advanceEnvelope(uint32_t samples) {
    if (m_envStage == EnvelopeStage::Idle || m_envStage == EnvelopeStage::Sustain) {
        m_envValue = computeEnvelope();
        return;
    }

    // Get stage duration in samples
    float stageDuration = 0.0f;
    switch (m_envStage) {
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
        m_envProgress += static_cast<float>(samples) / stageDuration;
    }

    // Update current value
    m_envValue = computeEnvelope();

    // Check for stage transitions
    if (m_envProgress >= 1.0f) {
        switch (m_envStage) {
            case EnvelopeStage::Attack:
                m_envStage = EnvelopeStage::Decay;
                m_envProgress = 0.0f;
                m_envValue = 1.0f;
                break;

            case EnvelopeStage::Decay:
                m_envStage = EnvelopeStage::Sustain;
                m_envProgress = 0.0f;
                m_envValue = static_cast<float>(m_sustain);
                break;

            case EnvelopeStage::Release:
                m_envStage = EnvelopeStage::Idle;
                m_envProgress = 0.0f;
                m_envValue = 0.0f;
                break;

            default:
                break;
        }
    }
}

} // namespace vivid::audio
