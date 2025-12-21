/**
 * @file fm_synth.cpp
 * @brief Implementation of FM synthesizer
 */

#include <vivid/audio/fm_synth.h>
#include <cstring>
#include <algorithm>

namespace vivid::audio {

FMSynth::FMSynth() {
    registerParam(ratio1);
    registerParam(ratio2);
    registerParam(ratio3);
    registerParam(ratio4);
    registerParam(level1);
    registerParam(level2);
    registerParam(level3);
    registerParam(level4);
    registerParam(feedback);
    registerParam(volume);

    // Initialize operator settings
    for (int i = 0; i < NUM_OPS; ++i) {
        m_opSettings[i].attack = 0.01f;
        m_opSettings[i].decay = 0.1f;
        m_opSettings[i].sustain = 0.7f;
        m_opSettings[i].release = 0.3f;
    }
}

void FMSynth::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();

    // Initialize all voices as inactive
    for (auto& voice : m_voices) {
        voice.active = false;
    }
}

void FMSynth::process(Context& ctx) {
    // Nothing to do - audio generation happens in generateBlock
}

void FMSynth::cleanup() {
    for (auto& voice : m_voices) {
        voice.active = false;
    }
}

void FMSynth::loadPreset(FMPreset preset) {
    switch (preset) {
        case FMPreset::EPiano:
            setAlgorithm(FMAlgorithm::Stack4);
            ratio1 = 1.0f; ratio2 = 14.0f; ratio3 = 1.0f; ratio4 = 1.0f;
            level1 = 1.0f; level2 = 0.5f; level3 = 0.8f; level4 = 1.0f;
            feedback = 0.1f;
            setEnvelope(0, 0.001f, 0.5f, 0.0f, 0.3f);
            setEnvelope(1, 0.001f, 0.1f, 0.0f, 0.1f);
            setEnvelope(2, 0.001f, 0.3f, 0.0f, 0.2f);
            setEnvelope(3, 0.001f, 1.0f, 0.0f, 0.5f);
            break;

        case FMPreset::Bass:
            setAlgorithm(FMAlgorithm::Stack4);
            ratio1 = 1.0f; ratio2 = 1.0f; ratio3 = 1.0f; ratio4 = 1.0f;
            level1 = 0.8f; level2 = 0.6f; level3 = 0.4f; level4 = 1.0f;
            feedback = 0.4f;
            setEnvelope(0, 0.001f, 0.1f, 0.3f, 0.1f);
            setEnvelope(1, 0.001f, 0.15f, 0.2f, 0.1f);
            setEnvelope(2, 0.001f, 0.2f, 0.5f, 0.15f);
            setEnvelope(3, 0.001f, 0.05f, 0.8f, 0.1f);
            break;

        case FMPreset::Bell:
            setAlgorithm(FMAlgorithm::Pairs);
            ratio1 = 1.0f; ratio2 = 3.5f; ratio3 = 1.0f; ratio4 = 7.0f;
            level1 = 0.9f; level2 = 0.7f; level3 = 0.6f; level4 = 0.4f;
            feedback = 0.0f;
            setEnvelope(0, 0.001f, 2.0f, 0.0f, 1.0f);
            setEnvelope(1, 0.001f, 0.5f, 0.0f, 0.3f);
            setEnvelope(2, 0.001f, 3.0f, 0.0f, 1.5f);
            setEnvelope(3, 0.001f, 0.3f, 0.0f, 0.2f);
            break;

        case FMPreset::Brass:
            setAlgorithm(FMAlgorithm::Stack3_1);
            ratio1 = 1.0f; ratio2 = 1.0f; ratio3 = 1.0f; ratio4 = 2.0f;
            level1 = 0.9f; level2 = 0.7f; level3 = 1.0f; level4 = 0.8f;
            feedback = 0.3f;
            setEnvelope(0, 0.05f, 0.1f, 0.8f, 0.15f);
            setEnvelope(1, 0.03f, 0.1f, 0.6f, 0.1f);
            setEnvelope(2, 0.08f, 0.15f, 0.9f, 0.2f);
            setEnvelope(3, 0.06f, 0.12f, 0.7f, 0.18f);
            break;

        case FMPreset::Organ:
            setAlgorithm(FMAlgorithm::Parallel);
            ratio1 = 0.5f; ratio2 = 1.0f; ratio3 = 2.0f; ratio4 = 4.0f;
            level1 = 0.8f; level2 = 1.0f; level3 = 0.6f; level4 = 0.3f;
            feedback = 0.1f;
            setEnvelope(0, 0.01f, 0.01f, 1.0f, 0.1f);
            setEnvelope(1, 0.01f, 0.01f, 1.0f, 0.1f);
            setEnvelope(2, 0.01f, 0.01f, 1.0f, 0.1f);
            setEnvelope(3, 0.01f, 0.01f, 1.0f, 0.1f);
            break;

        case FMPreset::Pad:
            setAlgorithm(FMAlgorithm::Branch2);
            ratio1 = 1.0f; ratio2 = 2.0f; ratio3 = 3.0f; ratio4 = 0.5f;
            level1 = 0.4f; level2 = 0.8f; level3 = 0.7f; level4 = 0.9f;
            feedback = 0.2f;
            setEnvelope(0, 0.5f, 1.0f, 0.6f, 1.5f);
            setEnvelope(1, 0.3f, 0.8f, 0.5f, 1.0f);
            setEnvelope(2, 0.4f, 0.9f, 0.55f, 1.2f);
            setEnvelope(3, 0.6f, 1.2f, 0.7f, 2.0f);
            break;

        case FMPreset::Pluck:
            setAlgorithm(FMAlgorithm::Stack4);
            ratio1 = 1.0f; ratio2 = 3.0f; ratio3 = 1.0f; ratio4 = 1.0f;
            level1 = 1.0f; level2 = 0.9f; level3 = 0.5f; level4 = 1.0f;
            feedback = 0.2f;
            setEnvelope(0, 0.001f, 0.05f, 0.0f, 0.05f);
            setEnvelope(1, 0.001f, 0.02f, 0.0f, 0.02f);
            setEnvelope(2, 0.001f, 0.1f, 0.0f, 0.08f);
            setEnvelope(3, 0.001f, 0.15f, 0.0f, 0.1f);
            break;

        case FMPreset::Lead:
            setAlgorithm(FMAlgorithm::Pairs);
            ratio1 = 1.0f; ratio2 = 1.0f; ratio3 = 2.0f; ratio4 = 2.0f;
            level1 = 0.8f; level2 = 0.7f; level3 = 0.6f; level4 = 0.5f;
            feedback = 0.35f;
            setEnvelope(0, 0.01f, 0.1f, 0.7f, 0.2f);
            setEnvelope(1, 0.01f, 0.08f, 0.5f, 0.15f);
            setEnvelope(2, 0.01f, 0.12f, 0.6f, 0.25f);
            setEnvelope(3, 0.01f, 0.1f, 0.4f, 0.2f);
            break;
    }
}

void FMSynth::setEnvelope(int op, float a, float d, float s, float r) {
    if (op < 0 || op >= NUM_OPS) return;
    m_opSettings[op].attack = std::max(0.001f, a);
    m_opSettings[op].decay = std::max(0.001f, d);
    m_opSettings[op].sustain = std::max(0.0f, std::min(1.0f, s));
    m_opSettings[op].release = std::max(0.001f, r);
}

int FMSynth::noteOn(float hz) {
    int voiceIdx = findFreeVoice();
    if (voiceIdx < 0) {
        voiceIdx = findVoiceToSteal();
    }
    if (voiceIdx < 0) return -1;

    Voice& voice = m_voices[voiceIdx];
    voice.frequency = hz;
    voice.active = true;
    voice.noteId = ++m_noteCounter;

    // Initialize operators from settings
    for (int i = 0; i < NUM_OPS; ++i) {
        voice.ops[i] = m_opSettings[i];
        voice.ops[i].phase = 0.0f;
        voice.ops[i].output = 0.0f;
        voice.ops[i].prevOutput = 0.0f;
        voice.envStage[i] = EnvelopeStage::Attack;
        voice.envValue[i] = 0.0f;
        voice.envProgress[i] = 0.0f;
        voice.releaseStartValue[i] = 0.0f;
    }

    return voiceIdx;
}

void FMSynth::noteOff(float hz) {
    int voiceIdx = findVoiceByFrequency(hz);
    if (voiceIdx < 0) return;

    Voice& voice = m_voices[voiceIdx];
    for (int i = 0; i < NUM_OPS; ++i) {
        if (voice.envStage[i] != EnvelopeStage::Idle) {
            voice.envStage[i] = EnvelopeStage::Release;
            voice.releaseStartValue[i] = voice.envValue[i];
            voice.envProgress[i] = 0.0f;
        }
    }
}

int FMSynth::noteOnMidi(int midiNote) {
    float hz = 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
    return noteOn(hz);
}

void FMSynth::noteOffMidi(int midiNote) {
    float hz = 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
    noteOff(hz);
}

void FMSynth::allNotesOff() {
    for (auto& voice : m_voices) {
        if (voice.active) {
            for (int i = 0; i < NUM_OPS; ++i) {
                voice.envStage[i] = EnvelopeStage::Release;
                voice.releaseStartValue[i] = voice.envValue[i];
                voice.envProgress[i] = 0.0f;
            }
        }
    }
}

void FMSynth::panic() {
    for (auto& voice : m_voices) {
        voice.active = false;
    }
}

int FMSynth::activeVoiceCount() const {
    int count = 0;
    for (const auto& voice : m_voices) {
        if (voice.active) ++count;
    }
    return count;
}

float FMSynth::operatorEnvelope(int op) const {
    if (op < 0 || op >= NUM_OPS) return 0.0f;
    float maxEnv = 0.0f;
    for (const auto& voice : m_voices) {
        if (voice.active && voice.envValue[op] > maxEnv) {
            maxEnv = voice.envValue[op];
        }
    }
    return maxEnv;
}

void FMSynth::generateBlock(uint32_t frameCount) {
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    float* output = m_output.samples;

    // Get parameters
    float ratios[4] = {
        static_cast<float>(ratio1),
        static_cast<float>(ratio2),
        static_cast<float>(ratio3),
        static_cast<float>(ratio4)
    };
    float levels[4] = {
        static_cast<float>(level1),
        static_cast<float>(level2),
        static_cast<float>(level3),
        static_cast<float>(level4)
    };
    float fb = static_cast<float>(feedback);
    float vol = static_cast<float>(volume);

    for (uint32_t i = 0; i < frameCount; ++i) {
        float sample = 0.0f;

        for (auto& voice : m_voices) {
            if (!voice.active) continue;

            sample += processVoice(voice, ratios, levels, fb);

            // Advance envelopes
            for (int op = 0; op < NUM_OPS; ++op) {
                advanceEnvelope(voice, op, 1);
            }

            // Check if voice is done (all operators idle)
            bool allIdle = true;
            for (int op = 0; op < NUM_OPS; ++op) {
                if (voice.envStage[op] != EnvelopeStage::Idle) {
                    allIdle = false;
                    break;
                }
            }
            if (allIdle) {
                voice.active = false;
            }
        }

        // Apply master volume
        sample *= vol;

        // Output stereo (mono source)
        output[i * 2] = sample;
        output[i * 2 + 1] = sample;
    }
}

float FMSynth::processVoice(Voice& voice, float ratios[4], float levels[4], float fb) {
    float baseFreq = voice.frequency;
    float phaseInc[4];

    for (int i = 0; i < NUM_OPS; ++i) {
        phaseInc[i] = (baseFreq * ratios[i]) / static_cast<float>(m_sampleRate);
    }

    // Get envelope values
    float env[4];
    for (int i = 0; i < NUM_OPS; ++i) {
        env[i] = voice.envValue[i];
    }

    // Calculate operator outputs based on algorithm
    float opOut[4] = {0, 0, 0, 0};
    float result = 0.0f;

    // Feedback for operator 4 (or last in chain)
    float fbSample = (voice.ops[3].output + voice.ops[3].prevOutput) * 0.5f * fb;

    switch (m_algorithm) {
        case FMAlgorithm::Stack4:
            // 1→2→3→4
            opOut[0] = std::sin(TWO_PI * voice.ops[0].phase + fbSample) * levels[0] * env[0];
            opOut[1] = std::sin(TWO_PI * voice.ops[1].phase + opOut[0] * PI) * levels[1] * env[1];
            opOut[2] = std::sin(TWO_PI * voice.ops[2].phase + opOut[1] * PI) * levels[2] * env[2];
            opOut[3] = std::sin(TWO_PI * voice.ops[3].phase + opOut[2] * PI) * levels[3] * env[3];
            result = opOut[3];
            break;

        case FMAlgorithm::Stack3_1:
            // 1→2→3, 4
            opOut[0] = std::sin(TWO_PI * voice.ops[0].phase) * levels[0] * env[0];
            opOut[1] = std::sin(TWO_PI * voice.ops[1].phase + opOut[0] * PI) * levels[1] * env[1];
            opOut[2] = std::sin(TWO_PI * voice.ops[2].phase + opOut[1] * PI) * levels[2] * env[2];
            opOut[3] = std::sin(TWO_PI * voice.ops[3].phase + fbSample) * levels[3] * env[3];
            result = (opOut[2] + opOut[3]) * 0.5f;
            break;

        case FMAlgorithm::Parallel:
            // 1,2,3,4 (additive)
            for (int i = 0; i < 4; ++i) {
                opOut[i] = std::sin(TWO_PI * voice.ops[i].phase) * levels[i] * env[i];
                result += opOut[i];
            }
            result *= 0.25f;
            break;

        case FMAlgorithm::Pairs:
            // 1→2, 3→4
            opOut[0] = std::sin(TWO_PI * voice.ops[0].phase) * levels[0] * env[0];
            opOut[1] = std::sin(TWO_PI * voice.ops[1].phase + opOut[0] * PI) * levels[1] * env[1];
            opOut[2] = std::sin(TWO_PI * voice.ops[2].phase + fbSample) * levels[2] * env[2];
            opOut[3] = std::sin(TWO_PI * voice.ops[3].phase + opOut[2] * PI) * levels[3] * env[3];
            result = (opOut[1] + opOut[3]) * 0.5f;
            break;

        case FMAlgorithm::Branch2:
            // 1→2,3 + 4
            opOut[0] = std::sin(TWO_PI * voice.ops[0].phase) * levels[0] * env[0];
            opOut[1] = std::sin(TWO_PI * voice.ops[1].phase + opOut[0] * PI) * levels[1] * env[1];
            opOut[2] = std::sin(TWO_PI * voice.ops[2].phase + opOut[0] * PI) * levels[2] * env[2];
            opOut[3] = std::sin(TWO_PI * voice.ops[3].phase + fbSample) * levels[3] * env[3];
            result = (opOut[1] + opOut[2] + opOut[3]) / 3.0f;
            break;

        case FMAlgorithm::Branch3:
            // 1→2,3,4
            opOut[0] = std::sin(TWO_PI * voice.ops[0].phase + fbSample) * levels[0] * env[0];
            opOut[1] = std::sin(TWO_PI * voice.ops[1].phase + opOut[0] * PI) * levels[1] * env[1];
            opOut[2] = std::sin(TWO_PI * voice.ops[2].phase + opOut[0] * PI) * levels[2] * env[2];
            opOut[3] = std::sin(TWO_PI * voice.ops[3].phase + opOut[0] * PI) * levels[3] * env[3];
            result = (opOut[1] + opOut[2] + opOut[3]) / 3.0f;
            break;

        case FMAlgorithm::Y:
            // 1→2, 1→3, 2+3→4
            opOut[0] = std::sin(TWO_PI * voice.ops[0].phase) * levels[0] * env[0];
            opOut[1] = std::sin(TWO_PI * voice.ops[1].phase + opOut[0] * PI) * levels[1] * env[1];
            opOut[2] = std::sin(TWO_PI * voice.ops[2].phase + opOut[0] * PI) * levels[2] * env[2];
            opOut[3] = std::sin(TWO_PI * voice.ops[3].phase + (opOut[1] + opOut[2]) * 0.5f * PI + fbSample) * levels[3] * env[3];
            result = opOut[3];
            break;

        case FMAlgorithm::Diamond:
            // 1→2, 1→3, 2→4, 3→4
            opOut[0] = std::sin(TWO_PI * voice.ops[0].phase) * levels[0] * env[0];
            opOut[1] = std::sin(TWO_PI * voice.ops[1].phase + opOut[0] * PI) * levels[1] * env[1];
            opOut[2] = std::sin(TWO_PI * voice.ops[2].phase + opOut[0] * PI) * levels[2] * env[2];
            opOut[3] = std::sin(TWO_PI * voice.ops[3].phase + (opOut[1] + opOut[2]) * 0.5f * PI + fbSample) * levels[3] * env[3];
            result = opOut[3];
            break;
    }

    // Store outputs for feedback
    for (int i = 0; i < NUM_OPS; ++i) {
        voice.ops[i].prevOutput = voice.ops[i].output;
        voice.ops[i].output = opOut[i];

        // Advance phase
        voice.ops[i].phase += phaseInc[i];
        if (voice.ops[i].phase >= 1.0f) {
            voice.ops[i].phase -= 1.0f;
        }
    }

    return result;
}

void FMSynth::advanceEnvelope(Voice& voice, int op, uint32_t samples) {
    if (op < 0 || op >= NUM_OPS) return;

    float& value = voice.envValue[op];
    float& progress = voice.envProgress[op];
    EnvelopeStage& stage = voice.envStage[op];
    const Operator& settings = voice.ops[op];

    float timeStep = static_cast<float>(samples) / static_cast<float>(m_sampleRate);

    switch (stage) {
        case EnvelopeStage::Idle:
            value = 0.0f;
            break;

        case EnvelopeStage::Attack:
            progress += timeStep / settings.attack;
            if (progress >= 1.0f) {
                value = 1.0f;
                progress = 0.0f;
                stage = EnvelopeStage::Decay;
            } else {
                value = progress;
            }
            break;

        case EnvelopeStage::Decay:
            progress += timeStep / settings.decay;
            if (progress >= 1.0f) {
                value = settings.sustain;
                progress = 0.0f;
                stage = EnvelopeStage::Sustain;
            } else {
                value = 1.0f - progress * (1.0f - settings.sustain);
            }
            break;

        case EnvelopeStage::Sustain:
            value = settings.sustain;
            break;

        case EnvelopeStage::Release:
            progress += timeStep / settings.release;
            if (progress >= 1.0f) {
                value = 0.0f;
                stage = EnvelopeStage::Idle;
            } else {
                value = voice.releaseStartValue[op] * (1.0f - progress);
            }
            break;
    }
}

int FMSynth::findFreeVoice() const {
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (!m_voices[i].active) {
            return i;
        }
    }
    return -1;
}

int FMSynth::findVoiceToSteal() const {
    // Steal oldest voice
    uint64_t oldestId = UINT64_MAX;
    int oldestIdx = -1;

    for (int i = 0; i < MAX_VOICES; ++i) {
        if (m_voices[i].noteId < oldestId) {
            oldestId = m_voices[i].noteId;
            oldestIdx = i;
        }
    }

    return oldestIdx;
}

int FMSynth::findVoiceByFrequency(float hz) const {
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (m_voices[i].active &&
            std::abs(m_voices[i].frequency - hz) < FREQ_TOLERANCE) {
            return i;
        }
    }
    return -1;
}

} // namespace vivid::audio
