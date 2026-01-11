#include <vivid/audio/glitch/stutter.h>
#include <vivid/operator_registry.h>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace vivid::audio {

REGISTER_OPERATOR_EX(Stutter, "Glitch", "Beat-synced stutter with volume envelopes", false, vivid::OutputKind::Audio);

void Stutter::initEffect(Context& ctx) {
    m_state = State::Passthrough;
    m_triggerPhase = 0.0;
    m_stutterPhase = 0.0;
    m_currentStutter = 0;
    m_totalStutters = 0;
    m_buffer.clear();
}

float Stutter::calculateEnvelope() const {
    if (m_totalStutters <= 1) return 1.0f;

    float progress = static_cast<float>(m_currentStutter) / static_cast<float>(m_totalStutters - 1);
    float amount = static_cast<float>(envAmount);

    switch (m_envelope) {
        case StutterEnvelope::Flat:
            return 1.0f;

        case StutterEnvelope::Decay:
            // 1.0 -> (1.0 - amount)
            return 1.0f - progress * amount;

        case StutterEnvelope::Build:
            // (1.0 - amount) -> 1.0
            return (1.0f - amount) + progress * amount;

        case StutterEnvelope::Triangle: {
            // 0 -> 1 -> 0 (or scaled by amount)
            float tri;
            if (progress < 0.5f) {
                tri = progress * 2.0f;  // 0 -> 1
            } else {
                tri = 2.0f - progress * 2.0f;  // 1 -> 0
            }
            return (1.0f - amount) + tri * amount;
        }

        default:
            return 1.0f;
    }
}

void Stutter::processEffect(const float* input, float* output, uint32_t frames) {
    float currentBpm = static_cast<float>(bpm);
    uint32_t sampleRate = m_buffer.sampleRate();

    // Calculate trigger interval in samples
    float triggerSamples = static_cast<float>(divisionToSamples(m_triggerDiv, currentBpm, sampleRate));
    float phaseIncPerSample = 1.0f / triggerSamples;

    // Calculate stutter length
    uint32_t stutterSamples = divisionToSamples(m_stutterDiv, currentBpm, sampleRate);

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Always write to circular buffer
        m_buffer.write(inL, inR);

        float outL, outR;

        if (m_state == State::Passthrough) {
            // Check for trigger
            m_triggerPhase += phaseIncPerSample;

            if (m_triggerPhase >= 1.0) {
                m_triggerPhase -= 1.0;

                // Roll the dice
                if (random() < static_cast<float>(chance)) {
                    // Start stuttering
                    m_state = State::Stuttering;
                    m_stutterLength = stutterSamples;
                    m_stutterStart = m_buffer.getReadPosition(m_stutterLength);
                    m_stutterPhase = 0.0;
                    m_currentStutter = 0;
                    m_totalStutters = static_cast<int>(stutterCount);
                }
            }

            // Pass through
            outL = inL;
            outR = inR;

        } else {
            // Stuttering state - play back from buffer with envelope
            double readPos = static_cast<double>(m_stutterStart) + m_stutterPhase;
            m_buffer.read(readPos, outL, outR);

            // Apply envelope
            float env = calculateEnvelope();
            outL *= env;
            outR *= env;

            // Advance playback phase
            m_stutterPhase += 1.0;

            // Check if we've finished this stutter
            if (m_stutterPhase >= m_stutterLength) {
                m_stutterPhase = 0.0;
                m_currentStutter++;

                // Check if we've done all stutters
                if (m_currentStutter >= m_totalStutters) {
                    m_state = State::Passthrough;
                }
            }
        }

        // Apply mix
        float mixAmt = static_cast<float>(this->mix);
        output[i * 2] = inL * (1.0f - mixAmt) + outL * mixAmt;
        output[i * 2 + 1] = inR * (1.0f - mixAmt) + outR * mixAmt;
    }
}

} // namespace vivid::audio
