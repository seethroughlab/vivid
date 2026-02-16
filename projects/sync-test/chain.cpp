// Sync Test: Metronome with Visual Flashes
// This demonstrates audio-video sync with sharp, percussive beats
// Any desync will be immediately obvious as the flash won't match the click
//
// Audio: Click track at 120 BPM (2 beats per second)
// Visual: White screen flash on every beat, fading to black
//
// If sync is broken, you'll see the flash before/after the click sound

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// Global state for beat timing
struct BeatState {
    float lastBeatTime = -1.0f;
    int beatCount = 0;
} g_beat;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================
    // Audio: Metronome click at 120 BPM
    // =========================================

    // High-pitched click (2kHz sine burst)
    auto& click = chain.add<Oscillator>("click");
    click.frequency = 2000.0f;
    click.volume = 0.0f;  // Will be modulated in update()
    click.waveform(Waveform::Sine);

    auto& out = chain.add<AudioOutput>("out");
    out.input("click");
    chain.audioOutput("out");

    // =========================================
    // Visuals: White noise with gain to create flashes
    // =========================================

    // Start with white noise (full spectrum static)
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 1000.0f;  // Very fine noise = looks like white when bright
    noise.speed = 0.0f;     // Static

    // Brightness to control flash effect
    auto& brightness = chain.add<Brightness>("brightness");
    brightness.input("noise");
    brightness.brightness = -1.0f;  // Start black

    chain.output("brightness");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = ctx.time();

    // 120 BPM = 2 beats per second = 0.5s per beat
    const float beatInterval = 0.5f;
    const float clickDuration = 0.05f;  // 50ms click
    const float flashDuration = 0.12f;  // 120ms flash (slightly longer for visibility)

    // Check if we need to trigger a new beat
    float timeSinceLastBeat = time - g_beat.lastBeatTime;

    if (g_beat.lastBeatTime < 0.0f || timeSinceLastBeat >= beatInterval) {
        // Trigger new beat
        g_beat.lastBeatTime = time;
        g_beat.beatCount++;
    }

    // Audio: sharp click with exponential decay
    auto& click = chain.get<Oscillator>("click");
    if (timeSinceLastBeat < clickDuration) {
        float t = timeSinceLastBeat / clickDuration;
        click.volume = 0.6f * std::exp(-8.0f * t);
    } else {
        click.volume = 0.0f;
    }

    // Visual: white flash with exponential decay
    auto& brightness = chain.get<Brightness>("brightness");
    if (timeSinceLastBeat < flashDuration) {
        float t = timeSinceLastBeat / flashDuration;
        float level = std::exp(-5.0f * t);

        // Every 4th beat: extra bright (downbeat indicator)
        if (g_beat.beatCount % 4 == 1) {
            brightness.brightness = level * 1.5f;  // Brighter flash
        } else {
            brightness.brightness = level * 1.0f;   // Regular flash
        }
    } else {
        brightness.brightness = -1.0f;  // Black between beats
    }
}

VIVID_CHAIN(setup, update)
