#include <vivid/audio/glitch/reverse.h>
#include <vivid/operator_registry.h>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace vivid::audio {

REGISTER_OPERATOR_EX(Reverse, "Glitch", "Beat-synced reverse playback effect", false, vivid::OutputKind::Audio);

void Reverse::initEffect(Context& ctx) {
    m_state = State::Passthrough;
    m_triggerPhase = 0.0;
    m_reversePhase = 0.0;
    m_crossfadePos = 0;
    m_crossfadingIn = false;
    m_crossfadingOut = false;
    m_buffer.clear();
}

void Reverse::processEffect(const float* input, float* output, uint32_t frames) {
    float currentBpm = static_cast<float>(bpm);
    uint32_t sampleRate = m_buffer.sampleRate();

    // Calculate trigger interval in samples
    float triggerSamples = static_cast<float>(divisionToSamples(m_triggerDiv, currentBpm, sampleRate));
    float phaseIncPerSample = 1.0f / triggerSamples;

    // Calculate reverse length
    uint32_t reverseSamples = divisionToSamples(m_reverseDiv, currentBpm, sampleRate);

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Always write to circular buffer
        m_buffer.write(inL, inR);

        float outL = inL;
        float outR = inR;
        float wetL = 0.0f;
        float wetR = 0.0f;

        if (m_state == State::Passthrough) {
            // Check for trigger
            m_triggerPhase += phaseIncPerSample;

            if (m_triggerPhase >= 1.0) {
                m_triggerPhase -= 1.0;

                // Roll the dice
                if (random() < static_cast<float>(chance)) {
                    // Start reversing
                    m_state = State::Reversing;
                    m_reverseLength = reverseSamples;
                    // Start from current position, will read backwards
                    m_reverseStart = m_buffer.getReadPosition(m_reverseLength);
                    m_reversePhase = 0.0;
                    m_crossfadingIn = true;
                    m_crossfadingOut = false;
                    m_crossfadePos = 0;
                }
            }

        } else {
            // Reversing state - read backwards from buffer
            // reversePhase goes from 0 to reverseLength
            // We read from (reverseStart + reverseLength - 1) down to reverseStart
            double readPos = static_cast<double>(m_reverseStart) +
                            static_cast<double>(m_reverseLength - 1) - m_reversePhase;

            m_buffer.read(readPos, wetL, wetR);

            // Apply crossfade envelope
            float envelope = 1.0f;
            if (m_crossfadingIn) {
                envelope = static_cast<float>(m_crossfadePos) / CROSSFADE_SAMPLES;
                m_crossfadePos++;
                if (m_crossfadePos >= CROSSFADE_SAMPLES) {
                    m_crossfadingIn = false;
                }
            } else if (m_crossfadingOut) {
                envelope = 1.0f - static_cast<float>(m_crossfadePos) / CROSSFADE_SAMPLES;
                m_crossfadePos++;
                if (m_crossfadePos >= CROSSFADE_SAMPLES) {
                    m_crossfadingOut = false;
                    m_state = State::Passthrough;
                }
            }

            // Check if we should start crossfading out
            if (!m_crossfadingOut && m_reversePhase >= m_reverseLength - CROSSFADE_SAMPLES) {
                m_crossfadingOut = true;
                m_crossfadePos = 0;
            }

            wetL *= envelope;
            wetR *= envelope;

            // Advance playback phase
            m_reversePhase += 1.0;

            // Check if we've finished (with crossfade)
            if (m_reversePhase >= m_reverseLength && !m_crossfadingOut) {
                m_state = State::Passthrough;
            }
        }

        // Apply mix - blend between dry (input) and wet (reversed)
        float mixAmt = static_cast<float>(this->mix);
        if (m_state == State::Reversing || m_crossfadingOut) {
            outL = inL * (1.0f - mixAmt) + wetL * mixAmt;
            outR = inR * (1.0f - mixAmt) + wetR * mixAmt;
        }

        output[i * 2] = outL;
        output[i * 2 + 1] = outR;
    }
}

} // namespace vivid::audio
