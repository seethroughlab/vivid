// MIDI Input Example
// Demonstrates: MidiIn, MidiOut, Trigger
//
// Shows MIDI device connection and event handling with visual feedback.
// Uses Trigger operators to convert MIDI events into smooth envelopes.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/midi/midi.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::midi;

// Store MIDI state globally for visualization
static float g_velocityDisplay = 0.0f;
static float g_ccValues[8] = {0};
static bool g_noteActive = false;
static int g_lastNote = 60;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // MidiIn - receive from hardware controllers
    auto& midiIn = chain.add<MidiIn>("midiIn");
    midiIn.channel = 0;  // Omni (receive all channels)

    // Try to open the first available MIDI input
    auto ports = MidiIn::listPorts();
    if (!ports.empty()) {
        midiIn.openPort(0);
    }

    // MidiOut - send to external synths/DAWs
    auto& midiOut = chain.add<MidiOut>("midiOut");

    // Try to open the first available MIDI output
    auto outPorts = MidiOut::listPorts();
    if (!outPorts.empty()) {
        midiOut.openPort(0);
    }

    // Trigger operator for note visualization (replaces manual decay)
    auto& noteTrigger = chain.add<Trigger>("noteTrigger");
    noteTrigger.decay = 0.92f;  // Smooth decay for visual feedback

    // Visual feedback - background color responds to notes
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.1f, 0.1f, 0.15f, 1.0f);

    // Shape for note visualization
    auto& noteShape = chain.add<Shape>("noteShape");
    noteShape.type = ShapeType::Ellipse;
    noteShape.size.set(0.15f, 0.15f);
    noteShape.color.set(0.2f, 0.6f, 1.0f, 0.0f);

    // Canvas for UI
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& midiIn = chain.get<MidiIn>("midiIn");
    auto& midiOut = chain.get<MidiOut>("midiOut");
    auto& noteTrigger = chain.get<Trigger>("noteTrigger");

    // Process incoming MIDI events
    for (const auto& e : midiIn.events()) {
        switch (e.type) {
            case MidiEventType::NoteOn:
                g_noteActive = true;
                g_lastNote = e.note;
                g_velocityDisplay = e.velocity / 127.0f;

                // Fire trigger with velocity - handles decay automatically
                noteTrigger.fire(g_velocityDisplay);

                // Echo note to output (if open)
                if (midiOut.isOpen()) {
                    midiOut.noteOn(0, e.note, e.velocity / 127.0f);
                }
                break;

            case MidiEventType::NoteOff:
                g_noteActive = false;
                if (midiOut.isOpen()) {
                    midiOut.noteOff(0, e.note);
                }
                break;

            case MidiEventType::ControlChange:
                if (e.cc < 8) {
                    g_ccValues[e.cc] = e.value / 127.0f;
                }
                break;

            default:
                break;
        }
    }

    // Get trigger value (automatically decays each frame)
    float noteDisplay = noteTrigger.value();

    // Update visual feedback
    auto& bg = chain.get<SolidColor>("bg");
    float brightness = 0.1f + noteDisplay * 0.3f;
    bg.color.set(brightness * 0.8f, brightness * 0.8f, brightness, 1.0f);

    auto& noteShape = chain.get<Shape>("noteShape");
    float shapeSize = 0.1f + g_velocityDisplay * 0.1f;
    noteShape.size.set(shapeSize, shapeSize);
    noteShape.color.set(0.2f, 0.6f, 1.0f, noteDisplay * 0.8f);
    // Position based on note (C4 = center)
    float noteX = (g_lastNote - 60) / 24.0f;  // -1 to 1 for 2 octaves
    noteShape.position.set(noteX * 0.5f, 0.0f);

    chain.process(ctx);

    // Draw UI
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.08f, 0.08f, 0.12f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();

    // Draw note visualization (use noteTrigger.active() for cleaner check)
    if (noteTrigger.active()) {
        canvas.drawImage(noteShape, 0, 0, static_cast<float>(w), static_cast<float>(h));
    }

    // Draw MIDI status
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("MIDI Input Example", 20, 30);

    canvas.fillStyle(0.7f, 0.7f, 0.7f, 1.0f);

    // Input port status
    char inStatus[128];
    snprintf(inStatus, sizeof(inStatus), "Input: %s",
        midiIn.isOpen() ? midiIn.portName().c_str() : "No MIDI device");
    canvas.fillText(inStatus, 20, 60);

    // Output port status
    char outStatus[128];
    snprintf(outStatus, sizeof(outStatus), "Output: %s",
        midiOut.isOpen() ? midiOut.portName().c_str() : "No MIDI device");
    canvas.fillText(outStatus, 20, 80);

    // Note activity
    float noteGray = g_noteActive ? 1.0f : 0.5f;
    canvas.fillStyle(noteGray, noteGray, noteGray, 1.0f);
    char noteStr[64];
    const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (g_lastNote / 12) - 1;
    int noteIndex = g_lastNote % 12;
    snprintf(noteStr, sizeof(noteStr), "Last Note: %s%d (vel: %.0f%%)",
        noteNames[noteIndex], octave, g_velocityDisplay * 100);
    canvas.fillText(noteStr, 20, 110);

    // CC bars
    canvas.fillStyle(0.5f, 0.5f, 0.5f, 1.0f);
    canvas.fillText("CC 0-7:", 20, 150);

    for (int i = 0; i < 8; i++) {
        int barX = 80 + i * 30;
        int barY = 135;
        int barH = 40;

        // Background
        canvas.fillStyle(0.2f, 0.2f, 0.25f, 1.0f);
        canvas.fillRect(static_cast<float>(barX), static_cast<float>(barY), 20.0f, static_cast<float>(barH));

        // Value bar
        float val = g_ccValues[i];
        canvas.fillStyle(0.3f, 0.7f, 0.4f, 1.0f);
        canvas.fillRect(static_cast<float>(barX), barY + barH * (1.0f - val), 20.0f, barH * val);

        // CC number
        canvas.fillStyle(0.5f, 0.5f, 0.5f, 1.0f);
        char ccLabel[8];
        snprintf(ccLabel, sizeof(ccLabel), "%d", i);
        canvas.fillText(ccLabel, static_cast<float>(barX + 4), static_cast<float>(barY + barH + 15));
    }

    // Instructions
    canvas.fillStyle(0.5f, 0.5f, 0.6f, 1.0f);
    canvas.fillText("Connect a MIDI controller to see input visualization", 20, static_cast<float>(h - 50));
    canvas.fillText("Notes are echoed to MIDI output (if available)", 20, static_cast<float>(h - 30));

    chain.output("canvas");
}

VIVID_CHAIN(setup, update)
