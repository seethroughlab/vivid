#include <vivid/audio/noise_gen.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void NoiseGen::init(Context& ctx) {
    allocateOutput();
    m_seed = 12345;
    m_b0 = m_b1 = m_b2 = m_b3 = m_b4 = m_b5 = m_b6 = 0.0f;
    m_lastBrown = 0.0f;
    m_initialized = true;
}

void NoiseGen::process(Context& ctx) {
    if (!m_initialized) return;

    float vol = static_cast<float>(volume);

    // Get frame count from context (variable based on render framerate)
    uint32_t frames = ctx.audioFramesThisFrame();
    if (m_output.frameCount != frames) {
        m_output.resize(frames);
    }

    for (uint32_t i = 0; i < frames; ++i) {
        float sample = 0.0f;

        switch (m_color) {
            case NoiseColor::White:
                sample = generateWhite();
                break;
            case NoiseColor::Pink:
                sample = generatePink();
                break;
            case NoiseColor::Brown:
                sample = generateBrown();
                break;
        }

        sample *= vol;

        // Stereo output (same noise both channels)
        m_output.samples[i * 2] = sample;
        m_output.samples[i * 2 + 1] = sample;
    }
}

void NoiseGen::cleanup() {
    releaseOutput();
    m_initialized = false;
}

float NoiseGen::generateWhite() {
    // Fast PRNG (xorshift)
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;

    // Convert to float in range [-1, 1]
    return (static_cast<float>(m_seed) / 2147483648.0f) - 1.0f;
}

float NoiseGen::generatePink() {
    // Paul Kellet's pink noise algorithm
    float white = generateWhite();

    m_b0 = 0.99886f * m_b0 + white * 0.0555179f;
    m_b1 = 0.99332f * m_b1 + white * 0.0750759f;
    m_b2 = 0.96900f * m_b2 + white * 0.1538520f;
    m_b3 = 0.86650f * m_b3 + white * 0.3104856f;
    m_b4 = 0.55000f * m_b4 + white * 0.5329522f;
    m_b5 = -0.7616f * m_b5 - white * 0.0168980f;

    float pink = m_b0 + m_b1 + m_b2 + m_b3 + m_b4 + m_b5 + m_b6 + white * 0.5362f;
    m_b6 = white * 0.115926f;

    return pink * 0.11f;  // Normalize
}

float NoiseGen::generateBrown() {
    // Brown noise: integrate white noise with leaky integrator
    float white = generateWhite();
    m_lastBrown = (m_lastBrown + (0.02f * white)) / 1.02f;
    return m_lastBrown * 3.5f;  // Normalize
}

} // namespace vivid::audio
