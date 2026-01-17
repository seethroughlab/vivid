// MIDI Clock Sync Example
// Demonstrates: Clock with MIDI sync (both receive and send)
//
// This example shows bidirectional MIDI clock:
// - Receive: Sync Vivid's clock to external MIDI clock (from DAW or hardware)
// - Send: Vivid sends MIDI clock to control external gear

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/midi/midi.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::midi;
using namespace vivid::audio;

// Mode: true = receive clock from external, false = send clock to external
static bool g_receiveMode = false;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- MIDI PORTS -----
    auto& midiIn = chain.add<MidiIn>("midiIn");
    auto& midiOut = chain.add<MidiOut>("midiOut");

    // Open first available ports
    auto inPorts = MidiIn::listPorts();
    if (!inPorts.empty()) {
        midiIn.openPort(0);
    }

    auto outPorts = MidiOut::listPorts();
    if (!outPorts.empty()) {
        // Prefer IAC Driver on macOS for loopback testing
        bool opened = false;
        for (size_t i = 0; i < outPorts.size(); i++) {
            if (outPorts[i].find("IAC") != std::string::npos) {
                midiOut.openPort(static_cast<unsigned int>(i));
                opened = true;
                break;
            }
        }
        if (!opened) {
            midiOut.openPort(0);
        }
    }

    // ----- CLOCK -----
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Quarter);
    clock.start();

    // =========================================================
    // MIDI CLOCK SYNC SETUP
    // =========================================================

    if (g_receiveMode) {
        // RECEIVE MODE: Sync to external MIDI clock
        // The clock will derive BPM from incoming MIDI clock messages (24 PPQ)
        midiIn.setClockTarget("clock");
        clock.setMidiClockSync(true);
    } else {
        // SEND MODE: Vivid is the master, send clock to external gear
        // Clock sends 24 PPQ MIDI clock + Start/Stop/Continue messages
        clock.setMidiClockOutput("midiOut");
    }

    // =========================================================

    // ----- DRUMS (triggered by clock) -----
    auto& kick = chain.add<Kick>("kick");
    kick.setTriggerSource("clock");
    kick.pitch = 55.0f;
    kick.decay = 0.3f;

    auto& mixer = chain.add<Mixer>("mixer");
    mixer.addInput("kick", 0.8f);

    auto& out = chain.add<AudioOutput>("out");
    out.input("mixer");
    chain.audioOutput("out");

    // ----- VISUALS -----
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.05f, 0.05f, 0.08f, 1.0f);

    // Beat indicator
    auto& beatDot = chain.add<Shape>("beatDot");
    beatDot.type = ShapeType::Ellipse;
    beatDot.size.set(0.15f, 0.15f);
    beatDot.color.set(0.2f, 0.2f, 0.3f, 1.0f);

    auto& comp = chain.add<Composite>("comp");
    comp.inputA("bg");
    comp.inputB("beatDot");
    comp.mode = BlendMode::Add;

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("comp");
    bloom.threshold = 0.3f;
    bloom.intensity = 2.0f;

    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& clock = chain.get<Clock>("clock");
    auto& midiIn = chain.get<MidiIn>("midiIn");
    auto& midiOut = chain.get<MidiOut>("midiOut");
    auto& beatDot = chain.get<Shape>("beatDot");

    // Toggle mode with spacebar
    if (ctx.keyPressed(' ')) {
        g_receiveMode = !g_receiveMode;

        if (g_receiveMode) {
            midiIn.setClockTarget("clock");
            clock.setMidiClockSync(true);
            clock.clearMidiClockOutput();
        } else {
            midiIn.clearClockTarget();
            clock.setMidiClockSync(false);
            clock.setMidiClockOutput("midiOut");
        }
    }

    // Adjust BPM with mouse X (only in send mode)
    if (!g_receiveMode) {
        float mouseX = ctx.mouseNorm().x;
        clock.bpm = 60.0f + mouseX * 140.0f;  // 60-200 BPM
    }

    // Beat visualization
    bool triggered = clock.triggeredPeek();
    uint32_t beat = clock.beat();

    if (triggered) {
        // Flash on beat
        float brightness = (beat == 0) ? 1.0f : 0.7f;  // Brighter on downbeat
        beatDot.color.set(
            0.3f + brightness * 0.5f,
            0.5f + brightness * 0.4f,
            0.8f + brightness * 0.2f,
            1.0f
        );
        beatDot.size.set(0.18f, 0.18f);
    } else {
        // Fade out
        float r = beatDot.color.r() * 0.92f;
        float g = beatDot.color.g() * 0.92f;
        float b = beatDot.color.b() * 0.92f;
        beatDot.color.set(std::max(r, 0.2f), std::max(g, 0.2f), std::max(b, 0.3f), 1.0f);
        beatDot.size.set(0.15f, 0.15f);
    }

    // Position based on beat (4 positions)
    float x = -0.3f + beat * 0.2f;
    beatDot.position.set(x, 0.0f);

    chain.process(ctx);

    // ----- STATUS DISPLAY -----
    auto& canvas = chain.get<Canvas>("canvas");
    auto& bloom = chain.get<Bloom>("bloom");

    canvas.clear(0.0f, 0.0f, 0.0f, 0.0f);
    canvas.drawImage(bloom, 0, 0, static_cast<float>(ctx.width()), static_cast<float>(ctx.height()));

    // Title
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("MIDI Clock Sync", 20, 30);

    // Mode indicator
    canvas.fillStyle(0.8f, 0.8f, 0.2f, 1.0f);
    canvas.fillText(g_receiveMode ? "[RECEIVE MODE] Syncing to external clock" :
                                    "[SEND MODE] Sending clock to external gear", 20, 55);

    // MIDI status
    canvas.fillStyle(0.6f, 0.6f, 0.7f, 1.0f);
    char status[128];
    snprintf(status, sizeof(status), "MIDI In: %s",
        midiIn.isOpen() ? midiIn.portName().c_str() : "Not connected");
    canvas.fillText(status, 20, 85);

    snprintf(status, sizeof(status), "MIDI Out: %s",
        midiOut.isOpen() ? midiOut.portName().c_str() : "Not connected");
    canvas.fillText(status, 20, 105);

    // Clock status
    canvas.fillStyle(0.5f, 0.8f, 0.5f, 1.0f);
    snprintf(status, sizeof(status), "BPM: %.1f  |  Bar: %u  |  Beat: %u",
        static_cast<float>(clock.bpm),
        clock.bar() + 1,
        clock.beat() + 1);
    canvas.fillText(status, 20, 135);

    // Instructions
    canvas.fillStyle(0.4f, 0.4f, 0.5f, 1.0f);
    canvas.fillText("Press SPACE to toggle receive/send mode", 20, static_cast<float>(ctx.height() - 60));
    canvas.fillText(g_receiveMode ?
        "Connect DAW or drum machine sending MIDI clock" :
        "Move mouse X to adjust BPM (clock sent to MIDI out)", 20, static_cast<float>(ctx.height() - 40));

    chain.output("canvas");
}

VIVID_CHAIN(setup, update)
