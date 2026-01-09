// Sampler Basics - Vivid Example
// Demonstrates: Sampler, SamplePlayer, MultiSampler - Sample playback
//
// NOTE: Requires sample files in assets/ folder.
// See CLAUDE.md for sample requirements.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// Demo mode
enum class SamplerDemo {
    Sampler,        // Chromatic sampler (like Simpler)
    SamplePlayer,   // Trigger samples from a bank
    MultiSampler    // Multi-zone with velocity layers
};
static SamplerDemo g_demo = SamplerDemo::Sampler;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- DEMO 1: SAMPLER -----
    // Single sample played chromatically (like Ableton Simpler)
    auto& sampler = chain.add<Sampler>("sampler");
    // Load a sample - adjust path to your sample file
    sampler.loadSample("assets/audio/piano_c4.wav");
    sampler.rootNote = 60;  // C4 is the original pitch
    sampler.attack = 0.01f;
    sampler.decay = 0.1f;
    sampler.sustain = 0.8f;
    sampler.release = 0.5f;
    sampler.volume = 0.8f;

    // ----- DEMO 2: SAMPLE PLAYER -----
    // Trigger samples from a bank (for drums, sound effects)
    auto& bank = chain.add<SampleBank>("bank");
    bank.folder("assets/audio/drums");  // Folder with WAV files

    auto& player = chain.add<SamplePlayer>("player");
    player.setBank("bank");
    player.setVoices(8);  // 8-voice polyphony
    player.volume = 0.8f;

    // ----- DEMO 3: MULTI SAMPLER -----
    // Multi-zone sampler with velocity layers (like Kontakt)
    auto& multi = chain.add<MultiSampler>("multi");
    // Load a preset file (JSON or .dspreset)
    multi.loadPreset("assets/sample_packs/Piano/preset.json");
    multi.attack = 0.01f;
    multi.release = 1.0f;
    multi.volume = 0.8f;

    // ----- MIXER -----
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.addInput(&sampler, 0.0f);
    mixer.addInput(&player, 0.0f);
    mixer.addInput(&multi, 0.0f);

    // ----- OUTPUT -----
    auto& output = chain.add<AudioOutput>("audio_out");
    output.input("mixer");

    // ----- VISUALS -----
    auto& bg = chain.add<Gradient>("bg");
    bg.mode = GradientMode::Radial;
    bg.colorA.set(0.15f, 0.1f, 0.12f, 1.0f);
    bg.colorB.set(0.03f, 0.02f, 0.04f, 1.0f);

    // Visual keyboard representation
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);

    auto& comp = chain.add<Composite>("comp");
    comp.inputA("bg");
    comp.inputB("canvas");
    comp.mode = BlendMode::Add;

    chain.output("comp");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Get operators
    auto& sampler = chain.get<Sampler>("sampler");
    auto& player = chain.get<SamplePlayer>("player");
    auto& multi = chain.get<MultiSampler>("multi");
    auto& mixer = chain.get<AudioMixer>("mixer");
    auto& canvas = chain.get<Canvas>("canvas");

    // ----- SELECT DEMO WITH KEYS -----
    if (ctx.keyPressed(Key::Key1)) g_demo = SamplerDemo::Sampler;
    if (ctx.keyPressed(Key::Key2)) g_demo = SamplerDemo::SamplePlayer;
    if (ctx.keyPressed(Key::Key3)) g_demo = SamplerDemo::MultiSampler;

    // Set mixer levels
    mixer.setLevel(0, g_demo == SamplerDemo::Sampler ? 0.8f : 0.0f);
    mixer.setLevel(1, g_demo == SamplerDemo::SamplePlayer ? 0.8f : 0.0f);
    mixer.setLevel(2, g_demo == SamplerDemo::MultiSampler ? 0.8f : 0.0f);

    // ----- KEYBOARD INPUT -----
    // Map computer keyboard to MIDI notes
    // Lower row: Z-M = C3-B3
    // Upper row: A-; = C4-B4
    // Top row: Q-P = C5-B5

    static const struct {
        Key key;
        int note;
    } keyMap[] = {
        // Lower octave (C3)
        {Key::Z, 48}, {Key::S, 49}, {Key::X, 50}, {Key::D, 51},
        {Key::C, 52}, {Key::V, 53}, {Key::G, 54}, {Key::B, 55},
        {Key::H, 56}, {Key::N, 57}, {Key::J, 58}, {Key::M, 59},
        // Middle octave (C4)
        {Key::Q, 60}, {Key::Key2, 61}, {Key::W, 62}, {Key::Key3, 63},
        {Key::E, 64}, {Key::R, 65}, {Key::Key5, 66}, {Key::T, 67},
        {Key::Key6, 68}, {Key::Y, 69}, {Key::Key7, 70}, {Key::U, 71},
        // Upper notes
        {Key::I, 72}, {Key::Key9, 73}, {Key::O, 74}, {Key::Key0, 75},
        {Key::P, 76}
    };

    // Velocity from mouse Y
    float velocity = 0.5f + ctx.mouseNorm().y * 0.5f;  // 0.5-1.0

    // Process key events
    for (const auto& km : keyMap) {
        if (ctx.keyPressed(km.key)) {
            switch (g_demo) {
                case SamplerDemo::Sampler:
                    sampler.noteOn(km.note, velocity);
                    break;
                case SamplerDemo::SamplePlayer:
                    // Map notes to sample indices (0-7 for drums)
                    player.trigger(km.note % 8, velocity);
                    break;
                case SamplerDemo::MultiSampler:
                    multi.noteOn(km.note, velocity);
                    break;
            }
        }
        if (ctx.keyReleased(km.key)) {
            switch (g_demo) {
                case SamplerDemo::Sampler:
                    sampler.noteOff(km.note);
                    break;
                case SamplerDemo::MultiSampler:
                    multi.noteOff(km.note);
                    break;
                case SamplerDemo::SamplePlayer:
                    // SamplePlayer doesn't need note off
                    break;
            }
        }
    }

    // ----- VISUAL KEYBOARD -----
    canvas.clear(0.0f, 0.0f, 0.0f, 0.0f);

    // Draw piano keys
    float keyWidth = 50.0f;
    float whiteHeight = 150.0f;
    float blackHeight = 100.0f;
    float startX = 200.0f;
    float startY = 500.0f;

    // White key pattern for one octave
    static const bool isBlack[] = {false, true, false, true, false, false, true, false, true, false, true, false};
    static const float blackOffset[] = {0, 0.7f, 0, 0.7f, 0, 0, 0.7f, 0, 0.7f, 0, 0.7f, 0};

    int whiteKeyIndex = 0;
    for (int octave = 0; octave < 2; octave++) {
        for (int note = 0; note < 12; note++) {
            int midiNote = 60 + octave * 12 + note;  // Starting from C4

            if (!isBlack[note]) {
                // White key
                float x = startX + whiteKeyIndex * keyWidth;
                float y = startY;

                // Check if this note is playing
                bool playing = false;
                switch (g_demo) {
                    case SamplerDemo::Sampler:
                        playing = sampler.isPlaying();  // Simplified
                        break;
                    case SamplerDemo::MultiSampler:
                        playing = multi.isPlaying();
                        break;
                    default:
                        break;
                }

                // Draw white key
                canvas.fill(playing ? Color(0.9f, 0.9f, 0.7f) : Color(0.95f, 0.95f, 0.95f));
                canvas.stroke(Color(0.3f, 0.3f, 0.3f));
                canvas.strokeWeight(2);
                canvas.rect(x, y, keyWidth - 2, whiteHeight);

                whiteKeyIndex++;
            }
        }
    }

    // Draw black keys on top
    whiteKeyIndex = 0;
    for (int octave = 0; octave < 2; octave++) {
        for (int note = 0; note < 12; note++) {
            if (!isBlack[note]) {
                whiteKeyIndex++;
            } else {
                // Black key
                float x = startX + (whiteKeyIndex - 1) * keyWidth + keyWidth * blackOffset[note];
                float y = startY;

                canvas.fill(Color(0.15f, 0.15f, 0.15f));
                canvas.noStroke();
                canvas.rect(x, y, keyWidth * 0.6f, blackHeight);
            }
        }
    }

    // Demo label
    canvas.fill(Color(1.0f, 1.0f, 1.0f));
    canvas.textSize(24);
    const char* demoNames[] = {"Sampler (Chromatic)", "SamplePlayer (Drums)", "MultiSampler (Zones)"};
    canvas.text(demoNames[static_cast<int>(g_demo)], 50, 50);

    // Instructions
    canvas.textSize(16);
    canvas.fill(Color(0.7f, 0.7f, 0.7f));
    canvas.text("Keys 1/2/3: Switch demo | QWERTY row: Play notes | Mouse Y: Velocity", 50, 80);

    // Voice count
    int voices = 0;
    switch (g_demo) {
        case SamplerDemo::Sampler:
            voices = sampler.activeVoiceCount();
            break;
        case SamplerDemo::SamplePlayer:
            voices = player.activeVoices();
            break;
        case SamplerDemo::MultiSampler:
            voices = multi.activeVoiceCount();
            break;
    }
    canvas.text("Active voices: " + std::to_string(voices), 50, 110);
}

VIVID_CHAIN(setup, update)
