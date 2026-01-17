// Vivid Audio - ADSR Modulator Implementation

#include <vivid/audio/modulators/adsr.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace vivid::audio {

REGISTER_OPERATOR_EX(ADSRMod, "Audio Modulation", "ADSR envelope for modulation", false, vivid::OutputKind::Audio);

ADSRMod::ADSRMod() {
    // Envelopes are always per-voice by default
    perVoice = true;
    retrigger = true;

    // Register Modulator params
    registerParam(perVoice);
    registerParam(retrigger);

    // Register ADSR-specific params
    registerParam(attack);
    registerParam(decay);
    registerParam(sustain);
    registerParam(release);
}

void ADSRMod::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_initialized = true;
}

void ADSRMod::process(Context& ctx) {
    // Nothing to do here - audio generation in generateBlock()
}

void ADSRMod::cleanup() {
    releaseOutput();
    m_initialized = false;
}

void ADSRMod::trigger() {
    m_globalState.stage = EnvelopeStage::Attack;
    m_globalState.value = 0.0f;
    m_globalState.progress = 0.0f;
}

void ADSRMod::releaseNote() {
    if (m_globalState.stage != EnvelopeStage::Idle &&
        m_globalState.stage != EnvelopeStage::Release) {
        m_globalState.stage = EnvelopeStage::Release;
        m_globalState.progress = 0.0f;
        m_globalState.releaseStartValue = m_globalState.value;
    }
}

float ADSRMod::computeEnvelopeValue(const ADSRState& state) const {
    float sustainLevel = static_cast<float>(sustain);

    switch (state.stage) {
        case EnvelopeStage::Attack:
            // Linear attack from 0 to 1
            return state.progress;

        case EnvelopeStage::Decay:
            // Exponential-ish decay from 1 to sustain
            return 1.0f - state.progress * (1.0f - sustainLevel);

        case EnvelopeStage::Sustain:
            return sustainLevel;

        case EnvelopeStage::Release:
            // Exponential-ish release from releaseStartValue to 0
            return state.releaseStartValue * (1.0f - state.progress);

        case EnvelopeStage::Idle:
        default:
            return 0.0f;
    }
}

void ADSRMod::advanceEnvelope(ADSRState& state, float sampleRate) {
    if (state.stage == EnvelopeStage::Idle) return;

    float timePerSample = 1.0f / sampleRate;

    switch (state.stage) {
        case EnvelopeStage::Attack: {
            float attackTime = std::max(0.001f, static_cast<float>(attack));
            state.progress += timePerSample / attackTime;
            if (state.progress >= 1.0f) {
                state.progress = 0.0f;
                state.stage = EnvelopeStage::Decay;
            }
            break;
        }

        case EnvelopeStage::Decay: {
            float decayTime = std::max(0.001f, static_cast<float>(decay));
            state.progress += timePerSample / decayTime;
            if (state.progress >= 1.0f) {
                state.progress = 0.0f;
                state.stage = EnvelopeStage::Sustain;
            }
            break;
        }

        case EnvelopeStage::Sustain:
            // Stay until noteOff
            break;

        case EnvelopeStage::Release: {
            float releaseTime = std::max(0.001f, static_cast<float>(release));
            state.progress += timePerSample / releaseTime;
            if (state.progress >= 1.0f) {
                state.stage = EnvelopeStage::Idle;
                state.value = 0.0f;
            }
            break;
        }

        default:
            break;
    }

    state.value = computeEnvelopeValue(state);
}

float ADSRMod::process(ModulatorState& baseState, float sampleRate) {
    ADSRState& state = static_cast<ADSRState&>(baseState);
    advanceEnvelope(state, sampleRate);
    return state.value;
}

void ADSRMod::noteOn(ModulatorState& baseState) {
    ADSRState& state = static_cast<ADSRState&>(baseState);

    if (static_cast<bool>(retrigger)) {
        state.stage = EnvelopeStage::Attack;
        state.progress = 0.0f;
        state.value = 0.0f;
    } else if (state.stage == EnvelopeStage::Idle) {
        // Only start if idle
        state.stage = EnvelopeStage::Attack;
        state.progress = 0.0f;
    }
}

void ADSRMod::noteOff(ModulatorState& baseState) {
    ADSRState& state = static_cast<ADSRState&>(baseState);

    if (state.stage != EnvelopeStage::Idle &&
        state.stage != EnvelopeStage::Release) {
        state.stage = EnvelopeStage::Release;
        state.progress = 0.0f;
        state.releaseStartValue = state.value;
    }
}

void ADSRMod::handleEvent(const AudioEvent& event) {
    switch (event.type) {
        case AudioEventType::Trigger:
            trigger();
            break;
        case AudioEventType::NoteOff:
            releaseNote();
            break;
        default:
            break;
    }
}

void ADSRMod::generateBlock(uint32_t blockFrameCount) {
    if (!m_initialized) return;

    // Resize buffer if needed
    if (m_output.frameCount != blockFrameCount) {
        m_output.resize(blockFrameCount);
    }

    float sampleRate = static_cast<float>(m_sampleRate);

    for (uint32_t i = 0; i < blockFrameCount; ++i) {
        float value = process(m_globalState, sampleRate);

        // Output as stereo (both channels same)
        m_output.samples[i * 2] = value;
        m_output.samples[i * 2 + 1] = value;
    }
}

} // namespace vivid::audio
