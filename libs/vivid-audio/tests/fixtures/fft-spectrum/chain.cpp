// Testing Fixture: FFT Spectrum Visualization
// Tests FFT analyzer with frequency band visualization
//
// Visual verification:
// - Canvas-drawn spectrum bars
// - Bass/mids/highs indicators
// - Smooth response to audio changes

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

constexpr int NUM_BARS = 32;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio sources with rich harmonic content
    auto& synth1 = chain.add<Synth>("synth1");
    synth1.setWaveform(Waveform::Saw);
    synth1.frequency = 110.0f;
    synth1.volume = 0.3f;

    auto& synth2 = chain.add<Synth>("synth2");
    synth2.setWaveform(Waveform::Square);
    synth2.frequency = 220.0f;
    synth2.volume = 0.2f;

    // Mix synths
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.setInput(0, "synth1");
    mixer.setInput(1, "synth2");
    mixer.setGain(0, 1.0f);
    mixer.setGain(1, 1.0f);

    // FFT Analysis
    auto& fft = chain.add<FFT>("fft");
    fft.input("mixer");
    fft.setSize(1024);
    fft.smoothing = 0.8f;

    // Canvas for visualization
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);

    chain.output("canvas");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = static_cast<float>(ctx.time());

    // Animate synth frequencies
    auto& synth1 = chain.get<Synth>("synth1");
    synth1.frequency = 110.0f + std::sin(t * 0.3f) * 20.0f;

    auto& synth2 = chain.get<Synth>("synth2");
    synth2.frequency = 220.0f + std::sin(t * 0.5f) * 30.0f;

    // Get FFT data
    auto& fft = chain.get<FFT>("fft");
    float bass = fft.band(20, 250);
    float mids = fft.band(250, 2000);
    float highs = fft.band(2000, 8000);

    // Draw visualization
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.02f, 0.02f, 0.05f, 1.0f);

    // Draw spectrum bars
    int barWidth = 1280 / NUM_BARS - 4;
    int maxBarHeight = 400;
    int barY = 650;

    for (int i = 0; i < NUM_BARS; i++) {
        // Get magnitude for this frequency bin using logarithmic scaling
        float freq = 20.0f * std::pow(1000.0f, static_cast<float>(i) / NUM_BARS);
        int binIndex = fft.frequencyToBin(freq);
        float mag = fft.bin(binIndex);

        int barHeight = static_cast<int>(mag * maxBarHeight);
        int x = i * (barWidth + 4) + 2;

        // Color gradient from bass (red) to highs (blue)
        float hue = static_cast<float>(i) / NUM_BARS;
        canvas.fillStyle(
            1.0f - hue * 0.5f,
            0.3f + hue * 0.4f,
            0.3f + hue * 0.7f,
            1.0f
        );
        canvas.fillRect(static_cast<float>(x), static_cast<float>(barY - barHeight),
                       static_cast<float>(barWidth), static_cast<float>(barHeight));
    }

    // Draw band indicators at top
    float indicatorY = 80.0f;
    float indicatorSize = 60.0f;

    // Bass indicator
    canvas.fillStyle(1.0f, 0.3f, 0.2f, 0.5f + bass * 0.5f);
    canvas.fillCircle(200.0f, indicatorY, indicatorSize * (0.5f + bass * 0.5f));

    // Mids indicator
    canvas.fillStyle(0.3f, 1.0f, 0.3f, 0.5f + mids * 0.5f);
    canvas.fillCircle(640.0f, indicatorY, indicatorSize * (0.5f + mids * 0.5f));

    // Highs indicator
    canvas.fillStyle(0.3f, 0.5f, 1.0f, 0.5f + highs * 0.5f);
    canvas.fillCircle(1080.0f, indicatorY, indicatorSize * (0.5f + highs * 0.5f));
}

VIVID_CHAIN(setup, update)
