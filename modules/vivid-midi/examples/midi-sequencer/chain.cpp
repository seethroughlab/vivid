// MIDI Sequencer - Vivid Example
// Demonstrates: MidiOut, Clock, Sequencer
//
// Send MIDI notes to external synthesizers

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/midi/midi.h>
#include <vivid/audio/audio.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::midi;
using namespace vivid::audio;

// Note sequence (C minor pentatonic)
static const int g_notes[] = {48, 51, 53, 55, 58, 60, 63, 65};  // C3 minor pentatonic
static int g_currentNote = 0;
static float g_noteVelocity = 0.8f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- MIDI OUTPUT -----
    auto& midiOut = chain.add<MidiOut>("midiOut");
    
    // Try to open a MIDI port
    auto ports = MidiOut::listPorts();
    if (!ports.empty()) {
        // On macOS, try IAC Driver first
        bool opened = false;
        for (size_t i = 0; i < ports.size(); i++) {
            if (ports[i].find("IAC") != std::string::npos) {
                midiOut.openPort(static_cast<unsigned int>(i));
                opened = true;
                break;
            }
        }
        if (!opened) {
            midiOut.openPort(0);  // Use first available port
        }
    }

    // ----- CLOCK -----
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Eighth);  // 8th notes
    clock.start();

    // ----- SEQUENCER -----
    auto& seq = chain.add<Sequencer>("seq");
    seq.setTriggerSource("clock");  // Advances on audio thread
    seq.steps = 8;
    // Pattern: play on steps 0, 2, 3, 5, 7
    seq.setStep(0, true, 1.0f);
    seq.setStep(2, true, 0.7f);
    seq.setStep(3, true, 0.8f);
    seq.setStep(5, true, 0.6f);
    seq.setStep(7, true, 0.9f);

    // ----- VISUALS -----
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.02f, 0.02f, 0.05f, 1.0f);

    // Piano key visualization (8 keys)
    for (int i = 0; i < 8; i++) {
        char name[32];
        snprintf(name, sizeof(name), "key_%d", i);
        auto& key = chain.add<Shape>(name);
        key.type = ShapeType::Rectangle;
        key.size.set(0.08f, 0.25f);
        key.position.set(-0.35f + i * 0.1f, 0.0f);
        key.color.set(0.2f, 0.2f, 0.3f, 1.0f);
        key.softness = 0.01f;
    }

    // Composite all keys
    auto& comp = chain.add<Composite>("comp");
    comp.inputA("bg");
    comp.inputB("key_0");
    comp.mode = BlendMode::Add;

    for (int i = 1; i < 8; i++) {
        char prevName[32], currName[32], compName[32];
        snprintf(prevName, sizeof(prevName), i == 1 ? "comp" : "comp_%d", i - 1);
        snprintf(currName, sizeof(currName), "key_%d", i);
        snprintf(compName, sizeof(compName), "comp_%d", i);
        
        auto& c = chain.add<Composite>(compName);
        c.inputA(prevName);
        c.inputB(currName);
        c.mode = BlendMode::Add;
    }

    // Bloom
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("comp_7");
    bloom.threshold = 0.3f;
    bloom.intensity = 1.5f;
    bloom.radius = 15.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& clock = chain.get<Clock>("clock");
    auto& seq = chain.get<Sequencer>("seq");
    auto& midiOut = chain.get<MidiOut>("midiOut");

    // Mouse controls
    float mouseX = ctx.mouseNorm().x;
    float mouseY = ctx.mouseNorm().y;

    // X: BPM (60-180)
    clock.bpm = 60.0f + mouseX * 120.0f;

    // Y: Velocity (0.3-1.0)
    g_noteVelocity = 0.3f + mouseY * 0.7f;

    // Process MIDI output based on sequencer state
    // NOTE: Sequencer advances automatically via setTriggerSource("clock")
    static int lastNote = -1;

    // Check if sequencer triggered (on main thread, checking audio thread state)
    if (seq.triggered()) {
        // Turn off previous note
        if (lastNote >= 0 && midiOut.isOpen()) {
            midiOut.noteOff(0, static_cast<uint8_t>(lastNote));
        }

        // Get note from sequence
        int step = seq.currentStep();
        int note = g_notes[step % 8];
        float vel = seq.currentVelocity() * g_noteVelocity;

        // Send MIDI note
        if (midiOut.isOpen()) {
            midiOut.noteOn(0, static_cast<uint8_t>(note), vel);
        }
        lastNote = note;
        g_currentNote = step;
    }

    // Visual feedback - highlight active key
    for (int i = 0; i < 8; i++) {
        char name[32];
        snprintf(name, sizeof(name), "key_%d", i);
        auto& key = chain.get<Shape>(name);

        if (i == g_currentNote && seq.triggered()) {
            // Active key - bright color based on note
            float hue = static_cast<float>(i) / 8.0f;
            key.color.set(
                0.8f + 0.2f * std::sin(hue * 6.28f),
                0.8f + 0.2f * std::sin(hue * 6.28f + 2.09f),
                0.8f + 0.2f * std::sin(hue * 6.28f + 4.19f),
                1.0f
            );
            key.size.set(0.09f, 0.28f);  // Slightly larger when active
        } else {
            // Inactive key - fade back
            float r = key.color.r();
            float g = key.color.g();
            float b = key.color.b();
            key.color.set(r * 0.95f, g * 0.95f, b * 0.95f, 1.0f);
            key.size.set(0.08f, 0.25f);
            
            // Clamp to minimum
            if (r < 0.2f) key.color.set(0.2f, 0.2f, 0.3f, 1.0f);
        }
    }
}

VIVID_CHAIN(setup, update)
