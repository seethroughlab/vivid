// Vivid - Shared DSP Utilities
// Radix-2 Cooley-Tukey FFT, Hann window, magnitude spectrum

#include <vivid/dsp_utils.h>
#include <cmath>
#include <algorithm>

namespace vivid {
namespace dsp {

static constexpr float PI = 3.14159265358979323846f;

void fft(std::complex<float>* x, int n) {
    if (n <= 1) return;

    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    // Butterfly stages
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * PI / static_cast<float>(len);
        std::complex<float> wn(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; j++) {
                std::complex<float> u = x[i + j];
                std::complex<float> v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wn;
            }
        }
    }
}

std::vector<float> hannWindow(int size) {
    std::vector<float> window(size);
    for (int i = 0; i < size; i++) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * PI * i / (size - 1)));
    }
    return window;
}

std::vector<float> magnitudeSpectrum(const std::complex<float>* fftOutput, int n) {
    int numBins = n / 2;
    std::vector<float> mag(numBins);
    for (int i = 0; i < numBins; i++) {
        mag[i] = std::abs(fftOutput[i]);
    }
    return mag;
}

} // namespace dsp
} // namespace vivid
