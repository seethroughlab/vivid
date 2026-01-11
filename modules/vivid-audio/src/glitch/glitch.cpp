#include <vivid/audio/glitch/glitch.h>
#include <vivid/operator_registry.h>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

REGISTER_OPERATOR_EX(Glitch, "Glitch", "Multi-effect glitch processor with per-effect probability", false, vivid::OutputKind::Audio);

static constexpr double PI = 3.14159265358979323846;
static constexpr double TWO_PI = 2.0 * PI;

void Glitch::initEffect(Context& ctx) {
    m_active = ActiveEffect::None;
    m_triggerPhase = 0.0;
    m_buffer.clear();

    // Clear allpass state
    for (int i = 0; i < 4; ++i) {
        m_allpassL[i] = 0.0f;
        m_allpassR[i] = 0.0f;
    }
}

Glitch::ActiveEffect Glitch::selectEffect() {
    // Build probability distribution
    float chances[6] = {
        static_cast<float>(repeatChance),
        static_cast<float>(reverseChance),
        static_cast<float>(stutterChance),
        static_cast<float>(scratchChance),
        static_cast<float>(tapeChance),
        static_cast<float>(shiftChance)
    };

    float total = 0.0f;
    for (int i = 0; i < 6; ++i) total += chances[i];

    if (total < 0.001f) return ActiveEffect::None;

    // Random selection weighted by probability
    float r = random() * total;
    float cumulative = 0.0f;

    for (int i = 0; i < 6; ++i) {
        cumulative += chances[i];
        if (r < cumulative) {
            return static_cast<ActiveEffect>(i + 1);  // +1 because None=0
        }
    }

    return ActiveEffect::None;
}

void Glitch::startEffect(ActiveEffect effect, uint32_t sampleRate) {
    float currentBpm = static_cast<float>(bpm);

    switch (effect) {
        case ActiveEffect::Repeat: {
            uint32_t sliceSamples = divisionToSamples(ClockDiv::Sixteenth, currentBpm, sampleRate);
            m_repeatLength = sliceSamples;
            m_repeatStart = m_buffer.getReadPosition(m_repeatLength);
            m_repeatPhase = 0.0;
            m_repeatCount = 0;
            m_repeatTotal = 3 + static_cast<int>(random() * 4);  // 3-6 repeats
            m_repeatGain = 1.0f;
            break;
        }

        case ActiveEffect::Reverse: {
            uint32_t reverseSamples = divisionToSamples(ClockDiv::Quarter, currentBpm, sampleRate);
            m_reverseLength = reverseSamples;
            m_reverseStart = m_buffer.getReadPosition(m_reverseLength);
            m_reversePhase = 0.0;
            break;
        }

        case ActiveEffect::Stutter: {
            uint32_t stutterSamples = divisionToSamples(ClockDiv::ThirtySecond, currentBpm, sampleRate);
            m_stutterLength = stutterSamples;
            m_stutterStart = m_buffer.getReadPosition(m_stutterLength);
            m_stutterPhase = 0.0;
            m_stutterCount = 0;
            m_stutterTotal = 6 + static_cast<int>(random() * 6);  // 6-12 stutters
            break;
        }

        case ActiveEffect::Scratch: {
            float scratchBeats = 0.25f + random() * 0.5f;  // 0.25-0.75 beats
            float beatsPerSec = currentBpm / 60.0f;
            m_scratchLength = static_cast<uint32_t>(scratchBeats / beatsPerSec * sampleRate);
            m_scratchStart = m_buffer.getReadPosition(m_scratchLength * 2);
            m_scratchPhase = 0.0;
            m_scratchReadPos = 0.0;
            m_scratchSpeed = 0.8f + random() * 0.8f;  // 0.8-1.6x
            m_scratchDir = 1;
            break;
        }

        case ActiveEffect::TapeStop: {
            m_tapeStopLen = static_cast<uint32_t>(0.3f * sampleRate);  // 300ms stop
            m_tapeStartLen = static_cast<uint32_t>(0.15f * sampleRate);  // 150ms start
            m_tapeRate = 1.0;
            m_tapeReadPos = 0.0;
            m_tapePhase = 0;
            m_tapeStopping = true;
            break;
        }

        case ActiveEffect::FreqShift: {
            m_shiftDuration = divisionToSamples(ClockDiv::Half, currentBpm, sampleRate);
            m_shiftPhase = 0;
            m_shiftOscPhase = 0.0;
            m_shiftAmount = 20.0f + random() * 60.0f;  // 20-80 Hz shift
            if (random() > 0.5f) m_shiftAmount = -m_shiftAmount;  // Random direction
            break;
        }

        default:
            break;
    }
}

void Glitch::processEffect(const float* input, float* output, uint32_t frames) {
    float currentBpm = static_cast<float>(bpm);
    uint32_t sampleRate = m_buffer.sampleRate();

    float triggerSamples = static_cast<float>(divisionToSamples(m_triggerDiv, currentBpm, sampleRate));
    float phaseInc = 1.0f / triggerSamples;

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Always record
        m_buffer.write(inL, inR);

        float outL = inL;
        float outR = inR;

        // Check for trigger when no effect is active
        if (m_active == ActiveEffect::None) {
            m_triggerPhase += phaseInc;
            if (m_triggerPhase >= 1.0) {
                m_triggerPhase -= 1.0;

                // Select and start an effect
                ActiveEffect selected = selectEffect();
                if (selected != ActiveEffect::None) {
                    m_active = selected;
                    startEffect(selected, sampleRate);
                }
            }
        }

        // Process active effect
        switch (m_active) {
            case ActiveEffect::Repeat: {
                double readPos = static_cast<double>(m_repeatStart) + m_repeatPhase;
                m_buffer.read(readPos, outL, outR);
                outL *= m_repeatGain;
                outR *= m_repeatGain;

                m_repeatPhase += 1.0;
                if (m_repeatPhase >= m_repeatLength) {
                    m_repeatPhase = 0.0;
                    m_repeatCount++;
                    m_repeatGain *= 0.85f;  // Decay
                    if (m_repeatCount >= m_repeatTotal) {
                        m_active = ActiveEffect::None;
                    }
                }
                break;
            }

            case ActiveEffect::Reverse: {
                double readPos = static_cast<double>(m_reverseStart) +
                                static_cast<double>(m_reverseLength - 1) - m_reversePhase;
                m_buffer.read(readPos, outL, outR);

                m_reversePhase += 1.0;
                if (m_reversePhase >= m_reverseLength) {
                    m_active = ActiveEffect::None;
                }
                break;
            }

            case ActiveEffect::Stutter: {
                double readPos = static_cast<double>(m_stutterStart) + m_stutterPhase;
                m_buffer.read(readPos, outL, outR);

                // Build envelope
                float env = static_cast<float>(m_stutterCount) / static_cast<float>(m_stutterTotal);
                outL *= env;
                outR *= env;

                m_stutterPhase += 1.0;
                if (m_stutterPhase >= m_stutterLength) {
                    m_stutterPhase = 0.0;
                    m_stutterCount++;
                    if (m_stutterCount >= m_stutterTotal) {
                        m_active = ActiveEffect::None;
                    }
                }
                break;
            }

            case ActiveEffect::Scratch: {
                double readPos = static_cast<double>(m_scratchStart) + m_scratchReadPos;
                m_buffer.read(readPos, outL, outR);

                m_scratchReadPos += m_scratchSpeed * m_scratchDir;

                // Back-forth motion
                float progress = static_cast<float>(m_scratchPhase) / static_cast<float>(m_scratchLength);
                float cyclePos = std::fmod(progress * 2.0f, 1.0f);
                m_scratchDir = (cyclePos < 0.5f) ? 1 : -1;

                if (m_scratchReadPos < 0) m_scratchReadPos = 0;
                if (m_scratchReadPos >= m_scratchLength) m_scratchReadPos = m_scratchLength - 1;

                m_scratchPhase++;
                if (m_scratchPhase >= m_scratchLength) {
                    m_active = ActiveEffect::None;
                }
                break;
            }

            case ActiveEffect::TapeStop: {
                double bufferPos = m_buffer.getReadPosition(static_cast<size_t>(m_tapeReadPos) + 1);
                m_buffer.read(bufferPos, outL, outR);

                if (m_tapeStopping) {
                    float progress = static_cast<float>(m_tapePhase) / static_cast<float>(m_tapeStopLen);
                    float curve = 1.0f - progress;
                    m_tapeRate = curve * curve * curve;
                    if (m_tapeRate < 0.01) m_tapeRate = 0.0;

                    m_tapeReadPos += m_tapeRate;
                    m_tapePhase++;

                    if (m_tapePhase >= m_tapeStopLen) {
                        m_tapeStopping = false;
                        m_tapePhase = 0;
                        m_tapeRate = 0.0;
                    }
                } else {
                    // Starting back up
                    float progress = static_cast<float>(m_tapePhase) / static_cast<float>(m_tapeStartLen);
                    m_tapeRate = progress * progress;
                    if (m_tapeRate > 1.0) m_tapeRate = 1.0;

                    m_tapeReadPos += m_tapeRate;
                    m_tapePhase++;

                    if (m_tapePhase >= m_tapeStartLen) {
                        m_active = ActiveEffect::None;
                    }
                }
                break;
            }

            case ActiveEffect::FreqShift: {
                // Simple frequency shift using phase modulation approximation
                double oscPhaseInc = std::abs(m_shiftAmount) / sampleRate;
                float oscI = static_cast<float>(std::cos(m_shiftOscPhase * TWO_PI));
                float oscQ = static_cast<float>(std::sin(m_shiftOscPhase * TWO_PI));

                // Approximate quadrature using allpass
                float qL = m_allpassL[0];
                m_allpassL[0] = inL * 0.6f + m_allpassL[1] * 0.4f;
                m_allpassL[1] = m_allpassL[0] * 0.6f + m_allpassL[2] * 0.4f;
                m_allpassL[2] = m_allpassL[1] * 0.6f + m_allpassL[3] * 0.4f;
                m_allpassL[3] = m_allpassL[2];

                float qR = m_allpassR[0];
                m_allpassR[0] = inR * 0.6f + m_allpassR[1] * 0.4f;
                m_allpassR[1] = m_allpassR[0] * 0.6f + m_allpassR[2] * 0.4f;
                m_allpassR[2] = m_allpassR[1] * 0.6f + m_allpassR[3] * 0.4f;
                m_allpassR[3] = m_allpassR[2];

                if (m_shiftAmount >= 0) {
                    outL = inL * oscI - qL * oscQ;
                    outR = inR * oscI - qR * oscQ;
                } else {
                    outL = inL * oscI + qL * oscQ;
                    outR = inR * oscI + qR * oscQ;
                }

                m_shiftOscPhase += oscPhaseInc;
                if (m_shiftOscPhase >= 1.0) m_shiftOscPhase -= 1.0;

                m_shiftPhase++;
                if (m_shiftPhase >= m_shiftDuration) {
                    m_active = ActiveEffect::None;
                }
                break;
            }

            default:
                break;
        }

        // Apply mix
        float mixAmt = static_cast<float>(this->mix);
        output[i * 2] = inL * (1.0f - mixAmt) + outL * mixAmt;
        output[i * 2 + 1] = inR * (1.0f - mixAmt) + outR * mixAmt;
    }
}

} // namespace vivid::audio
