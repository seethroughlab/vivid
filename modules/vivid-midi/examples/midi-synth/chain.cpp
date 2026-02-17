// Native MIDI Synth Example
// Demonstrates: MidiIn routing, CC mapping, pitch bend, velocity
//
// Connect a MIDI controller and play - no manual event handling needed!
// This example shows the new native MIDI routing API.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/midi/midi.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::midi;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- MIDI INPUT -----
    auto& midiIn = chain.add<MidiIn>("midi");

    // Open first available MIDI port
    auto ports = MidiIn::listPorts();
    if (!ports.empty()) {
        midiIn.openPort(0);
    }

    // ----- POLYPHONIC SYNTHESIZER -----
    auto& synth = chain.add<PolySynth>("synth");
    synth.waveform(Waveform::Saw);
    synth.maxVoices = 8;
    synth.attack = 0.01f;
    synth.decay = 0.1f;
    synth.sustain = 0.7f;
    synth.release = 0.3f;

    // ----- EFFECTS -----
    auto& delay = chain.add<Delay>("delay");
    delay.input("synth");
    delay.delayTime = 0.3f;
    delay.feedback = 0.3f;
    delay.mix = 0.2f;

    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("delay");
    reverb.roomSize = 0.6f;
    reverb.damping = 0.5f;
    reverb.mix = 0.25f;

    // ----- AUDIO OUTPUT -----
    auto& out = chain.add<AudioOutput>("out");
    out.input("reverb");
    chain.audioOutput("out");

    // =========================================================
    // NATIVE MIDI ROUTING - This is the new API!
    // =========================================================

    // Route all MIDI notes directly to synth - no manual event handling!
    // Notes, velocity, and pitch bend are all handled automatically.
    midiIn.setTarget("synth");

    // Map MIDI CCs to synth parameters:
    // CC91 (reverb send) -> reverb mix (0.0 - 0.6)
    midiIn.mapCC(91, "reverb", "mix", 0.0f, 0.6f);

    // CC93 (chorus send) -> delay mix (0.0 - 0.5)
    midiIn.mapCC(93, "delay", "mix", 0.0f, 0.5f);

    // CC7 (volume) -> synth volume (0.0 - 1.0)
    midiIn.mapCC(7, "synth", "volume", 0.0f, 1.0f);

    // =========================================================

    // ----- VISUALS -----
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.05f, 0.08f, 0.12f, 1.0f);

    // Canvas for status text
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Process audio chain
    chain.process(ctx);

    // ----- STATUS DISPLAY -----
    auto& canvas = chain.get<Canvas>("canvas");
    auto& bg = chain.get<SolidColor>("bg");
    auto& midiIn = chain.get<MidiIn>("midi");
    auto& synth = chain.get<PolySynth>("synth");
    auto& reverb = chain.get<Reverb>("reverb");
    auto& delay = chain.get<Delay>("delay");

    canvas.clear(0.0f, 0.0f, 0.0f, 0.0f);
    canvas.drawImage(bg, 0, 0, static_cast<float>(ctx.width()), static_cast<float>(ctx.height()));

    // Title
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Native MIDI Synth", 20, 30);

    // MIDI status
    canvas.fillStyle(0.7f, 0.7f, 0.7f, 1.0f);
    char status[128];
    snprintf(status, sizeof(status), "MIDI: %s",
        midiIn.isOpen() ? midiIn.portName().c_str() : "No device - connect a MIDI controller");
    canvas.fillText(status, 20, 55);

    // Active voices
    canvas.fillStyle(0.5f, 0.8f, 0.5f, 1.0f);
    snprintf(status, sizeof(status), "Active voices: %d / %d",
        synth.activeVoiceCount(), static_cast<int>(synth.maxVoices));
    canvas.fillText(status, 20, 80);

    // Parameter values (show CC-mapped parameters)
    canvas.fillStyle(0.6f, 0.6f, 0.7f, 1.0f);
    int y = 120;

    snprintf(status, sizeof(status), "Reverb Mix (CC91): %.2f",
        static_cast<float>(reverb.mix));
    canvas.fillText(status, 20, static_cast<float>(y)); y += 20;

    snprintf(status, sizeof(status), "Delay Mix (CC93): %.2f",
        static_cast<float>(delay.mix));
    canvas.fillText(status, 20, static_cast<float>(y)); y += 20;

    // Instructions
    canvas.fillStyle(0.4f, 0.4f, 0.5f, 1.0f);
    canvas.fillText("Play notes - velocity and pitch bend supported!", 20, static_cast<float>(ctx.height() - 60));
    canvas.fillText("Move mod wheel (CC1) to control filter cutoff", 20, static_cast<float>(ctx.height() - 40));
    canvas.fillText("No manual MIDI polling needed - setTarget() handles it all", 20, static_cast<float>(ctx.height() - 20));
}

VIVID_CHAIN(setup, update)
