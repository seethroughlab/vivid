#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// Write a minimal PCM WAV file with the given number of samples (mono, 44100 Hz, float32).
// Fills samples with a sine wave.
inline bool write_test_wav(const std::string& path, uint32_t num_samples,
                           uint32_t sample_rate = 44100, uint16_t channels = 1) {
    uint32_t byte_rate = sample_rate * channels * sizeof(float);
    uint16_t block_align = channels * sizeof(float);
    uint32_t data_size = num_samples * channels * sizeof(float);
    uint32_t riff_size = 36 + data_size;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;

    // RIFF header
    ofs.write("RIFF", 4);
    ofs.write(reinterpret_cast<const char*>(&riff_size), 4);
    ofs.write("WAVE", 4);

    // fmt chunk
    ofs.write("fmt ", 4);
    uint32_t fmt_size = 16;
    ofs.write(reinterpret_cast<const char*>(&fmt_size), 4);
    uint16_t format = 3;  // IEEE float
    ofs.write(reinterpret_cast<const char*>(&format), 2);
    ofs.write(reinterpret_cast<const char*>(&channels), 2);
    ofs.write(reinterpret_cast<const char*>(&sample_rate), 4);
    ofs.write(reinterpret_cast<const char*>(&byte_rate), 4);
    ofs.write(reinterpret_cast<const char*>(&block_align), 2);
    uint16_t bits = 32;
    ofs.write(reinterpret_cast<const char*>(&bits), 2);

    // data chunk
    ofs.write("data", 4);
    ofs.write(reinterpret_cast<const char*>(&data_size), 4);

    // Generate sine wave samples
    for (uint32_t i = 0; i < num_samples * channels; ++i) {
        float sample = 0.8f * std::sin(2.0f * 3.14159265f * 440.0f * i / sample_rate);
        ofs.write(reinterpret_cast<const char*>(&sample), sizeof(float));
    }

    return true;
}
