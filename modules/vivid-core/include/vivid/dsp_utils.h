#pragma once

/**
 * @file dsp_utils.h
 * @brief Shared DSP utilities: FFT, windowing, magnitude spectrum
 *
 * Extracted from spectrogram_image.cpp for reuse by audio_analysis.cpp
 * and other DSP consumers. Radix-2 Cooley-Tukey FFT, Hann window.
 */

#include <complex>
#include <vector>
#include <cstdint>

namespace vivid {
namespace dsp {

/**
 * @brief In-place radix-2 Cooley-Tukey FFT (forward)
 * @param x Complex float array, length must be power of 2
 * @param n Length of the array
 */
void fft(std::complex<float>* x, int n);

/**
 * @brief Generate a Hann window of given size
 * @param size Window length
 * @return Vector of window coefficients
 */
std::vector<float> hannWindow(int size);

/**
 * @brief Compute magnitude spectrum from complex FFT output
 * @param fftOutput Complex FFT result
 * @param n FFT size (only first n/2 bins returned)
 * @return Vector of magnitude values (n/2 bins)
 */
std::vector<float> magnitudeSpectrum(const std::complex<float>* fftOutput, int n);

} // namespace dsp
} // namespace vivid
