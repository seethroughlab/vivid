#include <vivid/audio/sample_bank.h>
#include <vivid/context.h>
#include <vivid/audio_buffer.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace vivid::audio {

SampleBank& SampleBank::folder(const std::string& path) {
    m_folderPath = path;
    m_needsLoad = true;
    return *this;
}

SampleBank& SampleBank::file(const std::string& path) {
    m_filePaths.push_back(path);
    m_needsLoad = true;
    return *this;
}

void SampleBank::init(Context& ctx) {
    if (m_needsLoad) {
        m_samples.clear();
        m_nameIndex.clear();

        // Load from folder
        if (!m_folderPath.empty()) {
            try {
                for (const auto& entry : fs::directory_iterator(m_folderPath)) {
                    if (entry.is_regular_file()) {
                        auto ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".wav") {
                            Sample sample;
                            if (loadWAV(entry.path().string(), sample)) {
                                sample.name = entry.path().stem().string();
                                m_nameIndex[sample.name] = m_samples.size();
                                m_samples.push_back(std::move(sample));
                            }
                        }
                    }
                }
            } catch (const fs::filesystem_error& e) {
                std::cerr << "[SampleBank] Failed to read folder: " << m_folderPath
                          << " - " << e.what() << std::endl;
            }
        }

        // Load individual files
        for (const auto& filePath : m_filePaths) {
            Sample sample;
            if (loadWAV(filePath, sample)) {
                fs::path p(filePath);
                sample.name = p.stem().string();
                m_nameIndex[sample.name] = m_samples.size();
                m_samples.push_back(std::move(sample));
            }
        }

        std::cout << "[SampleBank] Loaded " << m_samples.size() << " samples" << std::endl;
        m_needsLoad = false;
    }
}

void SampleBank::process(Context& ctx) {
    // SampleBank doesn't produce output - it just stores samples
}

void SampleBank::cleanup() {
    m_samples.clear();
    m_nameIndex.clear();
}

const Sample* SampleBank::get(size_t index) const {
    if (index < m_samples.size()) {
        return &m_samples[index];
    }
    return nullptr;
}

const Sample* SampleBank::get(const std::string& name) const {
    auto it = m_nameIndex.find(name);
    if (it != m_nameIndex.end()) {
        return &m_samples[it->second];
    }
    return nullptr;
}

int SampleBank::indexOf(const std::string& name) const {
    auto it = m_nameIndex.find(name);
    if (it != m_nameIndex.end()) {
        return static_cast<int>(it->second);
    }
    return -1;
}

std::vector<std::string> SampleBank::names() const {
    std::vector<std::string> result;
    result.reserve(m_samples.size());
    for (const auto& sample : m_samples) {
        result.push_back(sample.name);
    }
    return result;
}

bool SampleBank::loadWAV(const std::string& path, Sample& outSample) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "[SampleBank] Failed to open: " << path << std::endl;
        return false;
    }

    // Read RIFF header
    char riff[4];
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        std::cerr << "[SampleBank] Not a RIFF file: " << path << std::endl;
        return false;
    }

    uint32_t fileSize;
    file.read(reinterpret_cast<char*>(&fileSize), 4);

    char wave[4];
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        std::cerr << "[SampleBank] Not a WAVE file: " << path << std::endl;
        return false;
    }

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

            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            uint32_t dataSize = chunkSize;
            uint32_t bytesPerSample = bitsPerSample / 8;
            uint32_t totalSamples = dataSize / bytesPerSample;
            uint32_t totalFrames = totalSamples / numChannels;

            std::vector<char> rawData(dataSize);
            file.read(rawData.data(), dataSize);

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
                        if (sample & 0x800000) sample |= 0xFF000000;
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
                std::cerr << "[SampleBank] Unsupported format: " << audioFormat << std::endl;
                return false;
            }

            // Convert to stereo
            std::vector<float> stereoSamples;
            if (numChannels == 1) {
                stereoSamples.resize(totalFrames * 2);
                for (uint32_t i = 0; i < totalFrames; ++i) {
                    stereoSamples[i * 2] = floatSamples[i];
                    stereoSamples[i * 2 + 1] = floatSamples[i];
                }
            } else if (numChannels == 2) {
                stereoSamples = std::move(floatSamples);
            } else {
                stereoSamples.resize(totalFrames * 2);
                for (uint32_t i = 0; i < totalFrames; ++i) {
                    stereoSamples[i * 2] = floatSamples[i * numChannels];
                    stereoSamples[i * 2 + 1] = floatSamples[i * numChannels + 1];
                }
            }

            // Resample to 48kHz if needed
            if (fileSampleRate != AUDIO_SAMPLE_RATE) {
                double ratio = static_cast<double>(fileSampleRate) / AUDIO_SAMPLE_RATE;
                uint32_t newFrames = static_cast<uint32_t>(totalFrames / ratio);
                std::vector<float> resampled(newFrames * 2);

                for (uint32_t i = 0; i < newFrames; ++i) {
                    double srcPos = i * ratio;
                    uint32_t srcIndex = static_cast<uint32_t>(srcPos);
                    float frac = static_cast<float>(srcPos - srcIndex);

                    if (srcIndex + 1 < totalFrames) {
                        resampled[i * 2] = stereoSamples[srcIndex * 2] * (1 - frac) +
                                           stereoSamples[(srcIndex + 1) * 2] * frac;
                        resampled[i * 2 + 1] = stereoSamples[srcIndex * 2 + 1] * (1 - frac) +
                                               stereoSamples[(srcIndex + 1) * 2 + 1] * frac;
                    } else {
                        resampled[i * 2] = stereoSamples[srcIndex * 2];
                        resampled[i * 2 + 1] = stereoSamples[srcIndex * 2 + 1];
                    }
                }

                stereoSamples = std::move(resampled);
                totalFrames = newFrames;
            }

            outSample.samples = std::move(stereoSamples);
            outSample.frameCount = totalFrames;
            outSample.sampleRate = AUDIO_SAMPLE_RATE;

            return true;
        } else {
            file.seekg(chunkSize, std::ios::cur);
        }
    }

    return false;
}

} // namespace vivid::audio
