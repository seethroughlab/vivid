#include <vivid/audio/glitch/tape_stop.h>
#include <vivid/operator_registry.h>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

REGISTER_OPERATOR_EX(TapeStop, "Glitch", "Tape deck slowdown/speedup effect", false, vivid::OutputKind::Audio);

void TapeStop::initEffect(Context& ctx) {
    m_state = State::Passthrough;
    m_triggerPhase = 0.0;
    m_playbackRate = 1.0;
    m_readPos = 0.0;
    m_effectPhase = 0;
    m_buffer.clear();
}

void TapeStop::processEffect(const float* input, float* output, uint32_t frames) {
    float currentBpm = static_cast<float>(bpm);
    uint32_t sampleRate = m_buffer.sampleRate();

    // Calculate trigger interval
    float triggerSamples = static_cast<float>(divisionToSamples(m_triggerDiv, currentBpm, sampleRate));
    float phaseIncPerSample = 1.0f / triggerSamples;

    // Calculate stop/start durations in samples
    m_stopSamples = static_cast<uint32_t>(static_cast<float>(stopTime) * sampleRate / 1000.0f);
    m_startSamples = static_cast<uint32_t>(static_cast<float>(startTime) * sampleRate / 1000.0f);
    m_stoppedDuration = sampleRate / 10;  // Stay stopped for 100ms

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Always write to circular buffer
        m_buffer.write(inL, inR);

        float outL = inL;
        float outR = inR;

        switch (m_state) {
            case State::Passthrough: {
                // Check for trigger
                m_triggerPhase += phaseIncPerSample;

                if (m_triggerPhase >= 1.0) {
                    m_triggerPhase -= 1.0;

                    if (random() < static_cast<float>(chance)) {
                        // Start tape effect
                        if (m_mode == TapeMode::Start) {
                            // Start from stopped
                            m_state = State::Starting;
                            m_playbackRate = 0.0;
                        } else {
                            // Stop first
                            m_state = State::Stopping;
                            m_playbackRate = 1.0;
                        }
                        m_effectPhase = 0;
                        m_readPos = 0.0;
                    }
                }
                break;
            }

            case State::Stopping: {
                // Exponential slowdown curve
                float progress = static_cast<float>(m_effectPhase) / static_cast<float>(m_stopSamples);

                // Exponential decay: starts fast, slows down gradually
                // rate = (1 - progress)^2 gives nice tape-like curve
                float curve = 1.0f - progress;
                m_playbackRate = curve * curve * curve;  // Cubic for smoother stop

                // Clamp to minimum
                if (m_playbackRate < 0.01) m_playbackRate = 0.0;

                // Read from buffer at reduced rate
                double bufferPos = m_buffer.getReadPosition(static_cast<size_t>(m_readPos) + 1);
                m_buffer.read(bufferPos, outL, outR);
                m_readPos += m_playbackRate;

                m_effectPhase++;

                // Check if stopped
                if (m_effectPhase >= m_stopSamples) {
                    if (m_mode == TapeMode::Stop) {
                        m_state = State::Passthrough;
                        m_playbackRate = 1.0;
                    } else {
                        m_state = State::Stopped;
                        m_effectPhase = 0;
                    }
                }
                break;
            }

            case State::Stopped: {
                // Output silence briefly
                outL = 0.0f;
                outR = 0.0f;

                m_effectPhase++;

                if (m_effectPhase >= m_stoppedDuration) {
                    m_state = State::Starting;
                    m_effectPhase = 0;
                    m_playbackRate = 0.0;
                    m_readPos = 0.0;
                }
                break;
            }

            case State::Starting: {
                // Exponential speedup curve
                float progress = static_cast<float>(m_effectPhase) / static_cast<float>(m_startSamples);

                // Exponential attack: starts slow, speeds up
                // rate = progress^2 gives nice tape-like startup
                m_playbackRate = progress * progress;

                // Clamp to maximum
                if (m_playbackRate > 1.0) m_playbackRate = 1.0;

                // Read from buffer at increasing rate
                double bufferPos = m_buffer.getReadPosition(static_cast<size_t>(m_readPos) + 1);
                m_buffer.read(bufferPos, outL, outR);
                m_readPos += m_playbackRate;

                m_effectPhase++;

                // Check if back to full speed
                if (m_effectPhase >= m_startSamples) {
                    m_state = State::Passthrough;
                    m_playbackRate = 1.0;
                }
                break;
            }
        }

        // Apply mix
        float mixAmt = static_cast<float>(this->mix);
        output[i * 2] = inL * (1.0f - mixAmt) + outL * mixAmt;
        output[i * 2 + 1] = inR * (1.0f - mixAmt) + outR * mixAmt;
    }
}

} // namespace vivid::audio
