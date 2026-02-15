#pragma once

/**
 * @file wav_writer.h
 * @brief WAV file read/write utilities using miniaudio
 *
 * Simple wrappers around miniaudio's encoder/decoder for reading
 * and writing WAV files. Used by audio snapshot CLI and MCP tools.
 */

#include <string>
#include <vector>
#include <cstdint>

namespace vivid {

/**
 * @brief Write interleaved float samples to a WAV file
 *
 * @param path Output file path (.wav)
 * @param samples Interleaved float samples [-1.0, 1.0]
 * @param frameCount Number of frames (samples per channel)
 * @param channels Number of channels (1=mono, 2=stereo)
 * @param sampleRate Sample rate in Hz
 * @return true on success, false on failure
 */
bool writeWAV(const std::string& path, const float* samples,
              uint32_t frameCount, uint32_t channels, uint32_t sampleRate);

/**
 * @brief Read a WAV file into interleaved float samples
 *
 * @param path Input file path (.wav)
 * @param[out] samples Output buffer (resized to fit)
 * @param[out] frameCount Number of frames read
 * @param[out] channels Number of channels
 * @param[out] sampleRate Sample rate in Hz
 * @return true on success, false on failure
 */
bool readWAV(const std::string& path, std::vector<float>& samples,
             uint32_t& frameCount, uint32_t& channels, uint32_t& sampleRate);

} // namespace vivid
