#pragma once

/**
 * @file notes.h
 * @brief Musical note frequencies and scale definitions
 *
 * Provides constants for note frequencies across octaves and
 * utilities for working with musical scales.
 */

#include <array>
#include <vector>
#include <cmath>

namespace vivid::audio {

/**
 * @brief Note names (chromatic scale)
 */
enum class Note {
    C = 0, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B
};

// Enharmonic aliases
constexpr Note Db = Note::Cs;
constexpr Note Eb = Note::Ds;
constexpr Note Gb = Note::Fs;
constexpr Note Ab = Note::Gs;
constexpr Note Bb = Note::As;

/**
 * @brief Get frequency for a note at a given octave
 * @param note The note (C, Cs, D, etc.)
 * @param octave Octave number (4 = middle C at 261.63 Hz)
 * @return Frequency in Hz
 *
 * Uses A4 = 440 Hz as reference (standard tuning).
 */
constexpr float noteFreq(Note note, int octave) {
    // A4 = 440 Hz, MIDI note 69
    // Each semitone is 2^(1/12) ratio
    int semitonesFromA4 = (octave - 4) * 12 + static_cast<int>(note) - static_cast<int>(Note::A);
    // Use precomputed 2^(1/12) ≈ 1.059463094
    float freq = 440.0f;
    if (semitonesFromA4 > 0) {
        for (int i = 0; i < semitonesFromA4; ++i) freq *= 1.059463094f;
    } else {
        for (int i = 0; i < -semitonesFromA4; ++i) freq /= 1.059463094f;
    }
    return freq;
}

/**
 * @brief Get frequency from MIDI note number
 * @param midiNote MIDI note number (60 = middle C, 69 = A4)
 * @return Frequency in Hz
 */
constexpr float midiToFreq(int midiNote) {
    float freq = 440.0f;
    int semitonesFromA4 = midiNote - 69;
    if (semitonesFromA4 > 0) {
        for (int i = 0; i < semitonesFromA4; ++i) freq *= 1.059463094f;
    } else {
        for (int i = 0; i < -semitonesFromA4; ++i) freq /= 1.059463094f;
    }
    return freq;
}

// ============================================================================
// Pre-computed note frequencies (A4 = 440 Hz standard tuning)
// ============================================================================

namespace freq {

// Octave 0 (sub-bass)
constexpr float C0  = 16.35f;
constexpr float Cs0 = 17.32f;  constexpr float Db0 = Cs0;
constexpr float D0  = 18.35f;
constexpr float Ds0 = 19.45f;  constexpr float Eb0 = Ds0;
constexpr float E0  = 20.60f;
constexpr float F0  = 21.83f;
constexpr float Fs0 = 23.12f;  constexpr float Gb0 = Fs0;
constexpr float G0  = 24.50f;
constexpr float Gs0 = 25.96f;  constexpr float Ab0 = Gs0;
constexpr float A0  = 27.50f;
constexpr float As0 = 29.14f;  constexpr float Bb0 = As0;
constexpr float B0  = 30.87f;

// Octave 1
constexpr float C1  = 32.70f;
constexpr float Cs1 = 34.65f;  constexpr float Db1 = Cs1;
constexpr float D1  = 36.71f;
constexpr float Ds1 = 38.89f;  constexpr float Eb1 = Ds1;
constexpr float E1  = 41.20f;
constexpr float F1  = 43.65f;
constexpr float Fs1 = 46.25f;  constexpr float Gb1 = Fs1;
constexpr float G1  = 49.00f;
constexpr float Gs1 = 51.91f;  constexpr float Ab1 = Gs1;
constexpr float A1  = 55.00f;
constexpr float As1 = 58.27f;  constexpr float Bb1 = As1;
constexpr float B1  = 61.74f;

// Octave 2
constexpr float C2  = 65.41f;
constexpr float Cs2 = 69.30f;  constexpr float Db2 = Cs2;
constexpr float D2  = 73.42f;
constexpr float Ds2 = 77.78f;  constexpr float Eb2 = Ds2;
constexpr float E2  = 82.41f;
constexpr float F2  = 87.31f;
constexpr float Fs2 = 92.50f;  constexpr float Gb2 = Fs2;
constexpr float G2  = 98.00f;
constexpr float Gs2 = 103.83f; constexpr float Ab2 = Gs2;
constexpr float A2  = 110.00f;
constexpr float As2 = 116.54f; constexpr float Bb2 = As2;
constexpr float B2  = 123.47f;

// Octave 3
constexpr float C3  = 130.81f;
constexpr float Cs3 = 138.59f; constexpr float Db3 = Cs3;
constexpr float D3  = 146.83f;
constexpr float Ds3 = 155.56f; constexpr float Eb3 = Ds3;
constexpr float E3  = 164.81f;
constexpr float F3  = 174.61f;
constexpr float Fs3 = 185.00f; constexpr float Gb3 = Fs3;
constexpr float G3  = 196.00f;
constexpr float Gs3 = 207.65f; constexpr float Ab3 = Gs3;
constexpr float A3  = 220.00f;
constexpr float As3 = 233.08f; constexpr float Bb3 = As3;
constexpr float B3  = 246.94f;

// Octave 4 (middle C)
constexpr float C4  = 261.63f;
constexpr float Cs4 = 277.18f; constexpr float Db4 = Cs4;
constexpr float D4  = 293.66f;
constexpr float Ds4 = 311.13f; constexpr float Eb4 = Ds4;
constexpr float E4  = 329.63f;
constexpr float F4  = 349.23f;
constexpr float Fs4 = 369.99f; constexpr float Gb4 = Fs4;
constexpr float G4  = 392.00f;
constexpr float Gs4 = 415.30f; constexpr float Ab4 = Gs4;
constexpr float A4  = 440.00f;
constexpr float As4 = 466.16f; constexpr float Bb4 = As4;
constexpr float B4  = 493.88f;

// Octave 5
constexpr float C5  = 523.25f;
constexpr float Cs5 = 554.37f; constexpr float Db5 = Cs5;
constexpr float D5  = 587.33f;
constexpr float Ds5 = 622.25f; constexpr float Eb5 = Ds5;
constexpr float E5  = 659.25f;
constexpr float F5  = 698.46f;
constexpr float Fs5 = 739.99f; constexpr float Gb5 = Fs5;
constexpr float G5  = 783.99f;
constexpr float Gs5 = 830.61f; constexpr float Ab5 = Gs5;
constexpr float A5  = 880.00f;
constexpr float As5 = 932.33f; constexpr float Bb5 = As5;
constexpr float B5  = 987.77f;

// Octave 6
constexpr float C6  = 1046.50f;
constexpr float Cs6 = 1108.73f; constexpr float Db6 = Cs6;
constexpr float D6  = 1174.66f;
constexpr float Ds6 = 1244.51f; constexpr float Eb6 = Ds6;
constexpr float E6  = 1318.51f;
constexpr float F6  = 1396.91f;
constexpr float Fs6 = 1479.98f; constexpr float Gb6 = Fs6;
constexpr float G6  = 1567.98f;
constexpr float Gs6 = 1661.22f; constexpr float Ab6 = Gs6;
constexpr float A6  = 1760.00f;
constexpr float As6 = 1864.66f; constexpr float Bb6 = As6;
constexpr float B6  = 1975.53f;

// Octave 7
constexpr float C7  = 2093.00f;
constexpr float Cs7 = 2217.46f; constexpr float Db7 = Cs7;
constexpr float D7  = 2349.32f;
constexpr float Ds7 = 2489.02f; constexpr float Eb7 = Ds7;
constexpr float E7  = 2637.02f;
constexpr float F7  = 2793.83f;
constexpr float Fs7 = 2959.96f; constexpr float Gb7 = Fs7;
constexpr float G7  = 3135.96f;
constexpr float Gs7 = 3322.44f; constexpr float Ab7 = Gs7;
constexpr float A7  = 3520.00f;
constexpr float As7 = 3729.31f; constexpr float Bb7 = As7;
constexpr float B7  = 3951.07f;

// Octave 8
constexpr float C8  = 4186.01f;

} // namespace freq

// ============================================================================
// Scale definitions (intervals in semitones from root)
// ============================================================================

namespace scale {

// Common scales as semitone intervals from root
constexpr std::array<int, 7> Major       = {0, 2, 4, 5, 7, 9, 11};
constexpr std::array<int, 7> Minor       = {0, 2, 3, 5, 7, 8, 10};  // Natural minor
constexpr std::array<int, 7> HarmonicMinor = {0, 2, 3, 5, 7, 8, 11};
constexpr std::array<int, 7> MelodicMinor  = {0, 2, 3, 5, 7, 9, 11};

// Pentatonic scales
constexpr std::array<int, 5> MajorPentatonic = {0, 2, 4, 7, 9};
constexpr std::array<int, 5> MinorPentatonic = {0, 3, 5, 7, 10};

// Blues scale
constexpr std::array<int, 6> Blues = {0, 3, 5, 6, 7, 10};

// Modes
constexpr std::array<int, 7> Ionian     = Major;
constexpr std::array<int, 7> Dorian     = {0, 2, 3, 5, 7, 9, 10};
constexpr std::array<int, 7> Phrygian   = {0, 1, 3, 5, 7, 8, 10};
constexpr std::array<int, 7> Lydian     = {0, 2, 4, 6, 7, 9, 11};
constexpr std::array<int, 7> Mixolydian = {0, 2, 4, 5, 7, 9, 10};
constexpr std::array<int, 7> Aeolian    = Minor;
constexpr std::array<int, 7> Locrian    = {0, 1, 3, 5, 6, 8, 10};

// Chromatic
constexpr std::array<int, 12> Chromatic = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

// Whole tone
constexpr std::array<int, 6> WholeTone = {0, 2, 4, 6, 8, 10};

// Diminished (half-whole)
constexpr std::array<int, 8> Diminished = {0, 1, 3, 4, 6, 7, 9, 10};

} // namespace scale

/**
 * @brief Build an array of frequencies for a scale starting at a root note
 * @tparam N Number of notes in the scale
 * @param root Root note frequency in Hz
 * @param intervals Scale intervals (semitones from root)
 * @return Array of frequencies
 *
 * @code
 * // D minor scale starting at D3
 * auto dMinor = buildScale(freq::D3, scale::Minor);
 * // dMinor = {146.83, 164.81, 174.61, 196.00, 220.00, 233.08, 261.63}
 * @endcode
 */
template<size_t N>
std::array<float, N> buildScale(float root, const std::array<int, N>& intervals) {
    std::array<float, N> result;
    constexpr float semitoneRatio = 1.059463094f;  // 2^(1/12)
    for (size_t i = 0; i < N; ++i) {
        float freq = root;
        for (int j = 0; j < intervals[i]; ++j) {
            freq *= semitoneRatio;
        }
        result[i] = freq;
    }
    return result;
}

/**
 * @brief Build a scale spanning multiple octaves
 * @tparam N Number of notes in one octave of the scale
 * @param root Root note frequency in Hz
 * @param intervals Scale intervals for one octave
 * @param octaves Number of octaves to span
 * @return Vector of frequencies
 */
template<size_t N>
std::vector<float> buildScaleOctaves(float root, const std::array<int, N>& intervals, int octaves) {
    std::vector<float> result;
    result.reserve(N * octaves + 1);
    float octaveRoot = root;
    for (int oct = 0; oct < octaves; ++oct) {
        for (size_t i = 0; i < N; ++i) {
            float freq = octaveRoot;
            constexpr float semitoneRatio = 1.059463094f;
            for (int j = 0; j < intervals[i]; ++j) {
                freq *= semitoneRatio;
            }
            result.push_back(freq);
        }
        octaveRoot *= 2.0f;  // Next octave
    }
    // Add final root note
    result.push_back(octaveRoot);
    return result;
}

/**
 * @brief Transpose a frequency by semitones
 * @param freq Original frequency in Hz
 * @param semitones Number of semitones (positive = up, negative = down)
 * @return Transposed frequency in Hz
 */
constexpr float transpose(float freq, int semitones) {
    constexpr float semitoneRatio = 1.059463094f;
    if (semitones > 0) {
        for (int i = 0; i < semitones; ++i) freq *= semitoneRatio;
    } else {
        for (int i = 0; i < -semitones; ++i) freq /= semitoneRatio;
    }
    return freq;
}

/**
 * @brief Transpose a frequency by octaves
 * @param freq Original frequency in Hz
 * @param octaves Number of octaves (positive = up, negative = down)
 * @return Transposed frequency in Hz
 */
constexpr float transposeOctave(float freq, int octaves) {
    if (octaves > 0) {
        for (int i = 0; i < octaves; ++i) freq *= 2.0f;
    } else {
        for (int i = 0; i < -octaves; ++i) freq /= 2.0f;
    }
    return freq;
}

} // namespace vivid::audio
