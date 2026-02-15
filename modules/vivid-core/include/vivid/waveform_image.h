#pragma once

/**
 * @file waveform_image.h
 * @brief Render audio waveform overview to PNG image
 *
 * Used by `vivid inspect --out` to generate waveform.png when audio
 * operators are present in the chain.
 */

#include <string>
#include <vector>

namespace vivid {

/**
 * @brief Render audio samples as a waveform PNG image
 *
 * Generates a waveform overview image showing amplitude over time.
 * Stereo channels are rendered as overlapping waveforms (left=cyan, right=magenta).
 * Mono renders as a single cyan waveform.
 *
 * @param outputPath Path for the output PNG file
 * @param samples Interleaved audio samples [-1.0, 1.0]
 * @param frameCount Number of audio frames (samples per channel)
 * @param channels Number of channels (1=mono, 2=stereo)
 * @param width Image width in pixels (default: 800)
 * @param height Image height in pixels (default: 200)
 * @return true if image was written successfully
 */
bool renderWaveformPNG(const std::string& outputPath,
                       const float* samples,
                       uint32_t frameCount,
                       uint32_t channels,
                       int width = 800,
                       int height = 200);

} // namespace vivid
