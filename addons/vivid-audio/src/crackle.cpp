#include <vivid/audio/crackle.h>
#include <vivid/context.h>

namespace vivid::audio {

void Crackle::init(Context& ctx) {
    allocateOutput();
    m_seed = 54321;
    m_initialized = true;
}

void Crackle::process(Context& ctx) {
    if (!m_initialized) return;

    float density = static_cast<float>(m_density);
    float vol = static_cast<float>(m_volume);
    uint32_t frames = m_output.frameCount;

    for (uint32_t i = 0; i < frames; ++i) {
        float sample = 0.0f;

        // Random chance of impulse
        if (randomFloat() < density) {
            // Random impulse amplitude and polarity
            sample = (randomFloat() * 2.0f - 1.0f) * vol;
        }

        m_output.samples[i * 2] = sample;
        m_output.samples[i * 2 + 1] = sample;
    }
}

void Crackle::cleanup() {
    releaseOutput();
    m_initialized = false;
}

float Crackle::randomFloat() {
    // Fast PRNG
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return static_cast<float>(m_seed & 0x7FFFFFFF) / 2147483648.0f;
}

} // namespace vivid::audio
