#include <vivid/audio/glitch/stretch.h>
#include <vivid/operator_registry.h>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

REGISTER_OPERATOR_EX(Stretch, "Stretch", "Granular time-stretch without pitch change", false, vivid::OutputKind::Audio);

void Stretch::initEffect(Context& ctx) {
    m_stretching = false;
    m_triggerPhase = 0.0;
    m_buffer.clear();

    // Clear all grains
    for (int i = 0; i < MAX_GRAINS; ++i) {
        m_grains[i].active = false;
    }
    m_nextGrain = 0;
}

void Stretch::startGrain(uint32_t sampleRate) {
    // Find inactive grain slot
    int slot = -1;
    for (int i = 0; i < MAX_GRAINS; ++i) {
        int idx = (m_nextGrain + i) % MAX_GRAINS;
        if (!m_grains[idx].active) {
            slot = idx;
            break;
        }
    }

    if (slot < 0) return;  // All grains busy

    Grain& g = m_grains[slot];
    g.active = true;
    g.grainPhase = 0.0;

    // Calculate grain size in samples
    float grainMs = static_cast<float>(grainSize);
    g.grainSamples = static_cast<uint32_t>(grainMs * 0.001f * sampleRate);

    // Calculate source position with optional randomization
    float randomOffset = (random() - 0.5f) * 2.0f * static_cast<float>(grainRandom);
    float sourcePos = static_cast<float>(m_sourcePhase) + randomOffset;
    sourcePos = std::max(0.0f, std::min(1.0f, sourcePos));

    // Convert to buffer position
    g.sourcePos = static_cast<double>(m_stretchStart) + sourcePos * m_stretchLength;

    m_nextGrain = (slot + 1) % MAX_GRAINS;
}

void Stretch::processEffect(const float* input, float* output, uint32_t frames) {
    float currentBpm = static_cast<float>(bpm);
    uint32_t sampleRate = m_buffer.sampleRate();

    float triggerSamples = static_cast<float>(divisionToSamples(m_triggerDiv, currentBpm, sampleRate));
    float phaseInc = 1.0f / triggerSamples;

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Always record to buffer
        m_buffer.write(inL, inR);

        float outL = inL;
        float outR = inR;

        if (!m_stretching) {
            // Check for trigger
            m_triggerPhase += phaseInc;
            if (m_triggerPhase >= 1.0) {
                m_triggerPhase -= 1.0;

                if (random() < static_cast<float>(chance)) {
                    // Start stretching
                    m_stretching = true;

                    // Capture source audio
                    uint32_t sourceSamples = divisionToSamples(m_stretchDiv, currentBpm, sampleRate);
                    m_stretchLength = sourceSamples;
                    m_stretchStart = m_buffer.getReadPosition(sourceSamples);

                    // Calculate stretched output length
                    float factor = static_cast<float>(stretchFactor);
                    m_stretchSamples = static_cast<uint32_t>(sourceSamples * factor);
                    m_stretchPos = 0;
                    m_sourcePhase = 0.0;

                    // Calculate grain interval based on overlap
                    float grainMs = static_cast<float>(grainSize);
                    uint32_t grainSamples = static_cast<uint32_t>(grainMs * 0.001f * sampleRate);
                    float overlapAmt = static_cast<float>(overlap);
                    m_grainInterval = static_cast<uint32_t>(grainSamples * (1.0f - overlapAmt));
                    if (m_grainInterval < 1) m_grainInterval = 1;
                    m_grainCounter = 0;

                    // Clear existing grains
                    for (int j = 0; j < MAX_GRAINS; ++j) {
                        m_grains[j].active = false;
                    }

                    // Start first grain immediately
                    startGrain(sampleRate);
                }
            }
        }

        if (m_stretching) {
            // Check if we should start a new grain
            m_grainCounter++;
            if (m_grainCounter >= m_grainInterval) {
                m_grainCounter = 0;
                startGrain(sampleRate);
            }

            // Mix all active grains
            float grainL = 0.0f;
            float grainR = 0.0f;
            int activeCount = 0;

            for (int j = 0; j < MAX_GRAINS; ++j) {
                Grain& g = m_grains[j];
                if (!g.active) continue;

                // Get window amplitude
                float window = hannWindow(static_cast<float>(g.grainPhase));

                // Read from buffer at grain's source position
                float gL, gR;
                m_buffer.read(g.sourcePos, gL, gR);

                grainL += gL * window;
                grainR += gR * window;
                activeCount++;

                // Advance grain
                double phaseInc = 1.0 / static_cast<double>(g.grainSamples);
                g.grainPhase += phaseInc;
                g.sourcePos += 1.0;  // Read at normal speed (pitch preserved)

                // Deactivate finished grains
                if (g.grainPhase >= 1.0) {
                    g.active = false;
                }
            }

            // Normalize by expected overlap count (roughly 1/overlap grains active)
            if (activeCount > 0) {
                float normFactor = static_cast<float>(overlap) * 2.0f;
                outL = grainL * normFactor;
                outR = grainR * normFactor;
            }

            // Advance source phase (slower than real-time for stretch > 1)
            float factor = static_cast<float>(stretchFactor);
            m_sourcePhase += 1.0 / (m_stretchLength * factor);

            m_stretchPos++;
            if (m_stretchPos >= m_stretchSamples) {
                m_stretching = false;
            }
        }

        // Apply mix
        float mixAmt = static_cast<float>(this->mix);
        output[i * 2] = inL * (1.0f - mixAmt) + outL * mixAmt;
        output[i * 2 + 1] = inR * (1.0f - mixAmt) + outR * mixAmt;
    }
}

} // namespace vivid::audio
