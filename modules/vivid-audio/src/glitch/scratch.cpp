#include <vivid/audio/glitch/scratch.h>
#include <vivid/operator_registry.h>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

REGISTER_OPERATOR_EX(Scratch, "Glitch", "DJ-style scratch with varispeed playback", false, vivid::OutputKind::Audio);

void Scratch::initEffect(Context& ctx) {
    m_state = State::Passthrough;
    m_triggerPhase = 0.0;
    m_scratchPhase = 0.0;
    m_readPos = 0.0;
    m_currentSpeed = 1.0f;
    m_direction = 1;
    m_buffer.clear();
}

float Scratch::randomSpeed() {
    // Quantized speed values for musical scratching
    static const float speeds[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    static const int numSpeeds = 6;

    float baseSpeed = static_cast<float>(speed);
    float randomAmt = static_cast<float>(speedRandom);

    if (randomAmt < 0.01f) {
        return baseSpeed;
    }

    // Pick a random quantized speed and blend with base
    int idx = static_cast<int>(random() * numSpeeds) % numSpeeds;
    float quantizedSpeed = speeds[idx];

    return baseSpeed * (1.0f - randomAmt) + quantizedSpeed * randomAmt;
}

void Scratch::processEffect(const float* input, float* output, uint32_t frames) {
    float currentBpm = static_cast<float>(bpm);
    uint32_t sampleRate = m_buffer.sampleRate();

    // Calculate trigger interval
    float triggerSamples = static_cast<float>(divisionToSamples(m_triggerDiv, currentBpm, sampleRate));
    float phaseIncPerSample = 1.0f / triggerSamples;

    // Calculate scratch duration in samples
    float beatsPerSecond = currentBpm / 60.0f;
    float scratchSeconds = static_cast<float>(scratchBeats) / beatsPerSecond;
    uint32_t scratchSamples = static_cast<uint32_t>(scratchSeconds * sampleRate);

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Always write to circular buffer
        m_buffer.write(inL, inR);

        float outL = inL;
        float outR = inR;

        if (m_state == State::Passthrough) {
            // Check for trigger
            m_triggerPhase += phaseIncPerSample;

            if (m_triggerPhase >= 1.0) {
                m_triggerPhase -= 1.0;

                if (random() < static_cast<float>(chance)) {
                    // Start scratching
                    m_state = State::Scratching;
                    m_scratchLength = scratchSamples;
                    // Capture a slice slightly longer than scratch duration for back-forth
                    uint32_t captureLen = scratchSamples * 2;
                    m_scratchStart = m_buffer.getReadPosition(captureLen);
                    m_scratchPhase = 0.0;
                    m_readPos = 0.0;
                    m_currentSpeed = randomSpeed();

                    // Set initial direction based on motion type
                    switch (m_motion) {
                        case ScratchMotion::Forward:
                            m_direction = 1;
                            break;
                        case ScratchMotion::Backward:
                            m_direction = -1;
                            break;
                        case ScratchMotion::BackForth:
                            m_direction = 1;
                            break;
                        case ScratchMotion::Random:
                            m_direction = (random() > 0.5f) ? 1 : -1;
                            break;
                    }
                }
            }

        } else {
            // Scratching state
            // Read from buffer at current position
            double bufferPos = static_cast<double>(m_scratchStart) + m_readPos;
            m_buffer.read(bufferPos, outL, outR);

            // Advance read position based on speed and direction
            m_readPos += m_currentSpeed * m_direction;

            // Handle motion types
            float progress = static_cast<float>(m_scratchPhase) / static_cast<float>(m_scratchLength);

            switch (m_motion) {
                case ScratchMotion::Forward:
                    // Just play forward, wrap if needed
                    if (m_readPos >= scratchSamples) {
                        m_readPos = 0.0;
                    }
                    break;

                case ScratchMotion::Backward:
                    // Just play backward
                    if (m_readPos < 0) {
                        m_readPos = scratchSamples - 1;
                    }
                    break;

                case ScratchMotion::BackForth: {
                    // Triangle wave motion - forward then backward
                    // Use progress to determine direction
                    int cycles = 2;  // Number of back-forth cycles
                    float cycleProgress = std::fmod(progress * cycles, 1.0f);

                    if (cycleProgress < 0.5f) {
                        m_direction = 1;  // Forward
                    } else {
                        m_direction = -1;  // Backward
                    }

                    // Clamp read position
                    if (m_readPos < 0) m_readPos = 0;
                    if (m_readPos >= scratchSamples) m_readPos = scratchSamples - 1;
                    break;
                }

                case ScratchMotion::Random:
                    // Random direction changes
                    if (random() < 0.02f) {  // 2% chance per sample to flip
                        m_direction = -m_direction;
                        m_currentSpeed = randomSpeed();
                    }
                    // Clamp and bounce
                    if (m_readPos < 0) {
                        m_readPos = 0;
                        m_direction = 1;
                    }
                    if (m_readPos >= scratchSamples) {
                        m_readPos = scratchSamples - 1;
                        m_direction = -1;
                    }
                    break;
            }

            // Advance scratch phase
            m_scratchPhase += 1.0;

            // Check if scratch is complete
            if (m_scratchPhase >= m_scratchLength) {
                m_state = State::Passthrough;
            }
        }

        // Apply mix
        float mixAmt = static_cast<float>(this->mix);
        output[i * 2] = inL * (1.0f - mixAmt) + outL * mixAmt;
        output[i * 2 + 1] = inR * (1.0f - mixAmt) + outR * mixAmt;
    }
}

} // namespace vivid::audio
