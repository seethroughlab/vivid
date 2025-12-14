#include <vivid/audio/bitcrush.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Bitcrush::initEffect(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    m_holdL = 0.0f;
    m_holdR = 0.0f;
    m_sampleCounter = 0.0f;
}

float Bitcrush::quantize(float sample) {
    // Quantize to n-bit levels
    // Map from [-1, 1] to [0, levels], quantize, map back
    float quantLevels = static_cast<float>(1 << static_cast<int>(bits));
    float scaled = (sample + 1.0f) * 0.5f * quantLevels;
    float quantized = std::floor(scaled);
    return (quantized / quantLevels) * 2.0f - 1.0f;
}

void Bitcrush::processEffect(const float* input, float* output, uint32_t frames) {
    // Calculate how many input samples per output sample
    float sampleRatio = static_cast<float>(m_sampleRate) / static_cast<float>(targetSampleRate);

    for (uint32_t i = 0; i < frames; ++i) {
        m_sampleCounter += 1.0f;

        // When counter exceeds ratio, sample and hold new value
        if (m_sampleCounter >= sampleRatio) {
            m_sampleCounter -= sampleRatio;

            // Read input and quantize
            m_holdL = quantize(input[i * 2]);
            m_holdR = quantize(input[i * 2 + 1]);
        }

        // Output the held (sample-rate-reduced, bit-reduced) values
        output[i * 2] = m_holdL;
        output[i * 2 + 1] = m_holdR;
    }
}

void Bitcrush::cleanupEffect() {
    m_holdL = 0.0f;
    m_holdR = 0.0f;
    m_sampleCounter = 0.0f;
}

} // namespace vivid::audio
