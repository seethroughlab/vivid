#include <vivid/spectrogram_image.h>
#include <vivid/dsp_utils.h>
#include "stb_image_write.h"

#include <vector>
#include <cmath>
#include <algorithm>
#include <complex>

namespace vivid {

// --- Magma-like colormap: black -> purple -> red -> yellow -> white ---
static void magmaColor(float t, uint8_t& r, uint8_t& g, uint8_t& b) {
    // 5-stop gradient approximating magma
    t = std::max(0.0f, std::min(1.0f, t));

    struct Stop { float pos; float r, g, b; };
    static const Stop stops[] = {
        {0.00f,   0,   0,   4},  // near-black
        {0.25f,  63,  16, 100},  // dark purple
        {0.50f, 190,  30,  60},  // red-magenta
        {0.75f, 253, 170,  30},  // orange-yellow
        {1.00f, 252, 253, 191},  // pale yellow-white
    };

    // Find interval
    int i = 0;
    for (i = 0; i < 4; i++) {
        if (t < stops[i + 1].pos) break;
    }
    if (i >= 4) i = 3;

    float frac = (t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
    frac = std::max(0.0f, std::min(1.0f, frac));

    r = static_cast<uint8_t>(stops[i].r + (stops[i + 1].r - stops[i].r) * frac);
    g = static_cast<uint8_t>(stops[i].g + (stops[i + 1].g - stops[i].g) * frac);
    b = static_cast<uint8_t>(stops[i].b + (stops[i + 1].b - stops[i].b) * frac);
}

bool renderSpectrogramPNG(const std::string& outputPath,
                          const float* samples,
                          uint32_t frameCount,
                          uint32_t channels,
                          uint32_t sampleRate,
                          int width,
                          int height,
                          int fftSize) {
    if (!samples || frameCount == 0 || width <= 0 || height <= 0 || fftSize < 64) {
        return false;
    }

    // Ensure fftSize is power of 2
    int n = 1;
    while (n < fftSize) n <<= 1;
    fftSize = n;

    int hop = fftSize / 4;  // 75% overlap
    int numBins = fftSize / 2;
    float nyquist = static_cast<float>(sampleRate) / 2.0f;

    // Mix to mono
    std::vector<float> mono(frameCount);
    if (channels == 1) {
        for (uint32_t i = 0; i < frameCount; i++) mono[i] = samples[i];
    } else {
        for (uint32_t i = 0; i < frameCount; i++) {
            float sum = 0.0f;
            for (uint32_t c = 0; c < channels; c++) {
                sum += samples[i * channels + c];
            }
            mono[i] = sum / static_cast<float>(channels);
        }
    }

    // Compute number of STFT frames
    int numFrames = 0;
    if (static_cast<int>(frameCount) >= fftSize) {
        numFrames = (static_cast<int>(frameCount) - fftSize) / hop + 1;
    }
    if (numFrames <= 0) return false;

    // Precompute Hann window
    auto window = dsp::hannWindow(fftSize);

    // STFT -> magnitude in dB
    // Store as [numFrames][numBins]
    std::vector<float> spectrogram(numFrames * numBins);
    std::vector<std::complex<float>> fftBuf(fftSize);

    float dbFloor = -80.0f;

    for (int f = 0; f < numFrames; f++) {
        int offset = f * hop;

        // Apply window and load into FFT buffer
        for (int i = 0; i < fftSize; i++) {
            fftBuf[i] = std::complex<float>(mono[offset + i] * window[i], 0.0f);
        }

        dsp::fft(fftBuf.data(), fftSize);

        // Magnitude -> dB
        for (int b = 0; b < numBins; b++) {
            float mag = std::abs(fftBuf[b]);
            float db = 20.0f * std::log10(mag + 1e-10f);
            // Normalize: dbFloor..0 -> 0..1
            float norm = (db - dbFloor) / (0.0f - dbFloor);
            spectrogram[f * numBins + b] = std::max(0.0f, std::min(1.0f, norm));
        }
    }

    // Render image: X = time, Y = log-frequency (bottom=low, top=high)
    std::vector<uint8_t> pixels(width * height * 4);

    float logMinFreq = std::log2(20.0f);
    float logMaxFreq = std::log2(nyquist);
    float freqBinWidth = nyquist / static_cast<float>(numBins);

    for (int y = 0; y < height; y++) {
        // Map Y to frequency (bottom = low freq, top = high freq)
        float yNorm = 1.0f - static_cast<float>(y) / static_cast<float>(height - 1);
        float logFreq = logMinFreq + yNorm * (logMaxFreq - logMinFreq);
        float freq = std::pow(2.0f, logFreq);

        // Map frequency to FFT bin (fractional)
        float binF = freq / freqBinWidth;
        int bin0 = static_cast<int>(binF);
        float frac = binF - static_cast<float>(bin0);
        if (bin0 >= numBins - 1) { bin0 = numBins - 2; frac = 1.0f; }
        if (bin0 < 0) { bin0 = 0; frac = 0.0f; }

        for (int x = 0; x < width; x++) {
            // Map X to STFT frame (fractional)
            float frameF = static_cast<float>(x) / static_cast<float>(width - 1) * static_cast<float>(numFrames - 1);
            int frame0 = static_cast<int>(frameF);
            float frameFrac = frameF - static_cast<float>(frame0);
            if (frame0 >= numFrames - 1) { frame0 = numFrames - 2; frameFrac = 1.0f; }
            if (frame0 < 0) { frame0 = 0; frameFrac = 0.0f; }

            // Bilinear interpolation
            float v00 = spectrogram[frame0 * numBins + bin0];
            float v01 = spectrogram[frame0 * numBins + bin0 + 1];
            float v10 = spectrogram[(frame0 + 1) * numBins + bin0];
            float v11 = spectrogram[(frame0 + 1) * numBins + bin0 + 1];

            float v0 = v00 + (v01 - v00) * frac;
            float v1 = v10 + (v11 - v10) * frac;
            float val = v0 + (v1 - v0) * frameFrac;

            uint8_t r, g, b;
            magmaColor(val, r, g, b);

            int idx = (y * width + x) * 4;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = 255;
        }
    }

    return stbi_write_png(outputPath.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
}

} // namespace vivid
