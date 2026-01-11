#pragma once

/**
 * @file rate_utils.h
 * @brief Tempo-synced rate conversion utilities
 *
 * Provides functions to convert musical divisions to samples, Hz, and seconds.
 */

#include <vivid/audio/clock.h>
#include <vivid/audio_buffer.h>
#include <cstdint>

namespace vivid::audio {

/**
 * @brief Get the multiplier for a clock division relative to quarter note
 * @param div Clock division
 * @return Multiplier (1.0 = quarter note, 0.5 = eighth note, etc.)
 */
inline float divisionMultiplier(ClockDiv div) {
    switch (div) {
        case ClockDiv::Whole:          return 4.0f;
        case ClockDiv::Half:           return 2.0f;
        case ClockDiv::Quarter:        return 1.0f;
        case ClockDiv::Eighth:         return 0.5f;
        case ClockDiv::Sixteenth:      return 0.25f;
        case ClockDiv::ThirtySecond:   return 0.125f;
        case ClockDiv::DottedQuarter:  return 1.5f;
        case ClockDiv::DottedEighth:   return 0.75f;
        case ClockDiv::TripletQuarter: return 2.0f / 3.0f;
        case ClockDiv::TripletEighth:  return 1.0f / 3.0f;
        default:                       return 1.0f;
    }
}

/**
 * @brief Convert a division to duration in seconds
 * @param div Clock division
 * @param bpm Tempo in beats per minute
 * @return Duration in seconds
 */
inline float divisionToSeconds(ClockDiv div, float bpm) {
    float beatsPerSecond = bpm / 60.0f;
    float secondsPerBeat = 1.0f / beatsPerSecond;
    return secondsPerBeat * divisionMultiplier(div);
}

/**
 * @brief Convert a division to frequency in Hz
 * @param div Clock division
 * @param bpm Tempo in beats per minute
 * @return Frequency in Hz
 */
inline float divisionToHz(ClockDiv div, float bpm) {
    return 1.0f / divisionToSeconds(div, bpm);
}

/**
 * @brief Convert a division to sample count
 * @param div Clock division
 * @param bpm Tempo in beats per minute
 * @param sampleRate Sample rate in Hz (default 48kHz)
 * @return Number of samples
 */
inline uint32_t divisionToSamples(ClockDiv div, float bpm, uint32_t sampleRate = AUDIO_SAMPLE_RATE) {
    return static_cast<uint32_t>(divisionToSeconds(div, bpm) * sampleRate);
}

/**
 * @brief Convert beats to samples
 * @param beats Number of beats
 * @param bpm Tempo in beats per minute
 * @param sampleRate Sample rate in Hz (default 48kHz)
 * @return Number of samples
 */
inline uint32_t beatsToSamples(float beats, float bpm, uint32_t sampleRate = AUDIO_SAMPLE_RATE) {
    float secondsPerBeat = 60.0f / bpm;
    return static_cast<uint32_t>(beats * secondsPerBeat * sampleRate);
}

/**
 * @brief Convert samples to beats
 * @param samples Number of samples
 * @param bpm Tempo in beats per minute
 * @param sampleRate Sample rate in Hz (default 48kHz)
 * @return Number of beats
 */
inline float samplesToBeats(uint32_t samples, float bpm, uint32_t sampleRate = AUDIO_SAMPLE_RATE) {
    float secondsPerBeat = 60.0f / bpm;
    float seconds = static_cast<float>(samples) / sampleRate;
    return seconds / secondsPerBeat;
}

} // namespace vivid::audio
