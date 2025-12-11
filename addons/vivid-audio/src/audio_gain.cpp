#include <vivid/audio/audio_gain.h>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

void AudioGain::processEffect(const float* input, float* output, uint32_t frames) {
    if (m_mute) {
        // Output silence
        for (uint32_t i = 0; i < frames * 2; ++i) {
            output[i] = 0.0f;
        }
        return;
    }

    const float gain = static_cast<float>(m_gain);
    const float pan = static_cast<float>(m_pan);

    // Calculate L/R gains using constant power panning
    // pan: -1 = full left, 0 = center, 1 = full right
    const float panNorm = (pan + 1.0f) * 0.5f;  // 0-1 range
    const float angle = panNorm * 1.5707963f;    // 0 to PI/2
    const float gainL = gain * std::cos(angle);
    const float gainR = gain * std::sin(angle);

    for (uint32_t i = 0; i < frames; ++i) {
        const float inL = input[i * 2];
        const float inR = input[i * 2 + 1];

        output[i * 2] = inL * gainL;
        output[i * 2 + 1] = inR * gainR;
    }
}

} // namespace vivid::audio
