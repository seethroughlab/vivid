/**
 * @file granular.cpp
 * @brief Implementation of granular synthesizer
 */

#include <vivid/audio/granular.h>
#include <cstring>
#include <fstream>
#include <algorithm>

namespace vivid::audio {

Granular::Granular() {
    registerParam(grainSize);
    registerParam(density);
    registerParam(position);
    registerParam(positionSpray);
    registerParam(pitch);
    registerParam(pitchSpray);
    registerParam(panSpray);
    registerParam(volume);

    // Seed random generator
    std::random_device rd;
    m_rng.seed(rd());
}

void Granular::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();

    // Load pending sample if set
    if (!m_pendingPath.empty()) {
        loadWAV(m_pendingPath);
        m_pendingPath.clear();
    }

    // Reset grain timer
    m_grainTimer = 0.0f;
    m_positionPhase = static_cast<float>(position);
}

void Granular::process(Context& ctx) {
    // Nothing to do here - audio generation happens in generateBlock
}

void Granular::cleanup() {
    m_sample.clear();
    m_sampleFrames = 0;
}

bool Granular::loadSample(const std::string& path) {
    // If not initialized yet, store path for later
    if (m_sampleRate == 0) {
        m_pendingPath = path;
        return true;
    }
    return loadWAV(path);
}

void Granular::loadBuffer(const float* samples, uint32_t frameCount) {
    m_sample.resize(frameCount * 2);
    std::memcpy(m_sample.data(), samples, frameCount * 2 * sizeof(float));
    m_sampleFrames = frameCount;
}

float Granular::sampleDuration() const {
    if (m_sampleFrames == 0 || m_sampleRate == 0) return 0.0f;
    return static_cast<float>(m_sampleFrames) / static_cast<float>(m_sampleRate);
}

void Granular::triggerGrain() {
    spawnGrain();
}

void Granular::generateBlock(uint32_t frameCount) {
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    float* output = m_output.samples;

    // Clear output buffer
    std::memset(output, 0, frameCount * 2 * sizeof(float));

    // No sample loaded - output silence
    if (m_sample.empty() || m_sampleFrames == 0) {
        return;
    }

    float vol = static_cast<float>(volume);
    float grainSizeMs = static_cast<float>(grainSize);
    float dens = static_cast<float>(density);
    float pos = static_cast<float>(position);
    float posSpray = static_cast<float>(positionSpray);
    float pitchBase = static_cast<float>(pitch);
    float pitchSpr = static_cast<float>(pitchSpray);
    float panSpr = static_cast<float>(panSpray);

    // Grain scheduling interval
    float grainInterval = static_cast<float>(m_sampleRate) / dens;

    for (uint32_t i = 0; i < frameCount; ++i) {
        // Spawn new grains based on density
        m_grainTimer -= 1.0f;
        if (m_grainTimer <= 0.0f) {
            // Calculate grain parameters
            float grainSamples = (grainSizeMs / 1000.0f) * static_cast<float>(m_sampleRate);

            // Position with spray
            float startPos = m_freeze ? pos : m_positionPhase;
            startPos += randomBipolar() * posSpray;
            startPos = std::max(0.0f, std::min(1.0f, startPos));

            // Pitch with spray
            float grainPitch = pitchBase;
            if (pitchSpr > 0.0f) {
                // Spray in semitones, then convert to ratio
                float semitones = randomBipolar() * pitchSpr * 12.0f;
                grainPitch *= std::pow(2.0f, semitones / 12.0f);
            }

            // Pan with spray
            float pan = randomBipolar() * panSpr;
            float panL = std::cos((pan + 1.0f) * 0.25f * PI);
            float panR = std::sin((pan + 1.0f) * 0.25f * PI);

            // Find inactive grain slot
            Grain* grain = nullptr;
            for (auto& g : m_grains) {
                if (!g.active) {
                    grain = &g;
                    break;
                }
            }

            // If no free slot, steal oldest grain
            if (!grain) {
                uint32_t maxAge = 0;
                for (auto& g : m_grains) {
                    if (g.age > maxAge) {
                        maxAge = g.age;
                        grain = &g;
                    }
                }
            }

            if (grain) {
                grain->active = true;
                grain->samplePos = startPos * static_cast<double>(m_sampleFrames);
                grain->pitch = grainPitch;
                grain->panL = panL;
                grain->panR = panR;
                grain->age = 0;
                grain->duration = static_cast<uint32_t>(grainSamples);
            }

            // Reset timer with some jitter
            m_grainTimer = grainInterval * (0.8f + randomUnipolar() * 0.4f);
        }

        // Auto-advance position
        if (m_autoAdvance && !m_freeze) {
            m_positionPhase += 1.0f / static_cast<float>(m_sampleFrames);
            if (m_positionPhase >= 1.0f) {
                m_positionPhase -= 1.0f;
            }
        } else if (!m_freeze) {
            // Follow position parameter
            m_positionPhase = pos;
        }

        // Process all active grains
        float outL = 0.0f;
        float outR = 0.0f;

        for (auto& grain : m_grains) {
            if (!grain.active) continue;

            // Calculate window envelope
            float t = static_cast<float>(grain.age) / static_cast<float>(grain.duration);
            float env = windowFunction(t);

            // Sample from buffer with interpolation
            float sampleL = sampleAt(grain.samplePos, 0);
            float sampleR = sampleAt(grain.samplePos, 1);

            // Apply envelope and panning
            outL += sampleL * env * grain.panL;
            outR += sampleR * env * grain.panR;

            // Advance grain position
            grain.samplePos += grain.pitch;
            grain.age++;

            // Deactivate finished grains
            if (grain.age >= grain.duration) {
                grain.active = false;
            }

            // Deactivate grains that run past sample end
            if (grain.samplePos >= static_cast<double>(m_sampleFrames) ||
                grain.samplePos < 0) {
                grain.active = false;
            }
        }

        // Apply volume and write to output
        output[i * 2] = outL * vol;
        output[i * 2 + 1] = outR * vol;
    }
}

void Granular::spawnGrain() {
    // Manual grain trigger - implementation same as in generateBlock
    // but without timer management
}

float Granular::windowFunction(float t) const {
    // t is 0-1 through the grain
    t = std::max(0.0f, std::min(1.0f, t));

    switch (m_window) {
        case GrainWindow::Hann:
            // Raised cosine: 0.5 * (1 - cos(2*pi*t))
            return 0.5f * (1.0f - std::cos(2.0f * PI * t));

        case GrainWindow::Triangle:
            // Linear fade in/out
            return (t < 0.5f) ? (2.0f * t) : (2.0f * (1.0f - t));

        case GrainWindow::Rectangle:
            // No envelope (slight fade at edges to avoid clicks)
            if (t < 0.01f) return t / 0.01f;
            if (t > 0.99f) return (1.0f - t) / 0.01f;
            return 1.0f;

        case GrainWindow::Gaussian:
            // Bell curve centered at 0.5
            {
                float x = (t - 0.5f) * 4.0f;  // Scale to -2..2
                return std::exp(-x * x);
            }

        case GrainWindow::Tukey:
            // Flat middle with cosine edges (alpha = 0.5)
            {
                const float alpha = 0.5f;
                if (t < alpha / 2.0f) {
                    return 0.5f * (1.0f + std::cos(2.0f * PI / alpha * (t - alpha / 2.0f)));
                } else if (t > 1.0f - alpha / 2.0f) {
                    return 0.5f * (1.0f + std::cos(2.0f * PI / alpha * (t - 1.0f + alpha / 2.0f)));
                }
                return 1.0f;
            }

        default:
            return 1.0f;
    }
}

float Granular::sampleAt(double pos, int channel) const {
    if (m_sample.empty()) return 0.0f;

    // Linear interpolation
    int32_t pos0 = static_cast<int32_t>(pos);
    int32_t pos1 = pos0 + 1;
    float frac = static_cast<float>(pos - static_cast<double>(pos0));

    // Bounds check
    if (pos0 < 0 || pos0 >= static_cast<int32_t>(m_sampleFrames)) return 0.0f;
    if (pos1 >= static_cast<int32_t>(m_sampleFrames)) pos1 = pos0;

    float s0 = m_sample[pos0 * 2 + channel];
    float s1 = m_sample[pos1 * 2 + channel];

    return s0 + frac * (s1 - s0);
}

bool Granular::loadWAV(const std::string& path) {
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

    m_sample.resize(outputFrames * 2);
    m_sampleFrames = outputFrames;

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
            m_sample[i * 2 + ch] = s0 + frac * (s1 - s0);
        }
    }

    return true;
}

} // namespace vivid::audio
