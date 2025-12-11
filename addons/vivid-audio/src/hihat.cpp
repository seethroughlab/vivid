#include <vivid/audio/hihat.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void HiHat::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    reset();
    m_initialized = true;
}

void HiHat::process(Context& ctx) {
    if (!m_initialized) return;

    uint32_t frames = m_output.frameCount;

    float decayTime = static_cast<float>(m_decay) * m_sampleRate;
    float toneAmt = static_cast<float>(m_tone);
    float ringAmt = static_cast<float>(m_ring);
    float vol = static_cast<float>(m_volume);

    float decayRate = (decayTime > 0) ? (1.0f / decayTime) : 1.0f;

    // Ring oscillator frequencies (metallic hi-hat character)
    // Based on 808 hi-hat - 6 square wave oscillators
    const float ringFreqs[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};

    for (uint32_t i = 0; i < frames; ++i) {
        // Generate noise base
        float noise = generateNoise();

        // Add metallic ring (sum of square waves at inharmonic frequencies)
        float ring = 0.0f;
        if (ringAmt > 0.0f) {
            for (int r = 0; r < 6; ++r) {
                float phaseInc = ringFreqs[r] / m_sampleRate;
                ring += (m_ringPhase[r] < 0.5f) ? 1.0f : -1.0f;
                m_ringPhase[r] += phaseInc;
                if (m_ringPhase[r] >= 1.0f) m_ringPhase[r] -= 1.0f;
            }
            ring /= 6.0f;
        }

        // Mix noise and ring
        float sample = noise * (1.0f - ringAmt * 0.5f) + ring * ringAmt;

        // Apply highpass filter for brightness
        sample = highpass(sample, 0);

        // Additional bandpass for tone shaping
        float cutoff = 4000.0f + toneAmt * 8000.0f;  // 4kHz to 12kHz
        sample = bandpass(sample, 0);

        // Apply envelope
        sample *= m_env * vol;

        // Output
        m_output.samples[i * 2] = sample;
        m_output.samples[i * 2 + 1] = sample;

        // Decay envelope (exponential)
        m_env *= (1.0f - decayRate * 0.3f);
    }
}

void HiHat::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void HiHat::trigger() {
    m_env = 1.0f;
}

void HiHat::choke() {
    m_env = 0.0f;
}

void HiHat::reset() {
    m_env = 0.0f;
    for (int i = 0; i < 6; ++i) m_ringPhase[i] = 0.0f;
    m_bpState1[0] = m_bpState1[1] = 0.0f;
    m_bpState2[0] = m_bpState2[1] = 0.0f;
    m_hpState[0] = m_hpState[1] = 0.0f;
}

float HiHat::generateNoise() {
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return (static_cast<float>(m_seed) / 2147483648.0f) - 1.0f;
}

float HiHat::bandpass(float in, int ch) {
    // Simple 2-pole bandpass around 8kHz
    float freq = 8000.0f;
    float Q = 2.0f;
    float omega = TWO_PI * freq / m_sampleRate;
    float alpha = std::sin(omega) / (2.0f * Q);
    float cosOmega = std::cos(omega);

    // Simplified state-variable filter
    float lp = m_bpState1[ch] + alpha * m_bpState2[ch];
    float hp = in - lp - Q * m_bpState2[ch];
    float bp = alpha * hp + m_bpState2[ch];

    m_bpState1[ch] = lp;
    m_bpState2[ch] = bp;

    return bp;
}

float HiHat::highpass(float in, int ch) {
    // One-pole highpass at ~7kHz
    float alpha = 0.7f;  // Approximate for high cutoff
    float out = alpha * (m_hpState[ch] + in);
    float delta = in - m_hpState[ch];
    m_hpState[ch] = in;
    return out * 0.5f + delta * 0.5f;
}

bool HiHat::getParam(const std::string& name, float out[4]) {
    if (name == "decay") { out[0] = m_decay; return true; }
    if (name == "tone") { out[0] = m_tone; return true; }
    if (name == "ring") { out[0] = m_ring; return true; }
    if (name == "volume") { out[0] = m_volume; return true; }
    return false;
}

bool HiHat::setParam(const std::string& name, const float value[4]) {
    if (name == "decay") { m_decay = value[0]; return true; }
    if (name == "tone") { m_tone = value[0]; return true; }
    if (name == "ring") { m_ring = value[0]; return true; }
    if (name == "volume") { m_volume = value[0]; return true; }
    return false;
}

} // namespace vivid::audio
