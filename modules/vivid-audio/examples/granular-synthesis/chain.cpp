/**
 * Granular Synthesis Example
 *
 * Demonstrates: Granular, AudioFile
 *
 * Creates textural soundscapes from audio files using
 * grain-based synthesis with position and pitch spray.
 */

#include <vivid/vivid.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Load an audio file for granular processing
    chain.add<AudioFile>("source");
    auto& source = chain.get<AudioFile>("source");
    source.setFile("assets/audio/texture.wav");  // Any audio file

    // Granular synthesizer
    chain.add<Granular>("clouds");
    auto& grain = chain.get<Granular>("clouds");
    grain.grainSize = 80.0f;       // 80ms grains
    grain.density = 15.0f;         // 15 grains per second
    grain.position = 0.5f;         // Start at middle of sample
    grain.positionSpray = 0.1f;    // Random position variation
    grain.pitch = 1.0f;            // Normal pitch
    grain.pitchSpray = 0.05f;      // Slight pitch randomization
    grain.panSpray = 0.3f;         // Stereo spread
    grain.volume = 0.6f;
    grain.setWindow(GrainWindow::Hann);

    // Add reverb for atmosphere
    chain.add<Reverb>("verb");
    auto& verb = chain.get<Reverb>("verb");
    verb.input("clouds");
    verb.roomSize = 0.8f;
    verb.damping = 0.3f;
    verb.mix = 0.4f;

    chain.output("verb");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& grain = chain.get<Granular>("clouds");

    // Slowly scan through the sample
    grain.position = 0.3f + 0.4f * (0.5f + 0.5f * std::sin(t * 0.1f));

    // Modulate grain size for evolving texture
    grain.grainSize = 60.0f + 40.0f * std::sin(t * 0.3f);

    // Subtle pitch drift
    grain.pitch = 1.0f + 0.1f * std::sin(t * 0.2f);

    chain.process(ctx);
}

// Widescreen for waveform visualization
static vivid::ChainConfig config{
    .windowWidth = 1280,
    .windowHeight = 800,
    .resizable = true
};
VIVID_CHAIN_CONFIG(setup, update, config)
