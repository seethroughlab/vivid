#include <vivid/audio/band_split.h>
#include <vivid/audio_buffer.h>
#include <kiss_fft.h>
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

struct BandSplit::Impl {
    kiss_fft_cfg cfg = nullptr;
    std::vector<kiss_fft_cpx> fftIn;
    std::vector<kiss_fft_cpx> fftOut;
    std::vector<float> window;
};

BandSplit::BandSplit() : m_impl(std::make_unique<Impl>()) {
    registerParam(smoothing);
}

BandSplit::~BandSplit() {
    cleanupAnalyzer();
}

void BandSplit::setFftSize(int n) {
    if (n <= 256) n = 256;
    else if (n <= 512) n = 512;
    else if (n <= 1024) n = 1024;
    else n = 2048;

    if (n != m_fftSize) {
        m_fftSize = n;
        allocateBuffers();
    }
}

void BandSplit::allocateBuffers() {
    // Free old FFT
    if (m_impl->cfg) {
        kiss_fft_free(m_impl->cfg);
        m_impl->cfg = nullptr;
    }

    // Allocate FFT
    m_impl->cfg = kiss_fft_alloc(m_fftSize, 0, nullptr, nullptr);
    m_impl->fftIn.resize(m_fftSize);
    m_impl->fftOut.resize(m_fftSize);

    // Hann window
    m_impl->window.resize(m_fftSize);
    for (int i = 0; i < m_fftSize; i++) {
        m_impl->window[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * i / (m_fftSize - 1)));
    }

    // Buffers
    m_inputBuffer.resize(m_fftSize, 0.0f);
    m_inputWritePos = 0;
    m_spectrum.resize(m_fftSize / 2, 0.0f);

    // Pre-compute bin ranges for standard bands
    m_subBassBins[0] = frequencyToBin(20);
    m_subBassBins[1] = frequencyToBin(60);
    m_bassBins[0] = frequencyToBin(60);
    m_bassBins[1] = frequencyToBin(250);
    m_lowMidBins[0] = frequencyToBin(250);
    m_lowMidBins[1] = frequencyToBin(500);
    m_midBins[0] = frequencyToBin(500);
    m_midBins[1] = frequencyToBin(2000);
    m_highMidBins[0] = frequencyToBin(2000);
    m_highMidBins[1] = frequencyToBin(4000);
    m_highBins[0] = frequencyToBin(4000);
    m_highBins[1] = frequencyToBin(20000);
}

int BandSplit::frequencyToBin(float hz) const {
    int bin = static_cast<int>(hz * m_fftSize / m_sampleRate + 0.5f);
    return std::clamp(bin, 0, m_fftSize / 2 - 1);
}

void BandSplit::initAnalyzer(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateBuffers();
}

void BandSplit::analyze(const float* input, uint32_t frames, uint32_t channels) {
    if (!m_impl->cfg || frames == 0) return;

    // Mix to mono and accumulate
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

    // Apply window
    for (int i = 0; i < m_fftSize; i++) {
        int readPos = (m_inputWritePos + i) % m_fftSize;
        m_impl->fftIn[i].r = m_inputBuffer[readPos] * m_impl->window[i];
        m_impl->fftIn[i].i = 0.0f;
    }

    // FFT
    kiss_fft(m_impl->cfg, m_impl->fftIn.data(), m_impl->fftOut.data());

    // Magnitude spectrum
    int binCount = m_fftSize / 2;
    float scale = 2.0f / m_fftSize;

    for (int i = 0; i < binCount; i++) {
        float re = m_impl->fftOut[i].r;
        float im = m_impl->fftOut[i].i;
        m_spectrum[i] = std::sqrt(re * re + im * im) * scale;
    }

    // Compute bands with smoothing
    float smooth = static_cast<float>(smoothing);
    float attack = 1.0f - smooth;

    float newSubBass = computeBand(m_subBassBins[0], m_subBassBins[1]);
    float newBass = computeBand(m_bassBins[0], m_bassBins[1]);
    float newLowMid = computeBand(m_lowMidBins[0], m_lowMidBins[1]);
    float newMid = computeBand(m_midBins[0], m_midBins[1]);
    float newHighMid = computeBand(m_highMidBins[0], m_highMidBins[1]);
    float newHigh = computeBand(m_highBins[0], m_highBins[1]);

    m_subBass = m_subBass * smooth + newSubBass * attack;
    m_bass = m_bass * smooth + newBass * attack;
    m_lowMid = m_lowMid * smooth + newLowMid * attack;
    m_mid = m_mid * smooth + newMid * attack;
    m_highMid = m_highMid * smooth + newHighMid * attack;
    m_high = m_high * smooth + newHigh * attack;
}

float BandSplit::computeBand(int lowBin, int highBin) const {
    if (lowBin > highBin) std::swap(lowBin, highBin);
    if (lowBin >= static_cast<int>(m_spectrum.size())) return 0.0f;
    highBin = std::min(highBin, static_cast<int>(m_spectrum.size()) - 1);

    float sum = 0.0f;
    for (int i = lowBin; i <= highBin; i++) {
        sum += m_spectrum[i];
    }
    return sum / (highBin - lowBin + 1);
}

float BandSplit::band(float lowHz, float highHz) const {
    int lowBin = frequencyToBin(lowHz);
    int highBin = frequencyToBin(highHz);
    return computeBand(lowBin, highBin);
}

void BandSplit::cleanupAnalyzer() {
    if (m_impl->cfg) {
        kiss_fft_free(m_impl->cfg);
        m_impl->cfg = nullptr;
    }
}

bool BandSplit::drawVisualization(ImDrawList* dl, float minX, float minY, float maxX, float maxY) {
    ImVec2 min(minX, minY);
    ImVec2 max(maxX, maxY);
    float width = maxX - minX - 8.0f;
    float height = maxY - minY - 8.0f;
    float startX = minX + 4.0f;

    // Dark purple background
    dl->AddRectFilled(min, max, IM_COL32(40, 30, 50, 255), 4.0f);

    // 6 frequency band bars
    float bandValues[6] = {
        m_subBass, m_bass, m_lowMid,
        m_mid, m_highMid, m_high
    };
    ImU32 bandColors[6] = {
        IM_COL32(120, 60, 160, 255),  // SubBass - deep purple
        IM_COL32(60, 100, 200, 255),  // Bass - blue
        IM_COL32(60, 180, 180, 255),  // LowMid - cyan
        IM_COL32(100, 200, 100, 255), // Mid - green
        IM_COL32(220, 200, 60, 255),  // HighMid - yellow
        IM_COL32(220, 100, 80, 255)   // High - red/orange
    };

    float barW = width / 6.0f - 2.0f;
    for (int i = 0; i < 6; i++) {
        float barH = bandValues[i] * 2.0f * height;  // Scale up
        barH = std::min(barH, height);
        float x = startX + i * (barW + 2.0f) + 1.0f;
        float y = max.y - 4.0f - barH;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barW, max.y - 4.0f),
                         bandColors[i], 2.0f);
    }

    return true;
}

} // namespace vivid::audio
