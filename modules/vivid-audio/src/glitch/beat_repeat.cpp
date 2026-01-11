#include <vivid/audio/glitch/beat_repeat.h>
#include <vivid/operator_registry.h>
#include <cstring>
#include <algorithm>

namespace vivid::audio {

REGISTER_OPERATOR_EX(BeatRepeat, "Glitch", "Beat-synced slice repeater for stutter effects", false, vivid::OutputKind::Audio);

void BeatRepeat::initEffect(Context& ctx) {
    m_state = State::Passthrough;
    m_triggerPhase = 0.0;
    m_slicePhase = 0.0;
    m_currentRepeat = 0;
    m_currentGain = 1.0f;
    m_buffer.clear();
}

void BeatRepeat::processEffect(const float* input, float* output, uint32_t frames) {
    float currentBpm = static_cast<float>(bpm);
    uint32_t sampleRate = m_buffer.sampleRate();

    // Calculate trigger interval in samples
    float triggerSamples = static_cast<float>(divisionToSamples(m_triggerDiv, currentBpm, sampleRate));
    float phaseIncPerSample = 1.0f / triggerSamples;

    // Calculate slice length
    uint32_t sliceSamples = divisionToSamples(m_sliceDiv, currentBpm, sampleRate);

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Always write to circular buffer
        m_buffer.write(inL, inR);

        float outL, outR;

        if (m_state == State::Passthrough) {
            // Check for trigger
            double prevPhase = m_triggerPhase;
            m_triggerPhase += phaseIncPerSample;

            if (m_triggerPhase >= 1.0) {
                m_triggerPhase -= 1.0;

                // Roll the dice
                if (random() < static_cast<float>(chance)) {
                    // Start repeating
                    m_state = State::Repeating;
                    m_sliceLength = sliceSamples;
                    m_sliceStart = m_buffer.getReadPosition(m_sliceLength);
                    m_slicePhase = 0.0;
                    m_currentRepeat = 0;
                    m_currentGain = 1.0f;
                }
            }

            // Pass through
            outL = inL;
            outR = inR;

        } else {
            // Repeating state - play back from buffer
            double readPos = static_cast<double>(m_sliceStart) + m_slicePhase;
            m_buffer.read(readPos, outL, outR);

            // Apply decay
            outL *= m_currentGain;
            outR *= m_currentGain;

            // Advance playback phase
            m_slicePhase += 1.0;

            // Check if we've finished this repeat
            if (m_slicePhase >= m_sliceLength) {
                m_slicePhase = 0.0;
                m_currentRepeat++;

                // Apply decay for next repeat
                m_currentGain *= (1.0f - static_cast<float>(decay));

                // Check if we've done all repeats
                if (m_currentRepeat >= static_cast<int>(repeatCount)) {
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
