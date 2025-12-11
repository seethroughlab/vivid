#include <vivid/audio/sample_player.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace vivid::audio {

SamplePlayer& SamplePlayer::bank(const std::string& bankName) {
    m_bankName = bankName;
    return *this;
}

SamplePlayer& SamplePlayer::voices(int v) {
    m_maxVoices = std::clamp(v, 1, MAX_VOICES);
    return *this;
}

void SamplePlayer::init(Context& ctx) {
    allocateOutput();

    // Find the SampleBank
    if (!m_bankName.empty()) {
        auto* op = ctx.chain().getByName(m_bankName);
        m_bank = dynamic_cast<SampleBank*>(op);
        if (!m_bank) {
            std::cerr << "[SamplePlayer] SampleBank not found: " << m_bankName << std::endl;
        }
    }

    // Clear all voices
    for (auto& voice : m_voices) {
        voice.active = false;
        voice.sampleIndex = -1;
    }

    m_initialized = true;
}

void SamplePlayer::process(Context& ctx) {
    // Re-resolve bank if needed (for hot-reload)
    if (!m_bank && !m_bankName.empty()) {
        auto* op = ctx.chain().getByName(m_bankName);
        m_bank = dynamic_cast<SampleBank*>(op);
    }
}

void SamplePlayer::cleanup() {
    stopAll();
    releaseOutput();
    m_initialized = false;
}

void SamplePlayer::generateBlock(uint32_t frameCount) {
    if (!m_initialized || !m_bank) {
        clearOutput();
        return;
    }

    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    // Clear output buffer
    for (uint32_t i = 0; i < frameCount * 2; ++i) {
        m_output.samples[i] = 0.0f;
    }

    const float masterVol = static_cast<float>(m_volume);

    // Mix all active voices
    for (int v = 0; v < m_maxVoices; ++v) {
        Voice& voice = m_voices[v];
        if (!voice.active || voice.sampleIndex < 0) continue;

        const Sample* sample = m_bank->get(static_cast<size_t>(voice.sampleIndex));
        if (!sample || sample->samples.empty()) {
            voice.active = false;
            continue;
        }

        const float* sampleData = sample->samples.data();
        const uint32_t sampleFrames = sample->frameCount;

        for (uint32_t i = 0; i < frameCount; ++i) {
            // Check if voice is done
            if (voice.position >= sampleFrames) {
                if (voice.loop) {
                    voice.position = 0.0;
                } else {
                    voice.active = false;
                    break;
                }
            }

            // Linear interpolation for pitch shifting
            uint32_t pos0 = static_cast<uint32_t>(voice.position);
            uint32_t pos1 = pos0 + 1;
            float frac = static_cast<float>(voice.position - pos0);

            if (pos1 >= sampleFrames) {
                if (voice.loop) {
                    pos1 = 0;
                } else {
                    pos1 = pos0;
                }
            }

            // Interpolate left and right channels
            float left0 = sampleData[pos0 * 2];
            float right0 = sampleData[pos0 * 2 + 1];
            float left1 = sampleData[pos1 * 2];
            float right1 = sampleData[pos1 * 2 + 1];

            float left = left0 + (left1 - left0) * frac;
            float right = right0 + (right1 - right0) * frac;

            // Apply voice volume and pan
            left *= voice.volume * voice.panL * masterVol;
            right *= voice.volume * voice.panR * masterVol;

            // Add to output
            m_output.samples[i * 2] += left;
            m_output.samples[i * 2 + 1] += right;

            // Advance position
            voice.position += voice.pitch;
        }
    }
}

int SamplePlayer::findFreeVoice() {
    // Find inactive voice
    for (int i = 0; i < m_maxVoices; ++i) {
        if (!m_voices[i].active) {
            return i;
        }
    }
    // Voice stealing: use the oldest voice (first one)
    return 0;
}

int SamplePlayer::triggerInternal(int sampleIndex, float vol, float pan, float pitch, bool loop) {
    if (!m_bank || sampleIndex < 0 || static_cast<size_t>(sampleIndex) >= m_bank->count()) {
        return -1;
    }

    int voiceId = findFreeVoice();
    Voice& voice = m_voices[voiceId];

    voice.sampleIndex = sampleIndex;
    voice.position = 0.0;
    voice.volume = vol;
    voice.pitch = pitch;
    voice.loop = loop;
    voice.active = true;

    // Constant power panning
    float panNorm = (pan + 1.0f) * 0.5f;  // 0-1 range
    float angle = panNorm * 1.5707963f;    // 0 to PI/2
    voice.panL = std::cos(angle);
    voice.panR = std::sin(angle);

    return voiceId;
}

// Trigger by index
void SamplePlayer::trigger(int index) {
    triggerInternal(index, 1.0f, 0.0f, 1.0f, false);
}

void SamplePlayer::trigger(int index, float vol) {
    triggerInternal(index, vol, 0.0f, 1.0f, false);
}

void SamplePlayer::trigger(int index, float vol, float pan) {
    triggerInternal(index, vol, pan, 1.0f, false);
}

void SamplePlayer::trigger(int index, float vol, float pan, float pitch) {
    triggerInternal(index, vol, pan, pitch, false);
}

// Trigger by name
void SamplePlayer::trigger(const std::string& name) {
    if (m_bank) {
        int idx = m_bank->indexOf(name);
        if (idx >= 0) trigger(idx);
    }
}

void SamplePlayer::trigger(const std::string& name, float vol) {
    if (m_bank) {
        int idx = m_bank->indexOf(name);
        if (idx >= 0) trigger(idx, vol);
    }
}

void SamplePlayer::trigger(const std::string& name, float vol, float pan) {
    if (m_bank) {
        int idx = m_bank->indexOf(name);
        if (idx >= 0) trigger(idx, vol, pan);
    }
}

void SamplePlayer::trigger(const std::string& name, float vol, float pan, float pitch) {
    if (m_bank) {
        int idx = m_bank->indexOf(name);
        if (idx >= 0) trigger(idx, vol, pan, pitch);
    }
}

// Trigger looped
int SamplePlayer::triggerLoop(int index) {
    return triggerInternal(index, 1.0f, 0.0f, 1.0f, true);
}

int SamplePlayer::triggerLoop(int index, float vol, float pan, float pitch) {
    return triggerInternal(index, vol, pan, pitch, true);
}

int SamplePlayer::triggerLoop(const std::string& name) {
    if (m_bank) {
        int idx = m_bank->indexOf(name);
        if (idx >= 0) return triggerLoop(idx);
    }
    return -1;
}

int SamplePlayer::triggerLoop(const std::string& name, float vol, float pan, float pitch) {
    if (m_bank) {
        int idx = m_bank->indexOf(name);
        if (idx >= 0) return triggerLoop(idx, vol, pan, pitch);
    }
    return -1;
}

// Stop controls
void SamplePlayer::stop(int voiceId) {
    if (voiceId >= 0 && voiceId < MAX_VOICES) {
        m_voices[voiceId].active = false;
    }
}

void SamplePlayer::stopSample(int index) {
    for (int i = 0; i < m_maxVoices; ++i) {
        if (m_voices[i].active && m_voices[i].sampleIndex == index) {
            m_voices[i].active = false;
        }
    }
}

void SamplePlayer::stopSample(const std::string& name) {
    if (m_bank) {
        int idx = m_bank->indexOf(name);
        if (idx >= 0) stopSample(idx);
    }
}

void SamplePlayer::stopAll() {
    for (auto& voice : m_voices) {
        voice.active = false;
    }
}

bool SamplePlayer::isPlaying(int voiceId) const {
    if (voiceId >= 0 && voiceId < MAX_VOICES) {
        return m_voices[voiceId].active;
    }
    return false;
}

int SamplePlayer::activeVoices() const {
    int count = 0;
    for (int i = 0; i < m_maxVoices; ++i) {
        if (m_voices[i].active) ++count;
    }
    return count;
}

} // namespace vivid::audio
