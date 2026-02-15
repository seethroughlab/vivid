// Arpeggiator Example
// Demonstrates: Arpeggiator with MIDI input, clock sync, different modes
//
// Connect a MIDI controller and hold chords - they'll be arpeggiated!
// Use the number keys to change arpeggio mode:
//   1 = Up, 2 = Down, 3 = Up/Down, 4 = Random, 5 = Order

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/midi/midi.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <vivid/frame_input.h>

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

    // ----- CLOCK -----
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Sixteenth);  // 16th note arpeggios

    // ----- ARPEGGIATOR -----
    auto& arp = chain.add<Arpeggiator>("arp");
    arp.setTriggerSource("clock");
    arp.mode(ArpMode::Up);
    arp.octaves = 2;  // Span 2 octaves
    arp.gate = 0.8f;  // 80% note length

    // ----- SYNTHESIZER -----
    auto& synth = chain.add<PolySynth>("synth");
    synth.waveform(Waveform::Saw);
    synth.maxVoices = 4;  // Arp plays one note at a time, but allow some overlap
    synth.attack = 0.005f;
    synth.decay = 0.1f;
    synth.sustain = 0.6f;
    synth.release = 0.15f;

    // ----- EFFECTS -----
    auto& delay = chain.add<Delay>("delay");
    delay.input("synth");
    delay.delayTime = 375.0f;  // Dotted 8th at 120 BPM (in ms)
    delay.feedback = 0.4f;
    delay.mix = 0.25f;

    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("delay");
    reverb.roomSize = 0.7f;
    reverb.damping = 0.4f;
    reverb.mix = 0.3f;

    // ----- AUDIO OUTPUT -----
    auto& out = chain.add<AudioOutput>("out");
    out.setInput("reverb");
    chain.audioOutput("out");

    // =========================================================
    // MIDI ROUTING CHAIN:
    // MidiIn -> Arpeggiator -> PolySynth
    // =========================================================

    // MIDI controller notes go to arpeggiator (input)
    midiIn.setTarget("arp");

    // Arpeggiator outputs to synth
    arp.setTarget("synth");

    // ----- VISUALS -----
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.08f, 0.05f, 0.12f, 1.0f);

    // Visual shape
    auto& waveform = chain.add<Shape>("waveform");
    waveform.type = ShapeType::Ellipse;
    waveform.position.set(0.5f, 0.5f);
    waveform.size.set(0.4f, 0.4f);
    waveform.color.set(0.8f, 0.4f, 1.0f, 0.8f);
    waveform.softness = 0.5f;

    auto& comp = chain.add<Composite>("comp");
    comp.inputA("bg");
    comp.inputB("waveform");
    comp.mode = BlendMode::Add;

    // Canvas for status text
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Process audio chain
    chain.process(ctx);

    // ----- KEYBOARD CONTROLS -----
    auto& arp = chain.get<Arpeggiator>("arp");
    auto& clock = chain.get<Clock>("clock");

    // Change arp mode with number keys
    if (ctx.key('1').pressed) arp.mode(ArpMode::Up);
    if (ctx.key('2').pressed) arp.mode(ArpMode::Down);
    if (ctx.key('3').pressed) arp.mode(ArpMode::UpDown);
    if (ctx.key('4').pressed) arp.mode(ArpMode::Random);
    if (ctx.key('5').pressed) arp.mode(ArpMode::Order);

    // Tempo control with up/down arrows
    if (ctx.key(static_cast<int>(Key::Up)).pressed) clock.bpm = std::min(200.0f, static_cast<float>(clock.bpm) + 10.0f);
    if (ctx.key(static_cast<int>(Key::Down)).pressed) clock.bpm = std::max(60.0f, static_cast<float>(clock.bpm) - 10.0f);

    // Octave control with left/right arrows
    if (ctx.key(static_cast<int>(Key::Left)).pressed) arp.octaves = std::max(1, static_cast<int>(arp.octaves) - 1);
    if (ctx.key(static_cast<int>(Key::Right)).pressed) arp.octaves = std::min(4, static_cast<int>(arp.octaves) + 1);

    // ----- STATUS DISPLAY -----
    auto& canvas = chain.get<Canvas>("canvas");
    auto& comp = chain.get<Composite>("comp");
    auto& midiIn = chain.get<MidiIn>("midi");
    auto& synth = chain.get<PolySynth>("synth");

    canvas.clear(0.0f, 0.0f, 0.0f, 0.0f);
    canvas.drawImage(comp, 0, 0, static_cast<float>(ctx.width()), static_cast<float>(ctx.height()));

    // Title
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Arpeggiator Demo", 20, 30);

    // MIDI status
    canvas.fillStyle(0.7f, 0.7f, 0.7f, 1.0f);
    char status[128];
    snprintf(status, sizeof(status), "MIDI: %s",
        midiIn.isOpen() ? midiIn.portName().c_str() : "No device - connect a MIDI controller");
    canvas.fillText(status, 20, 55);

    // Arp status
    canvas.fillStyle(0.8f, 0.5f, 1.0f, 1.0f);
    const char* modeNames[] = {"Up", "Down", "Up/Down", "Random", "Order"};
    snprintf(status, sizeof(status), "Mode: %s  |  Octaves: %d  |  Held notes: %d",
        modeNames[static_cast<int>(arp.mode())],
        static_cast<int>(arp.octaves),
        arp.heldNoteCount());
    canvas.fillText(status, 20, 80);

    // Clock status
    canvas.fillStyle(0.5f, 0.8f, 0.5f, 1.0f);
    snprintf(status, sizeof(status), "BPM: %.0f  |  Current note: %d",
        static_cast<float>(clock.bpm),
        arp.currentNote());
    canvas.fillText(status, 20, 105);

    // Active voices
    snprintf(status, sizeof(status), "Synth voices: %d / %d",
        synth.activeVoiceCount(), static_cast<int>(synth.maxVoices));
    canvas.fillText(status, 20, 130);

    // Instructions
    canvas.fillStyle(0.5f, 0.5f, 0.6f, 1.0f);
    int y = ctx.height() - 100;
    canvas.fillText("Hold notes on your MIDI controller to hear arpeggios!", 20, static_cast<float>(y)); y += 20;
    canvas.fillText("Keys: 1-5 = Mode (Up/Down/UpDown/Random/Order)", 20, static_cast<float>(y)); y += 20;
    canvas.fillText("Up/Down arrows = Tempo  |  Left/Right = Octaves", 20, static_cast<float>(y)); y += 20;

    chain.output("canvas");
}

VIVID_CHAIN(setup, update)
