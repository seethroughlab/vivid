#include <vivid/audio/oscillator.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Oscillator::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_phaseL = 0.0f;
    m_phaseR = 0.0f;
    m_initialized = true;
}

void Oscillator::process(Context& ctx) {
    if (!m_initialized) return;

    // Calculate effective frequency with detune
    float baseFreq = static_cast<float>(m_frequency);
    float detuneRatio = centsToRatio(static_cast<float>(m_detune));
    float freqL = baseFreq * detuneRatio;
    float freqR = baseFreq * detuneRatio;

    // Apply stereo detune (left goes down, right goes up)
    if (m_stereoDetune > 0.0f) {
        float stereoRatio = centsToRatio(static_cast<float>(m_stereoDetune));
        freqL /= stereoRatio;
        freqR *= stereoRatio;
    }

    // Phase increment per sample
    float phaseIncL = freqL / static_cast<float>(m_sampleRate);
    float phaseIncR = freqR / static_cast<float>(m_sampleRate);

    float vol = static_cast<float>(m_volume);
    uint32_t frames = m_output.frameCount;

    for (uint32_t i = 0; i < frames; ++i) {
        // Generate samples
        float sampleL = generateSample(m_phaseL) * vol;
        float sampleR = generateSample(m_phaseR) * vol;

        // Write to interleaved buffer
        m_output.samples[i * 2] = sampleL;
        m_output.samples[i * 2 + 1] = sampleR;

        // Advance phases (wrap at 1.0)
        m_phaseL += phaseIncL;
        m_phaseR += phaseIncR;

        if (m_phaseL >= 1.0f) m_phaseL -= 1.0f;
        if (m_phaseR >= 1.0f) m_phaseR -= 1.0f;
    }
}

void Oscillator::cleanup() {
    releaseOutput();
    m_initialized = false;
}

float Oscillator::generateSample(float phase) const {
    switch (m_waveform) {
        case Waveform::Sine:
            return std::sin(phase * TWO_PI);

        case Waveform::Triangle:
            // Triangle: ramp up 0->0.5, ramp down 0.5->1
            if (phase < 0.5f) {
                return 4.0f * phase - 1.0f;  // -1 to 1
            } else {
                return 3.0f - 4.0f * phase;  // 1 to -1
            }

        case Waveform::Square:
            return (phase < 0.5f) ? 1.0f : -1.0f;

        case Waveform::Saw:
            // Sawtooth: ramp from -1 to 1
            return 2.0f * phase - 1.0f;

        case Waveform::Pulse:
            // Variable pulse width
            return (phase < static_cast<float>(m_pulseWidth)) ? 1.0f : -1.0f;

        default:
            return 0.0f;
    }
}

float Oscillator::centsToRatio(float cents) const {
    // 100 cents = 1 semitone, 1200 cents = 1 octave
    // ratio = 2^(cents/1200)
    return std::pow(2.0f, cents / 1200.0f);
}

} // namespace vivid::audio
