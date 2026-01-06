#include <vivid/audio/audio_gain.h>
#include <vivid/audio/envelope.h>
#include <vivid/chain.h>
#include <vivid/context.h>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace vivid::audio {

void AudioGain::initEffect(Context& ctx) {
    // Resolve gain modulation input by name
    if (!m_gainInputName.empty()) {
        Operator* op = ctx.chain().getByName(m_gainInputName);
        if (op) {
            m_gainInputOp = op;
            // Add as dependency so it gets processed before us
            setInput(1, op);
        } else {
            std::cerr << "[AudioGain] Gain input '" << m_gainInputName
                      << "' not found" << std::endl;
        }
    }
}

void AudioGain::processEffect(const float* input, float* output, uint32_t frames) {
    if (m_mute) {
        // Output silence
        for (uint32_t i = 0; i < frames * 2; ++i) {
            output[i] = 0.0f;
        }
        return;
    }

    float gainValue = static_cast<float>(gain);

    // Apply gain modulation from envelope or other source
    if (m_gainInputOp) {
        // Check if it's an Envelope operator
        auto* envelope = dynamic_cast<Envelope*>(m_gainInputOp);
        if (envelope) {
            gainValue *= envelope->currentValue();
        }
    }

    const float panValue = static_cast<float>(pan);

    // Calculate L/R gains using constant power panning
    // pan: -1 = full left, 0 = center, 1 = full right
    const float panNorm = (panValue + 1.0f) * 0.5f;  // 0-1 range
    const float angle = panNorm * 1.5707963f;    // 0 to PI/2
    const float gainL = gainValue * std::cos(angle);
    const float gainR = gainValue * std::sin(angle);

    for (uint32_t i = 0; i < frames; ++i) {
        const float inL = input[i * 2];
        const float inR = input[i * 2 + 1];

        output[i * 2] = inL * gainL;
        output[i * 2 + 1] = inR * gainR;
    }
}

} // namespace vivid::audio
