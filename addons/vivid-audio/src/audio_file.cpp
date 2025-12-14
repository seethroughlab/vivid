#include <vivid/audio/audio_file.h>
#include <vivid/context.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>

namespace vivid::audio {

AudioFile::AudioFile() {
    registerParam(volume);
}
AudioFile::~AudioFile() = default;

AudioFile& AudioFile::file(const std::string& path) {
    m_filePath = path;
    m_needsLoad = true;
    return *this;
}

void AudioFile::init(Context& ctx) {
    allocateOutput();

    if (!m_filePath.empty()) {
        loadWAV(m_filePath);
        m_needsLoad = false;
        m_playing = true;  // Auto-play on load
    }
}

void AudioFile::process(Context& ctx) {
    // Handle file loading on main thread
    if (m_needsLoad && !m_filePath.empty()) {
        loadWAV(m_filePath);
        m_needsLoad = false;
        m_playing = true;
    }
    // Audio generation is handled by generateBlock() on audio thread
}

void AudioFile::generateBlock(uint32_t frameCount) {
    // Resize output buffer if needed
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    // Clear output buffer
    std::memset(m_output.samples, 0, frameCount * AUDIO_CHANNELS * sizeof(float));

    if (!m_playing || m_samples.empty()) {
        return;
    }

    float* out = m_output.samples;

    for (uint32_t i = 0; i < frameCount; ++i) {
        if (m_playPosition >= m_totalFrames) {
            if (m_loop) {
                m_playPosition = 0;
            } else {
                m_playing = false;
                break;
            }
        }

        // Read stereo samples
        uint64_t sampleIndex = m_playPosition * 2;
        float vol = static_cast<float>(volume);
        float left = m_samples[sampleIndex] * vol;
        float right = m_samples[sampleIndex + 1] * vol;

        out[i * 2] = left;
        out[i * 2 + 1] = right;

        m_playPosition++;
    }
}

void AudioFile::cleanup() {
    m_samples.clear();
    m_playing = false;
    m_playPosition = 0;
    releaseOutput();
}

void AudioFile::play() {
    m_playing = true;
}

void AudioFile::pause() {
    m_playing = false;
}

void AudioFile::stop() {
    m_playing = false;
    m_playPosition = 0;
}

void AudioFile::seek(float seconds) {
    uint64_t frame = static_cast<uint64_t>(seconds * m_sampleRate);
    m_playPosition = std::min(frame, m_totalFrames);
}

float AudioFile::currentTime() const {
    return static_cast<float>(m_playPosition) / static_cast<float>(m_sampleRate);
}

float AudioFile::duration() const {
    return static_cast<float>(m_totalFrames) / static_cast<float>(m_sampleRate);
}

// Simple WAV loader
bool AudioFile::loadWAV(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "[AudioFile] Failed to open: " << path << std::endl;
        return false;
    }

    // Read RIFF header
    char riff[4];
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        std::cerr << "[AudioFile] Not a RIFF file: " << path << std::endl;
        return false;
    }

    uint32_t fileSize;
    file.read(reinterpret_cast<char*>(&fileSize), 4);

    char wave[4];
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        std::cerr << "[AudioFile] Not a WAVE file: " << path << std::endl;
        return false;
    }

    // Find fmt chunk
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t fileSampleRate = 0;
    uint16_t bitsPerSample = 0;

    while (file) {
        char chunkId[4];
        uint32_t chunkSize;
        file.read(chunkId, 4);
        file.read(reinterpret_cast<char*>(&chunkSize), 4);

        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            file.read(reinterpret_cast<char*>(&audioFormat), 2);
            file.read(reinterpret_cast<char*>(&numChannels), 2);
            file.read(reinterpret_cast<char*>(&fileSampleRate), 4);
            uint32_t byteRate;
            file.read(reinterpret_cast<char*>(&byteRate), 4);
            uint16_t blockAlign;
            file.read(reinterpret_cast<char*>(&blockAlign), 2);
            file.read(reinterpret_cast<char*>(&bitsPerSample), 2);

            // Skip any extra format bytes
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            // Found data chunk
            uint32_t dataSize = chunkSize;
            uint32_t bytesPerSample = bitsPerSample / 8;
            uint32_t totalSamples = dataSize / bytesPerSample;
            uint32_t totalFrames = totalSamples / numChannels;

            // Read raw data
            std::vector<char> rawData(dataSize);
            file.read(rawData.data(), dataSize);

            // Convert to float
            std::vector<float> floatSamples(totalSamples);

            if (audioFormat == 1) {  // PCM
                if (bitsPerSample == 16) {
                    const int16_t* src = reinterpret_cast<const int16_t*>(rawData.data());
                    for (uint32_t i = 0; i < totalSamples; ++i) {
                        floatSamples[i] = static_cast<float>(src[i]) / 32768.0f;
                    }
                } else if (bitsPerSample == 24) {
                    const uint8_t* src = reinterpret_cast<const uint8_t*>(rawData.data());
                    for (uint32_t i = 0; i < totalSamples; ++i) {
                        int32_t sample = (src[i * 3] | (src[i * 3 + 1] << 8) | (src[i * 3 + 2] << 16));
                        if (sample & 0x800000) sample |= 0xFF000000;  // Sign extend
                        floatSamples[i] = static_cast<float>(sample) / 8388608.0f;
                    }
                } else if (bitsPerSample == 32) {
                    const int32_t* src = reinterpret_cast<const int32_t*>(rawData.data());
                    for (uint32_t i = 0; i < totalSamples; ++i) {
                        floatSamples[i] = static_cast<float>(src[i]) / 2147483648.0f;
                    }
                }
            } else if (audioFormat == 3) {  // IEEE float
                const float* src = reinterpret_cast<const float*>(rawData.data());
                std::copy(src, src + totalSamples, floatSamples.begin());
            } else {
                std::cerr << "[AudioFile] Unsupported audio format: " << audioFormat << std::endl;
                return false;
            }

            // Convert to stereo if mono
            if (numChannels == 1) {
                m_samples.resize(totalFrames * 2);
                for (uint32_t i = 0; i < totalFrames; ++i) {
                    m_samples[i * 2] = floatSamples[i];
                    m_samples[i * 2 + 1] = floatSamples[i];
                }
            } else if (numChannels == 2) {
                m_samples = std::move(floatSamples);
            } else {
                // Downmix to stereo (take first two channels)
                m_samples.resize(totalFrames * 2);
                for (uint32_t i = 0; i < totalFrames; ++i) {
                    m_samples[i * 2] = floatSamples[i * numChannels];
                    m_samples[i * 2 + 1] = floatSamples[i * numChannels + 1];
                }
            }

            // Simple resampling if needed (linear interpolation)
            if (fileSampleRate != AUDIO_SAMPLE_RATE) {
                double ratio = static_cast<double>(fileSampleRate) / AUDIO_SAMPLE_RATE;
                uint32_t newFrames = static_cast<uint32_t>(totalFrames / ratio);
                std::vector<float> resampled(newFrames * 2);

                for (uint32_t i = 0; i < newFrames; ++i) {
                    double srcPos = i * ratio;
                    uint32_t srcIndex = static_cast<uint32_t>(srcPos);
                    float frac = static_cast<float>(srcPos - srcIndex);

                    if (srcIndex + 1 < totalFrames) {
                        // Linear interpolation
                        resampled[i * 2] = m_samples[srcIndex * 2] * (1 - frac) +
                                           m_samples[(srcIndex + 1) * 2] * frac;
                        resampled[i * 2 + 1] = m_samples[srcIndex * 2 + 1] * (1 - frac) +
                                               m_samples[(srcIndex + 1) * 2 + 1] * frac;
                    } else {
                        resampled[i * 2] = m_samples[srcIndex * 2];
                        resampled[i * 2 + 1] = m_samples[srcIndex * 2 + 1];
                    }
                }

                m_samples = std::move(resampled);
                totalFrames = newFrames;
            }

            m_totalFrames = totalFrames;
            m_sampleRate = AUDIO_SAMPLE_RATE;
            m_channels = 2;
            m_playPosition = 0;

            std::cout << "[AudioFile] Loaded: " << path
                      << " (" << m_totalFrames << " frames, "
                      << duration() << "s)" << std::endl;

            return true;
        } else {
            // Skip unknown chunk
            file.seekg(chunkSize, std::ios::cur);
        }
    }

    std::cerr << "[AudioFile] No data chunk found in: " << path << std::endl;
    return false;
}

} // namespace vivid::audio
