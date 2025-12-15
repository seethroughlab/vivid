// Audio Visualizer - Vivid Project
// FFT-driven reactive graphics

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio input (microphone or system audio)
    auto& mic = chain.add<AudioIn>("mic");

    // FFT analysis
    auto& fft = chain.add<FFT>("fft");
    fft.input(&mic);
    fft.smoothing = 0.8f;

    // Beat detection
    auto& beat = chain.add<BeatDetect>("beat");
    beat.input(&mic);

    // Noise generator driven by audio
    auto& noise = chain.add<Noise>("noise");
    noise.type(NoiseType::Simplex);
    noise.scale = 4.0f;
    noise.octaves = 3;

    // Feedback for trails
    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input(&noise);
    feedback.decay = 0.85f;
    feedback.mix = 0.4f;

    // Color ramp
    auto& ramp = chain.add<Ramp>("ramp");
    ramp.type(RampType::Radial);
    ramp.hueSpeed = 0.2f;

    // Combine
    auto& comp = chain.add<Composite>("comp");
    comp.inputA(&feedback);
    comp.inputB(&ramp);
    comp.mode(BlendMode::Screen);

    chain.output("comp");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    auto& fft = chain.get<FFT>("fft");
    auto& beat = chain.get<BeatDetect>("beat");

    // Get bass energy (low frequencies)
    float bass = fft.band(0) + fft.band(1);

    // Get mid/high energy
    float mids = fft.band(2) + fft.band(3);
    float highs = fft.band(4) + fft.band(5);

    // Noise reacts to bass
    auto& noise = chain.get<Noise>("noise");
    noise.scale = 3.0f + bass * 8.0f;
    noise.speed = 0.3f + mids * 2.0f;
    noise.offset.set(time * 0.2f, time * 0.1f, highs * 2.0f);

    // Feedback zoom on beat
    auto& feedback = chain.get<Feedback>("feedback");
    if (beat.detected()) {
        feedback.zoom = 1.02f;
    } else {
        feedback.zoom = 1.0f + bass * 0.01f;
    }
    feedback.rotate = std::sin(time * 0.5f) * 0.01f;

    // Hue shifts with energy
    chain.get<Ramp>("ramp").hueOffset = std::fmod(time * 0.1f + bass * 0.5f, 1.0f);
}

VIVID_CHAIN(setup, update)
