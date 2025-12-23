#include <vivid/audio/fft.h>
#include <vivid/audio_buffer.h>
#include <kiss_fft.h>

#include <cmath>
#include <algorithm>

namespace vivid::audio {

struct FFT::Impl {
    kiss_fft_cfg cfg = nullptr;
    std::vector<kiss_fft_cpx> fftIn;
    std::vector<kiss_fft_cpx> fftOut;
    std::vector<float> window;  // Hann window
};

FFT::FFT() : m_impl(std::make_unique<Impl>()) {
    registerParam(smoothing);
}

FFT::~FFT() {
    cleanupAnalyzer();
}

void FFT::setSize(int n) {
    // Clamp to valid power of 2
    if (n <= 256) n = 256;
    else if (n <= 512) n = 512;
    else if (n <= 1024) n = 1024;
    else if (n <= 2048) n = 2048;
    else n = 4096;

    if (n != m_fftSize) {
        m_fftSize = n;
        allocateBuffers();
    }
}

void FFT::allocateBuffers() {
    // Free old FFT config
    if (m_impl->cfg) {
        kiss_fft_free(m_impl->cfg);
        m_impl->cfg = nullptr;
    }

    // Allocate FFT
    m_impl->cfg = kiss_fft_alloc(m_fftSize, 0, nullptr, nullptr);
    m_impl->fftIn.resize(m_fftSize);
    m_impl->fftOut.resize(m_fftSize);

    // Generate Hann window
    m_impl->window.resize(m_fftSize);
    for (int i = 0; i < m_fftSize; i++) {
        m_impl->window[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * i / (m_fftSize - 1)));
    }

    // Allocate buffers
    m_inputBuffer.resize(m_fftSize, 0.0f);
    m_inputWritePos = 0;

    m_spectrum.resize(m_fftSize / 2, 0.0f);
    m_smoothedSpectrum.resize(m_fftSize / 2, 0.0f);
}

void FFT::initAnalyzer(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateBuffers();
}

void FFT::analyze(const float* input, uint32_t frames, uint32_t channels) {
    if (!m_impl->cfg || frames == 0) return;

    // Mix stereo to mono and accumulate into input buffer
    for (uint32_t i = 0; i < frames; i++) {
        float sample;
        if (channels == 2) {
            sample = (input[i * 2] + input[i * 2 + 1]) * 0.5f;
        } else {
            sample = input[i];
        }

        m_inputBuffer[m_inputWritePos] = sample;
        m_inputWritePos = (m_inputWritePos + 1) % m_fftSize;
    }

    // Apply window and copy to FFT input
    // Read from oldest sample to newest (circular buffer unwrap)
    for (int i = 0; i < m_fftSize; i++) {
        int readPos = (m_inputWritePos + i) % m_fftSize;
        m_impl->fftIn[i].r = m_inputBuffer[readPos] * m_impl->window[i];
        m_impl->fftIn[i].i = 0.0f;
    }

    // Perform FFT
    kiss_fft(m_impl->cfg, m_impl->fftIn.data(), m_impl->fftOut.data());

    // Calculate magnitude spectrum
    int binCount = m_fftSize / 2;
    float scale = 2.0f / m_fftSize;  // Normalize

    for (int i = 0; i < binCount; i++) {
        float re = m_impl->fftOut[i].r;
        float im = m_impl->fftOut[i].i;
        float mag = std::sqrt(re * re + im * im) * scale;

        // Apply smoothing
        float smooth = static_cast<float>(smoothing);
        m_smoothedSpectrum[i] = m_smoothedSpectrum[i] * smooth +
                                 mag * (1.0f - smooth);
        m_spectrum[i] = m_smoothedSpectrum[i];
    }
}

void FFT::cleanupAnalyzer() {
    if (m_impl->cfg) {
        kiss_fft_free(m_impl->cfg);
        m_impl->cfg = nullptr;
    }
}

float FFT::bin(int index) const {
    if (index < 0 || index >= static_cast<int>(m_spectrum.size())) {
        return 0.0f;
    }
    return m_spectrum[index];
}

float FFT::binFrequency(int index) const {
    return static_cast<float>(index) * m_sampleRate / m_fftSize;
}

int FFT::frequencyToBin(float hz) const {
    int bin = static_cast<int>(hz * m_fftSize / m_sampleRate + 0.5f);
    return std::clamp(bin, 0, static_cast<int>(m_spectrum.size()) - 1);
}

float FFT::band(float lowHz, float highHz) const {
    int lowBin = frequencyToBin(lowHz);
    int highBin = frequencyToBin(highHz);

    if (lowBin > highBin) std::swap(lowBin, highBin);
    if (lowBin >= static_cast<int>(m_spectrum.size())) return 0.0f;

    highBin = std::min(highBin, static_cast<int>(m_spectrum.size()) - 1);

    float sum = 0.0f;
    int count = 0;
    for (int i = lowBin; i <= highBin; i++) {
        sum += m_spectrum[i];
        count++;
    }

    return count > 0 ? sum / count : 0.0f;
}

bool FFT::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizVec2 min(minX, minY);
    VizVec2 max(maxX, maxY);
    float width = maxX - minX - 8.0f;
    float height = maxY - minY - 8.0f;
    float startX = minX + 4.0f;

    // Dark purple background
    dl->AddRectFilled(min, max, VIZ_COL32(40, 30, 50, 255), 4.0f);

    const float* spec = m_spectrum.data();
    int bins = binCount();
    constexpr int NUM_BARS = 24;

    float barW = width / NUM_BARS - 1.0f;

    for (int i = 0; i < NUM_BARS; i++) {
        // Sample from spectrum (logarithmic distribution)
        int binIdx = static_cast<int>(std::pow(static_cast<float>(i + 1) / NUM_BARS, 2.0f) * bins * 0.5f);
        binIdx = std::min(binIdx, bins - 1);
        float mag = spec ? spec[binIdx] * 3.0f : 0.0f;  // Scale up
        mag = std::min(mag, 1.0f);

        float barH = mag * height;
        float x = startX + i * (barW + 1.0f);
        float y = max.y - 4.0f - barH;

        // Color gradient: blue -> purple -> pink
        float t = static_cast<float>(i) / NUM_BARS;
        uint8_t r = static_cast<uint8_t>(80 + t * 140);
        uint8_t g = static_cast<uint8_t>(80 - t * 40);
        uint8_t b = static_cast<uint8_t>(180 - t * 40);
        dl->AddRectFilled(VizVec2(x, y), VizVec2(x + barW, max.y - 4.0f),
                         VIZ_COL32(r, g, b, 220), 1.0f);
    }

    return true;
}

} // namespace vivid::audio
