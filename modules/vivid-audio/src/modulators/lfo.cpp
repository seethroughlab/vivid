// Vivid Audio - LFO Modulator Implementation

#include <vivid/audio/modulators/lfo.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <cstring>
#include <algorithm>

namespace vivid::audio {

REGISTER_OPERATOR_EX(LFO, "Audio Modulation", "Low frequency oscillator for modulation", false, vivid::OutputKind::Audio);

LFO::LFO() {
    // Register Modulator params
    registerParam(perVoice);
    registerParam(retrigger);

    // Register LFO-specific params
    registerParam(rate);
    registerParam(waveform);
    registerParam(sync);
    registerParam(division);
    registerParam(startPhase);
    registerParam(bpm);

    // Initialize global state
    m_globalState.phase = 0.0f;
}

void LFO::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_initialized = true;
}

void LFO::process(Context& ctx) {
    // Clock source resolution (lazy)
    if (!m_clockSourceName.empty() && m_cachedClock == nullptr) {
        // Would need to resolve from chain - for now use internal bpm
    }
}

void LFO::cleanup() {
    releaseOutput();
    m_initialized = false;
}

float LFO::calculateFrequency() const {
    if (!static_cast<bool>(sync)) {
        return static_cast<float>(rate);
    }

    // Tempo sync mode
    float currentBpm = m_cachedClock ? static_cast<float>(m_cachedClock->bpm)
                                     : static_cast<float>(bpm);

    ClockDiv div = static_cast<ClockDiv>(static_cast<int>(division));
    return divisionToHz(div, currentBpm);
}

float LFO::generateSample(float phase) const {
    LFOWaveform wave = static_cast<LFOWaveform>(waveform);

    switch (wave) {
        case LFOWaveform::Sine:
            return std::sin(phase * TWO_PI);

        case LFOWaveform::Triangle:
            // Triangle: ramps up 0->1 in first half, down 1->-1 in second half
            if (phase < 0.25f) {
                return phase * 4.0f;
            } else if (phase < 0.75f) {
                return 1.0f - (phase - 0.25f) * 4.0f;
            } else {
                return -1.0f + (phase - 0.75f) * 4.0f;
            }

        case LFOWaveform::Square:
            return phase < 0.5f ? 1.0f : -1.0f;

        case LFOWaveform::Saw:
            // Rising saw: -1 to 1 over full cycle
            return 2.0f * phase - 1.0f;

        case LFOWaveform::SawDown:
            // Falling saw: 1 to -1 over full cycle
            return 1.0f - 2.0f * phase;

        case LFOWaveform::SampleHold:
            // S&H handled in process() - just return current held value
            return 0.0f;  // Placeholder, actual value set in process()

        default:
            return 0.0f;
    }
}

float LFO::process(ModulatorState& baseState, float sampleRate) {
    LFOState& state = static_cast<LFOState&>(baseState);

    // Calculate frequency
    float freq = calculateFrequency();
    float phaseInc = freq / sampleRate;

    // Store previous phase for trigger detection
    float prevPhase = state.phase;

    // Advance phase
    state.phase += phaseInc;

    // Check for phase wrap (trigger)
    state.triggered = false;
    if (state.phase >= 1.0f) {
        state.phase -= std::floor(state.phase);
        state.triggered = true;

        // For S&H: update held value on trigger
        if (static_cast<LFOWaveform>(waveform) == LFOWaveform::SampleHold) {
            // Generate new random value
            // Note: This uses a simple approach - in production you might want
            // per-voice random seeds for more variation
            m_randSeed = m_randSeed * 1103515245 + 12345;
            state.sampleHoldValue = (static_cast<float>(m_randSeed & 0x7FFFFFFF) /
                                     static_cast<float>(0x7FFFFFFF)) * 2.0f - 1.0f;
        }
    }

    // Generate waveform sample
    LFOWaveform wave = static_cast<LFOWaveform>(waveform);
    if (wave == LFOWaveform::SampleHold) {
        state.value = state.sampleHoldValue;
    } else {
        state.value = generateSample(state.phase);
    }

    return state.value;
}

void LFO::noteOn(ModulatorState& baseState) {
    LFOState& state = static_cast<LFOState&>(baseState);

    if (static_cast<bool>(retrigger)) {
        state.phase = static_cast<float>(startPhase);
        state.triggered = false;

        // For S&H: trigger initial value
        if (static_cast<LFOWaveform>(waveform) == LFOWaveform::SampleHold) {
            m_randSeed = m_randSeed * 1103515245 + 12345;
            state.sampleHoldValue = (static_cast<float>(m_randSeed & 0x7FFFFFFF) /
                                     static_cast<float>(0x7FFFFFFF)) * 2.0f - 1.0f;
        }
    }
}

void LFO::generateBlock(uint32_t blockFrameCount) {
    if (!m_initialized) return;

    // Resize buffer if needed
    if (m_output.frameCount != blockFrameCount) {
        m_output.resize(blockFrameCount);
    }

    // Process the global state for standalone mode
    float sampleRate = static_cast<float>(m_sampleRate);

    for (uint32_t i = 0; i < blockFrameCount; ++i) {
        float value = process(m_globalState, sampleRate);

        // Output as stereo (both channels same)
        m_output.samples[i * 2] = value;
        m_output.samples[i * 2 + 1] = value;
    }
}

} // namespace vivid::audio
