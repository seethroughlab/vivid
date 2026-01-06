// Vivid Audio - PolySynth Implementation

#include <vivid/audio/poly_synth.h>
#include <vivid/audio/notes.h>
#include <vivid/context.h>

#include <cstring>
#include <limits>

namespace vivid::audio {

PolySynth::PolySynth() {
    registerParam(maxVoices);
    registerParam(volume);
    registerParam(detune);
    registerParam(unisonDetune);
    registerParam(pulseWidth);
    registerParam(attack);
    registerParam(decay);
    registerParam(sustain);
    registerParam(release);

    // Pre-allocate maximum voices
    m_voices.resize(16);
}

void PolySynth::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_initialized = true;
}

void PolySynth::process(Context& ctx) {
    if (!m_initialized) return;
    // Audio generation happens in generateBlock()
}

void PolySynth::cleanup() {
    m_voices.clear();
    releaseOutput();
    m_initialized = false;
}

int PolySynth::noteOn(float hz) {
    // Find a voice to use
    int voiceIdx = findFreeVoice();
    if (voiceIdx < 0) {
        voiceIdx = findVoiceToSteal();
    }
    if (voiceIdx < 0) {
        return -1;  // No voice available
    }

    Voice& voice = m_voices[voiceIdx];
    voice.frequency = hz;
    voice.phaseL = 0.0f;
    voice.phaseR = 0.0f;
    voice.envStage = EnvelopeStage::Attack;
    voice.envValue = 0.0f;
    voice.envProgress = 0.0f;
    voice.noteId = ++m_noteCounter;

    return voiceIdx;
}

void PolySynth::noteOff(float hz) {
    int voiceIdx = findVoiceByFrequency(hz);
    if (voiceIdx >= 0) {
        Voice& voice = m_voices[voiceIdx];
        if (voice.envStage != EnvelopeStage::Idle &&
            voice.envStage != EnvelopeStage::Release) {
            voice.envStage = EnvelopeStage::Release;
            voice.envProgress = 0.0f;
            voice.releaseStartValue = voice.envValue;
        }
    }
}

int PolySynth::noteOnMidi(int midiNote) {
    return noteOn(midiToFreq(midiNote));
}

void PolySynth::noteOffMidi(int midiNote) {
    noteOff(midiToFreq(midiNote));
}

void PolySynth::allNotesOff() {
    for (auto& voice : m_voices) {
        if (voice.envStage != EnvelopeStage::Idle &&
            voice.envStage != EnvelopeStage::Release) {
            voice.envStage = EnvelopeStage::Release;
            voice.envProgress = 0.0f;
            voice.releaseStartValue = voice.envValue;
        }
    }
}

void PolySynth::panic() {
    for (auto& voice : m_voices) {
        voice.envStage = EnvelopeStage::Idle;
        voice.envValue = 0.0f;
        voice.frequency = 0.0f;
    }
}

int PolySynth::activeVoiceCount() const {
    int count = 0;
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive()) {
            ++count;
        }
    }
    return count;
}

float PolySynth::maxEnvelopeValue() const {
    float maxEnv = 0.0f;
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive() && m_voices[i].envValue > maxEnv) {
            maxEnv = m_voices[i].envValue;
        }
    }
    return maxEnv;
}

int PolySynth::findFreeVoice() const {
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (!m_voices[i].isActive()) {
            return i;
        }
    }
    return -1;
}

int PolySynth::findVoiceToSteal() const {
    if (m_stealMode == VoiceStealMode::None) {
        return -1;
    }

    int max = static_cast<int>(maxVoices);
    int stealIdx = -1;

    if (m_stealMode == VoiceStealMode::Oldest) {
        uint64_t oldestId = std::numeric_limits<uint64_t>::max();
        for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
            if (m_voices[i].isActive() && m_voices[i].noteId < oldestId) {
                oldestId = m_voices[i].noteId;
                stealIdx = i;
            }
        }
    } else if (m_stealMode == VoiceStealMode::Quietest) {
        float quietest = std::numeric_limits<float>::max();
        for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
            if (m_voices[i].isActive() && m_voices[i].envValue < quietest) {
                quietest = m_voices[i].envValue;
                stealIdx = i;
            }
        }
    }

    return stealIdx;
}

int PolySynth::findVoiceByFrequency(float hz) const {
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive() &&
            !m_voices[i].isReleasing() &&
            std::abs(m_voices[i].frequency - hz) < FREQ_TOLERANCE) {
            return i;
        }
    }
    return -1;
}

float PolySynth::generateSample(float phase) const {
    switch (m_waveform) {
        case Waveform::Sine:
            return std::sin(phase);

        case Waveform::Triangle: {
            float t = phase / TWO_PI;
            return 4.0f * std::abs(t - std::floor(t + 0.5f)) - 1.0f;
        }

        case Waveform::Square:
            return phase < PI ? 1.0f : -1.0f;

        case Waveform::Saw: {
            float t = phase / TWO_PI;
            return 2.0f * (t - std::floor(t + 0.5f));
        }

        case Waveform::Pulse: {
            float pw = static_cast<float>(pulseWidth);
            return phase < (TWO_PI * pw) ? 1.0f : -1.0f;
        }

        default:
            return 0.0f;
    }
}

float PolySynth::centsToRatio(float cents) const {
    return std::pow(2.0f, cents / 1200.0f);
}

float PolySynth::computeEnvelope(Voice& voice) {
    switch (voice.envStage) {
        case EnvelopeStage::Attack:
            return voice.envProgress;

        case EnvelopeStage::Decay: {
            float s = static_cast<float>(sustain);
            return 1.0f - voice.envProgress * (1.0f - s);
        }

        case EnvelopeStage::Sustain:
            return static_cast<float>(sustain);

        case EnvelopeStage::Release:
            return voice.releaseStartValue * (1.0f - voice.envProgress);

        case EnvelopeStage::Idle:
        default:
            return 0.0f;
    }
}

void PolySynth::advanceEnvelope(Voice& voice, uint32_t samples) {
    if (voice.envStage == EnvelopeStage::Idle) return;

    float timeSeconds = static_cast<float>(samples) / static_cast<float>(m_sampleRate);

    switch (voice.envStage) {
        case EnvelopeStage::Attack: {
            float attackTime = std::max(0.001f, static_cast<float>(attack));
            voice.envProgress += timeSeconds / attackTime;
            if (voice.envProgress >= 1.0f) {
                voice.envProgress = 0.0f;
                voice.envStage = EnvelopeStage::Decay;
            }
            break;
        }

        case EnvelopeStage::Decay: {
            float decayTime = std::max(0.001f, static_cast<float>(decay));
            voice.envProgress += timeSeconds / decayTime;
            if (voice.envProgress >= 1.0f) {
                voice.envProgress = 0.0f;
                voice.envStage = EnvelopeStage::Sustain;
            }
            break;
        }

        case EnvelopeStage::Sustain:
            // Stay in sustain until noteOff
            break;

        case EnvelopeStage::Release: {
            float releaseTime = std::max(0.001f, static_cast<float>(release));
            voice.envProgress += timeSeconds / releaseTime;
            if (voice.envProgress >= 1.0f) {
                voice.envStage = EnvelopeStage::Idle;
                voice.envValue = 0.0f;
            }
            break;
        }

        default:
            break;
    }

    voice.envValue = computeEnvelope(voice);
}

void PolySynth::processVoice(Voice& voice, float* outputL, float* outputR, uint32_t frames) {
    if (!voice.isActive()) return;

    float freq = voice.frequency * centsToRatio(static_cast<float>(detune));
    float unisonCents = static_cast<float>(unisonDetune);
    float freqL = freq * centsToRatio(-unisonCents * 0.5f);
    float freqR = freq * centsToRatio(unisonCents * 0.5f);

    float phaseIncL = (freqL * TWO_PI) / static_cast<float>(m_sampleRate);
    float phaseIncR = (freqR * TWO_PI) / static_cast<float>(m_sampleRate);

    for (uint32_t i = 0; i < frames; ++i) {
        // Update envelope per sample for accuracy
        advanceEnvelope(voice, 1);

        if (voice.envStage == EnvelopeStage::Idle) {
            break;  // Voice became inactive
        }

        float env = voice.envValue;
        float sampleL = generateSample(voice.phaseL) * env;
        float sampleR = generateSample(voice.phaseR) * env;

        outputL[i] += sampleL;
        outputR[i] += sampleR;

        voice.phaseL += phaseIncL;
        voice.phaseR += phaseIncR;

        // Wrap phases
        if (voice.phaseL >= TWO_PI) voice.phaseL -= TWO_PI;
        if (voice.phaseR >= TWO_PI) voice.phaseR -= TWO_PI;
    }
}

void PolySynth::generateBlock(uint32_t frameCount) {
    if (!m_initialized) return;

    // Resize buffer if needed
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    // Temporary buffers for mixing
    std::vector<float> mixL(frameCount, 0.0f);
    std::vector<float> mixR(frameCount, 0.0f);

    // Process all active voices
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        processVoice(m_voices[i], mixL.data(), mixR.data(), frameCount);
    }

    // Apply master volume and interleave to output
    float vol = static_cast<float>(volume);
    // Normalize by voice count to prevent clipping
    float voiceScale = 1.0f / std::sqrt(static_cast<float>(std::max(1, max)));

    for (uint32_t i = 0; i < frameCount; ++i) {
        m_output.samples[i * 2] = mixL[i] * vol * voiceScale;
        m_output.samples[i * 2 + 1] = mixR[i] * vol * voiceScale;
    }
}

bool PolySynth::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizVec2 min(minX, minY);
    VizVec2 max(maxX, maxY);
    float width = maxX - minX;
    float height = maxY - minY;
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;

    // Dark purple background
    dl->AddRectFilled(min, max, VIZ_COL32(30, 25, 50, 255), 4.0f);

    int activeVoices_ = activeVoiceCount();
    int maxVoices_ = static_cast<int>(maxVoices);
    float maxEnv = maxEnvelopeValue();

    // Draw voice activity dots (left side)
    float dotRadius = 4.0f;
    float dotSpacing = 12.0f;
    float dotsStartY = min.y + 10;
    float dotsX = min.x + 10;

    for (int i = 0; i < maxVoices_ && i < 8; i++) {  // Show max 8 dots
        float dotY = dotsStartY + i * dotSpacing;
        uint32_t dotColor = (i < activeVoices_)
            ? VIZ_COL32(100, 255, 150, 255)   // Active - green
            : VIZ_COL32(60, 60, 70, 200);     // Inactive - dim
        dl->AddCircleFilled(VizVec2(dotsX, dotY), dotRadius, dotColor);
    }

    // Draw waveform icon (center)
    float iconSize = std::min(width, height) * 0.4f;
    float iconX = cx - iconSize * 0.5f;
    float iconY = cy - iconSize * 0.5f;
    uint32_t waveColor = VIZ_COL32(180, 120, 255, 180 + (int)(75 * maxEnv));

    if (m_waveform == Waveform::Sine) {
        for (int i = 0; i < 20; i++) {
            float t1 = i / 20.0f;
            float t2 = (i + 1) / 20.0f;
            float y1 = iconY + iconSize * 0.5f - std::sin(t1 * TWO_PI) * iconSize * 0.4f;
            float y2 = iconY + iconSize * 0.5f - std::sin(t2 * TWO_PI) * iconSize * 0.4f;
            dl->AddLine(VizVec2(iconX + t1 * iconSize, y1),
                       VizVec2(iconX + t2 * iconSize, y2), waveColor, 2.0f);
        }
    } else if (m_waveform == Waveform::Square || m_waveform == Waveform::Pulse) {
        float midY = iconY + iconSize * 0.5f;
        float amp = iconSize * 0.4f;
        dl->AddLine(VizVec2(iconX, midY - amp), VizVec2(iconX + iconSize * 0.5f, midY - amp), waveColor, 2.0f);
        dl->AddLine(VizVec2(iconX + iconSize * 0.5f, midY - amp), VizVec2(iconX + iconSize * 0.5f, midY + amp), waveColor, 2.0f);
        dl->AddLine(VizVec2(iconX + iconSize * 0.5f, midY + amp), VizVec2(iconX + iconSize, midY + amp), waveColor, 2.0f);
    } else if (m_waveform == Waveform::Saw) {
        float midY = iconY + iconSize * 0.5f;
        float amp = iconSize * 0.4f;
        dl->AddLine(VizVec2(iconX, midY + amp), VizVec2(iconX + iconSize * 0.5f, midY - amp), waveColor, 2.0f);
        dl->AddLine(VizVec2(iconX + iconSize * 0.5f, midY - amp), VizVec2(iconX + iconSize * 0.5f, midY + amp), waveColor, 2.0f);
        dl->AddLine(VizVec2(iconX + iconSize * 0.5f, midY + amp), VizVec2(iconX + iconSize, midY - amp), waveColor, 2.0f);
    } else if (m_waveform == Waveform::Triangle) {
        float midY = iconY + iconSize * 0.5f;
        float amp = iconSize * 0.4f;
        dl->AddLine(VizVec2(iconX, midY), VizVec2(iconX + iconSize * 0.25f, midY - amp), waveColor, 2.0f);
        dl->AddLine(VizVec2(iconX + iconSize * 0.25f, midY - amp), VizVec2(iconX + iconSize * 0.75f, midY + amp), waveColor, 2.0f);
        dl->AddLine(VizVec2(iconX + iconSize * 0.75f, midY + amp), VizVec2(iconX + iconSize, midY), waveColor, 2.0f);
    }

    // Max envelope level bar (right side)
    float barW = 10.0f;
    float barH = (height - 12) * maxEnv;
    uint32_t levelColor = VIZ_COL32(180 + (int)(75 * maxEnv), 120, 255, 255);
    dl->AddRectFilled(
        VizVec2(max.x - barW - 4, max.y - 4 - barH),
        VizVec2(max.x - 4, max.y - 4),
        levelColor, 2.0f);

    return true;
}

} // namespace vivid::audio
