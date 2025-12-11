#include <vivid/audio/clap.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Clap::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    reset();
    m_initialized = true;
}

void Clap::process(Context& ctx) {
    if (!m_initialized) return;

    uint32_t frames = m_output.frameCount;

    float decayTime = static_cast<float>(m_decay) * m_sampleRate;
    float toneAmt = static_cast<float>(m_tone);
    float spreadAmt = static_cast<float>(m_spread);
    float vol = static_cast<float>(m_volume);

    float decayRate = (decayTime > 0) ? (1.0f / decayTime) : 1.0f;

    for (uint32_t i = 0; i < frames; ++i) {
        float sample = 0.0f;

        // Sum all burst envelopes
        for (int b = 0; b < NUM_BURSTS; ++b) {
            if (m_samplesSinceTrigger >= m_burstDelay[b] && m_burstEnv[b] > 0.0001f) {
                float noise = generateNoise();
                sample += noise * m_burstEnv[b];

                // Decay each burst independently (fast attack for snap)
                m_burstEnv[b] *= 0.99f;
            }
        }

        // Bandpass filter for clap character (around 1-2kHz)
        sample = bandpass(sample, 0);

        // Apply overall envelope
        sample *= m_env * vol;

        // Output
        m_output.samples[i * 2] = sample;
        m_output.samples[i * 2 + 1] = sample;

        // Advance time
        m_samplesSinceTrigger++;

        // Decay overall envelope
        m_env *= (1.0f - decayRate * 0.2f);
    }
}

void Clap::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void Clap::trigger() {
    m_env = 1.0f;
    m_samplesSinceTrigger = 0;

    float spreadAmt = static_cast<float>(m_spread);

    // Set up burst delays (spread determines timing between bursts)
    // Base spacing: ~10-30ms between bursts
    uint32_t baseSpacing = static_cast<uint32_t>(m_sampleRate * 0.015f);  // 15ms base
    uint32_t spreadRange = static_cast<uint32_t>(m_sampleRate * 0.02f * spreadAmt);  // 0-20ms variation

    m_burstDelay[0] = 0;
    for (int b = 1; b < NUM_BURSTS; ++b) {
        // Add some randomness to timing
        uint32_t variation = (m_seed >> (b * 3)) % (spreadRange + 1);
        m_burstDelay[b] = m_burstDelay[b - 1] + baseSpacing + variation;
        m_burstEnv[b] = 1.0f;
    }
    m_burstEnv[0] = 1.0f;

    // Randomize seed for next trigger
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
}

void Clap::reset() {
    m_env = 0.0f;
    m_samplesSinceTrigger = 0;
    for (int b = 0; b < NUM_BURSTS; ++b) {
        m_burstEnv[b] = 0.0f;
        m_burstDelay[b] = 0;
    }
    m_bpState1[0] = m_bpState1[1] = 0.0f;
    m_bpState2[0] = m_bpState2[1] = 0.0f;
}

float Clap::generateNoise() {
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return (static_cast<float>(m_seed) / 2147483648.0f) - 1.0f;
}

float Clap::bandpass(float in, int ch) {
    // Bandpass around 1.5kHz with moderate Q
    float freq = 1500.0f + static_cast<float>(m_tone) * 1000.0f;  // 1.5kHz to 2.5kHz
    float Q = 1.5f;
    float omega = 6.28318530718f * freq / m_sampleRate;
    float alpha = std::sin(omega) / (2.0f * Q);

    // State-variable filter (simplified)
    float lp = m_bpState1[ch] + alpha * m_bpState2[ch];
    float hp = in - lp - Q * m_bpState2[ch];
    float bp = alpha * hp + m_bpState2[ch];

    m_bpState1[ch] = lp;
    m_bpState2[ch] = bp;

    return bp * 3.0f;  // Boost bandpass output
}

bool Clap::getParam(const std::string& name, float out[4]) {
    if (name == "decay") { out[0] = m_decay; return true; }
    if (name == "tone") { out[0] = m_tone; return true; }
    if (name == "spread") { out[0] = m_spread; return true; }
    if (name == "volume") { out[0] = m_volume; return true; }
    return false;
}

bool Clap::setParam(const std::string& name, const float value[4]) {
    if (name == "decay") { m_decay = value[0]; return true; }
    if (name == "tone") { m_tone = value[0]; return true; }
    if (name == "spread") { m_spread = value[0]; return true; }
    if (name == "volume") { m_volume = value[0]; return true; }
    return false;
}

} // namespace vivid::audio
