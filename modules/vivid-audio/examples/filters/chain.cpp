/**
 * Filters Example
 *
 * Demonstrates: AudioFilter, LadderFilter, CombFilter
 *
 * Shows different filter types for shaping audio:
 * - AudioFilter: Standard biquad (LP/HP/BP/Notch)
 * - LadderFilter: Moog-style 4-pole with resonance
 * - CombFilter: Metallic/resonant textures
 */

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Noise source for filter demos
    auto& noise = chain.add<NoiseGen>("noise");
    noise.setColor(NoiseColor::White);
    noise.volume = 0.6f;

    // Oscillator source for ladder filter demo
    auto& osc = chain.add<Oscillator>("osc");
    osc.waveform(Waveform::Saw);
    osc.frequency = 100.0f;
    osc.volume = 0.5f;

    // Standard biquad filter - multiple modes available
    auto& biquad = chain.add<AudioFilter>("biquad");
    biquad.setInputByName(0, "noise");
    biquad.setType(FilterType::Lowpass);  // LP, HP, BP, Notch, Shelf, Peak
    biquad.cutoff = 800.0f;
    biquad.resonance = 3.0f;  // Q factor

    // Moog-style ladder filter (24dB/oct)
    auto& ladder = chain.add<LadderFilter>("ladder");
    ladder.input("osc");
    ladder.cutoff = 1200.0f;
    ladder.resonance = 0.7f;  // Self-oscillates at 1.0
    ladder.drive = 1.5f;      // Analog saturation

    // Comb filter for metallic/resonant textures
    auto& comb = chain.add<CombFilter>("comb");
    comb.input("noise");
    comb.setType(CombType::FeedBack);  // FeedForward, FeedBack, AllPass
    comb.frequency = 200.0f;   // Creates pitched resonance
    comb.feedback = 0.9f;      // Higher = longer decay
    comb.damping = 0.4f;       // HF damping (string character)

    // Mix filters together
    auto& mix = chain.add<AudioMixer>("mix");
    mix.setInput(0, "biquad");
    mix.setGain(0, 0.3f);
    mix.setInput(1, "ladder");
    mix.setGain(1, 0.4f);
    mix.setInput(2, "comb");
    mix.setGain(2, 0.3f);

    // Visual output
    auto& visual = chain.add<Noise>("visual");
    visual.scale = 4.0f;

    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Modulate ladder filter cutoff (classic filter sweep)
    auto& ladder = chain.get<LadderFilter>("ladder");
    ladder.cutoff = 300.0f + 1500.0f * (0.5f + 0.5f * std::sin(t * 0.3f));

    // Modulate biquad cutoff
    auto& biquad = chain.get<AudioFilter>("biquad");
    biquad.cutoff = 400.0f + 800.0f * (0.5f + 0.5f * std::sin(t * 0.5f + 1.0f));

    // Modulate comb frequency for pitch bend effect
    auto& comb = chain.get<CombFilter>("comb");
    comb.frequency = 150.0f + 100.0f * std::sin(t * 0.2f);

    auto& visual = chain.get<Noise>("visual");
    visual.scale = 3.0f + std::sin(t) * 1.0f;

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
