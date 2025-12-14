#include <vivid/audio/reverb.h>
#include <vivid/context.h>

namespace vivid::audio {

// Static constexpr definitions
constexpr int Reverb::COMB_DELAYS_L[NUM_COMBS];
constexpr int Reverb::COMB_DELAYS_R[NUM_COMBS];
constexpr int Reverb::ALLPASS_DELAYS[NUM_ALLPASS];

void Reverb::initEffect(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;

    // Scale factor from 44.1kHz to our sample rate
    float scale = static_cast<float>(m_sampleRate) / 44100.0f;

    // Initialize comb filters
    for (int i = 0; i < NUM_COMBS; ++i) {
        m_combsL[i].init(static_cast<uint32_t>(COMB_DELAYS_L[i] * scale));
        m_combsR[i].init(static_cast<uint32_t>(COMB_DELAYS_R[i] * scale));
    }

    // Initialize all-pass filters
    for (int i = 0; i < NUM_ALLPASS; ++i) {
        uint32_t delay = static_cast<uint32_t>(ALLPASS_DELAYS[i] * scale);
        m_allpassL[i].init(delay);
        m_allpassR[i].init(delay);
        m_allpassL[i].setFeedback(0.5f);
        m_allpassR[i].setFeedback(0.5f);
    }

    updateParameters();
}

void Reverb::updateParameters() {
    // Convert room size to feedback (0.7 - 0.98)
    float feedback = 0.7f + static_cast<float>(roomSize) * 0.28f;
    float damp = static_cast<float>(damping);

    for (int i = 0; i < NUM_COMBS; ++i) {
        m_combsL[i].setFeedback(feedback);
        m_combsR[i].setFeedback(feedback);
        m_combsL[i].setDamping(damp);
        m_combsR[i].setDamping(damp);
    }
}

void Reverb::processEffect(const float* input, float* output, uint32_t frames) {
    // Update parameters in case they changed
    updateParameters();

    float w = static_cast<float>(width);

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Mix input to mono for reverb processing
        float in = (inL + inR) * 0.5f;

        // Parallel comb filters (scaled input to prevent clipping)
        float combOutL = 0.0f;
        float combOutR = 0.0f;
        float scaledIn = in * 0.125f;  // 1/8 since we sum 8 combs

        for (int c = 0; c < NUM_COMBS; ++c) {
            combOutL += m_combsL[c].process(scaledIn);
            combOutR += m_combsR[c].process(scaledIn);
        }

        // Series all-pass filters for diffusion
        float outL = combOutL;
        float outR = combOutR;

        for (int a = 0; a < NUM_ALLPASS; ++a) {
            outL = m_allpassL[a].process(outL);
            outR = m_allpassR[a].process(outR);
        }

        // Apply stereo width
        float mid = (outL + outR) * 0.5f;
        float side = (outL - outR) * 0.5f * w;

        output[i * 2] = mid + side;
        output[i * 2 + 1] = mid - side;
    }
}

void Reverb::cleanupEffect() {
    for (int i = 0; i < NUM_COMBS; ++i) {
        m_combsL[i].reset();
        m_combsR[i].reset();
    }
    for (int i = 0; i < NUM_ALLPASS; ++i) {
        m_allpassL[i].reset();
        m_allpassR[i].reset();
    }
}

} // namespace vivid::audio
