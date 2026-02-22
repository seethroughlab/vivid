#pragma once

/**
 * @file spectrogram_image.h
 * @brief Render audio spectrogram (frequency over time) to PNG image
 *
 * Used by `vivid inspect --out` to generate spectrogram.png when audio
 * operators are present in the chain. Uses inline radix-2 FFT (no external deps).
 */

#include <cstdint>
#include <string>

namespace vivid {

/**
 * @brief Render audio samples as a spectrogram PNG image
 *
 * Generates a spectrogram showing frequency content over time.
 * Uses STFT with Hann window, log-frequency Y axis, and magma-like colormap.
 *
 * @param outputPath Path for the output PNG file
 * @param samples Interleaved audio samples [-1.0, 1.0]
 * @param frameCount Number of audio frames (samples per channel)
 * @param channels Number of channels (1=mono, 2=stereo; stereo is mixed to mono)
 * @param sampleRate Sample rate in Hz (default: 48000)
 * @param width Image width in pixels (default: 1920)
 * @param height Image height in pixels (default: 480)
 * @param fftSize FFT window size, must be power of 2 (default: 2048)
 * @return true if image was written successfully
 */
bool renderSpectrogramPNG(const std::string& outputPath,
                          const float* samples,
                          uint32_t frameCount,
                          uint32_t channels,
                          uint32_t sampleRate = 48000,
                          int width = 1920,
                          int height = 480,
                          int fftSize = 2048);

} // namespace vivid
