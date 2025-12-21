// Vivid Audio - Sampler Implementation

#include <vivid/audio/sampler.h>
#include <vivid/asset_loader.h>
#include <vivid/context.h>
#include <cstring>
#include <fstream>
#include <limits>

namespace vivid::audio {

Sampler::Sampler() {
    registerParam(volume);
    registerParam(rootNote);
    registerParam(maxVoices);
    registerParam(attack);
    registerParam(decay);
    registerParam(sustain);
    registerParam(release);

    // Pre-allocate maximum voices
    m_voices.resize(32);
}

void Sampler::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_initialized = true;

    // Load pending sample if set before init
    if (!m_pendingPath.empty()) {
        loadSample(m_pendingPath);
        m_pendingPath.clear();
    }
}

void Sampler::process(Context& ctx) {
    if (!m_initialized) return;
    // Audio generation happens in generateBlock()
}

void Sampler::cleanup() {
    m_voices.clear();
    m_samples.clear();
    releaseOutput();
    m_initialized = false;
}

bool Sampler::loadSample(const std::string& path) {
    // If not initialized yet, store path for later
    if (!m_initialized) {
        m_pendingPath = path;
        return true;
    }

    // Use AssetLoader for path resolution
    auto resolved = AssetLoader::instance().resolve(path);
    std::string loadPath = resolved.empty() ? path : resolved.string();

    return loadWAV(loadPath);
}

float Sampler::sampleDuration() const {
    if (m_sampleFrames == 0 || m_sampleRate == 0) return 0.0f;
    return static_cast<float>(m_sampleFrames) / static_cast<float>(m_sampleRate);
}

void Sampler::setLoopPoints(float startSec, float endSec) {
    m_loopStart = static_cast<uint64_t>(startSec * static_cast<float>(m_sampleRate));
    m_loopEnd = endSec > 0.0f
        ? static_cast<uint64_t>(endSec * static_cast<float>(m_sampleRate))
        : m_sampleFrames;

    // Clamp to valid range
    if (m_loopStart >= m_sampleFrames) m_loopStart = 0;
    if (m_loopEnd > m_sampleFrames) m_loopEnd = m_sampleFrames;
    if (m_loopEnd <= m_loopStart) m_loopEnd = m_sampleFrames;
}

int Sampler::noteOn(int midiNote, float velocity) {
    if (m_samples.empty()) return -1;

    // Find a voice to use
    int voiceIdx = findFreeVoice();
    if (voiceIdx < 0) {
        voiceIdx = findVoiceToSteal();
    }
    if (voiceIdx < 0) {
        return -1;  // No voice available
    }

    Voice& voice = m_voices[voiceIdx];
    voice.midiNote = midiNote;
    voice.position = 0.0;
    voice.pitch = pitchFromNote(midiNote);
    voice.velocity = velocity;
    voice.envStage = EnvelopeStage::Attack;
    voice.envValue = 0.0f;
    voice.envProgress = 0.0f;
    voice.noteId = ++m_noteCounter;

    return voiceIdx;
}

void Sampler::noteOff(int midiNote) {
    int voiceIdx = findVoiceByNote(midiNote);
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

void Sampler::allNotesOff() {
    for (auto& voice : m_voices) {
        if (voice.envStage != EnvelopeStage::Idle &&
            voice.envStage != EnvelopeStage::Release) {
            voice.envStage = EnvelopeStage::Release;
            voice.envProgress = 0.0f;
            voice.releaseStartValue = voice.envValue;
        }
    }
}

void Sampler::panic() {
    for (auto& voice : m_voices) {
        voice.envStage = EnvelopeStage::Idle;
        voice.envValue = 0.0f;
        voice.midiNote = -1;
    }
}

int Sampler::activeVoiceCount() const {
    int count = 0;
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive()) {
            ++count;
        }
    }
    return count;
}

int Sampler::findFreeVoice() const {
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (!m_voices[i].isActive()) {
            return i;
        }
    }
    return -1;
}

int Sampler::findVoiceToSteal() const {
    if (m_stealMode == SamplerVoiceStealMode::None) {
        return -1;
    }

    int max = static_cast<int>(maxVoices);
    int stealIdx = -1;

    if (m_stealMode == SamplerVoiceStealMode::Oldest) {
        uint64_t oldestId = std::numeric_limits<uint64_t>::max();
        for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
            if (m_voices[i].isActive() && m_voices[i].noteId < oldestId) {
                oldestId = m_voices[i].noteId;
                stealIdx = i;
            }
        }
    } else if (m_stealMode == SamplerVoiceStealMode::Quietest) {
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

int Sampler::findVoiceByNote(int midiNote) const {
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive() &&
            !m_voices[i].isReleasing() &&
            m_voices[i].midiNote == midiNote) {
            return i;
        }
    }
    return -1;
}

float Sampler::computeEnvelope(const Voice& voice) const {
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

void Sampler::advanceEnvelope(Voice& voice, uint32_t samples) {
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

float Sampler::sampleAt(double position, int channel) const {
    if (m_samples.empty()) return 0.0f;

    size_t pos0 = static_cast<size_t>(position);
    size_t pos1 = pos0 + 1;

    // Bounds check
    if (pos0 >= m_sampleFrames) return 0.0f;
    if (pos1 >= m_sampleFrames) pos1 = pos0;

    float frac = static_cast<float>(position - static_cast<double>(pos0));

    // Interleaved stereo: [L0, R0, L1, R1, ...]
    float s0 = m_samples[pos0 * 2 + channel];
    float s1 = m_samples[pos1 * 2 + channel];

    // Linear interpolation
    return s0 + frac * (s1 - s0);
}

void Sampler::processVoice(Voice& voice, float* outputL, float* outputR, uint32_t frames) {
    if (!voice.isActive()) return;

    for (uint32_t i = 0; i < frames; ++i) {
        // Update envelope per sample
        advanceEnvelope(voice, 1);

        if (voice.envStage == EnvelopeStage::Idle) {
            break;  // Voice became inactive
        }

        // Check if we've reached end of sample
        if (voice.position >= static_cast<double>(m_sampleFrames)) {
            if (m_loopEnabled) {
                // Loop back
                uint64_t loopLen = (m_loopEnd > 0 ? m_loopEnd : m_sampleFrames) - m_loopStart;
                if (loopLen > 0) {
                    voice.position = static_cast<double>(m_loopStart) +
                        std::fmod(voice.position - static_cast<double>(m_loopStart),
                                  static_cast<double>(loopLen));
                }
            } else {
                // End of sample, release
                if (voice.envStage != EnvelopeStage::Release) {
                    voice.envStage = EnvelopeStage::Release;
                    voice.envProgress = 0.0f;
                    voice.releaseStartValue = voice.envValue;
                }
                // Output silence for rest of this sample
                voice.position = static_cast<double>(m_sampleFrames);
            }
        }

        float env = voice.envValue * voice.velocity;
        float sampleL = sampleAt(voice.position, 0) * env;
        float sampleR = sampleAt(voice.position, 1) * env;

        outputL[i] += sampleL;
        outputR[i] += sampleR;

        // Advance position by pitch
        voice.position += static_cast<double>(voice.pitch);
    }
}

void Sampler::generateBlock(uint32_t frameCount) {
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

bool Sampler::loadWAV(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read RIFF header
    char riff[4];
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        return false;
    }

    uint32_t fileSize;
    file.read(reinterpret_cast<char*>(&fileSize), 4);

    char wave[4];
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        return false;
    }

    // Find fmt and data chunks
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t fileSampleRate = 0;
    uint16_t bitsPerSample = 0;
    std::vector<float> rawSamples;

    while (file.good()) {
        char chunkId[4];
        uint32_t chunkSize;

        file.read(chunkId, 4);
        if (!file.good()) break;

        file.read(reinterpret_cast<char*>(&chunkSize), 4);
        if (!file.good()) break;

        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            file.read(reinterpret_cast<char*>(&audioFormat), 2);
            file.read(reinterpret_cast<char*>(&numChannels), 2);
            file.read(reinterpret_cast<char*>(&fileSampleRate), 4);
            file.seekg(6, std::ios::cur);  // Skip byteRate and blockAlign
            file.read(reinterpret_cast<char*>(&bitsPerSample), 2);

            // Skip extra format bytes if present
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            // Read sample data
            uint32_t numSamples = chunkSize / (bitsPerSample / 8);
            rawSamples.resize(numSamples);

            if (bitsPerSample == 16) {
                std::vector<int16_t> buffer(numSamples);
                file.read(reinterpret_cast<char*>(buffer.data()), chunkSize);
                for (uint32_t i = 0; i < numSamples; ++i) {
                    rawSamples[i] = static_cast<float>(buffer[i]) / 32768.0f;
                }
            } else if (bitsPerSample == 24) {
                for (uint32_t i = 0; i < numSamples; ++i) {
                    uint8_t bytes[3];
                    file.read(reinterpret_cast<char*>(bytes), 3);
                    int32_t value = (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];
                    if (value & 0x800000) value |= 0xFF000000;  // Sign extend
                    rawSamples[i] = static_cast<float>(value) / 8388608.0f;
                }
            } else if (bitsPerSample == 32 && audioFormat == 3) {
                // 32-bit float
                file.read(reinterpret_cast<char*>(rawSamples.data()), chunkSize);
            } else {
                return false;  // Unsupported format
            }
            break;
        } else {
            // Skip unknown chunk
            file.seekg(chunkSize, std::ios::cur);
        }
    }

    if (rawSamples.empty() || numChannels == 0) {
        return false;
    }

    // Convert to stereo interleaved at target sample rate
    uint32_t inputFrames = static_cast<uint32_t>(rawSamples.size()) / numChannels;

    // Simple sample rate conversion (linear interpolation)
    double ratio = static_cast<double>(m_sampleRate) / static_cast<double>(fileSampleRate);
    uint32_t outputFrames = static_cast<uint32_t>(static_cast<double>(inputFrames) * ratio);

    m_samples.resize(outputFrames * 2);
    m_sampleFrames = outputFrames;
    m_channels = 2;

    for (uint32_t i = 0; i < outputFrames; ++i) {
        double srcPos = static_cast<double>(i) / ratio;
        uint32_t srcIdx = static_cast<uint32_t>(srcPos);
        float frac = static_cast<float>(srcPos - static_cast<double>(srcIdx));

        if (srcIdx >= inputFrames - 1) {
            srcIdx = inputFrames - 2;
            frac = 1.0f;
        }

        for (int ch = 0; ch < 2; ++ch) {
            int srcCh = (numChannels == 1) ? 0 : ch;
            float s0 = rawSamples[srcIdx * numChannels + srcCh];
            float s1 = rawSamples[(srcIdx + 1) * numChannels + srcCh];
            m_samples[i * 2 + ch] = s0 + frac * (s1 - s0);
        }
    }

    // Reset loop points
    m_loopStart = 0;
    m_loopEnd = m_sampleFrames;

    return true;
}

} // namespace vivid::audio
