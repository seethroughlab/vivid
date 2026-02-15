// DrumKit Example
// Demonstrates: MIDI-controlled drum kit with Sequencer
//
// A complete drum machine using Sequencer → DrumKit routing.
// The sequencer sends GM drum notes to trigger individual drums.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <vivid/frame_input.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- CLOCK -----
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Sixteenth);  // 16th note resolution

    // ----- SEQUENCER -----
    auto& seq = chain.add<Sequencer>("seq");
    seq.setTriggerSource("clock");
    seq.steps = 16;

    // ----- DRUM KIT -----
    auto& kit = chain.add<DrumKit>("drums");

    // Customize drum sounds
    kit.kick().pitch = 55.0f;
    kit.kick().decay = 0.4f;
    kit.snare().tone = 0.5f;
    kit.snare().noise = 0.8f;

    // Route sequencer to drum kit
    seq.setTarget("drums");

    // ----- PROGRAM A BASIC BEAT -----
    // Using GM drum note numbers

    // Kick on beats 1, 3 (steps 0, 8)
    seq.setStep(0, DrumKit::GM_KICK, 1.0f);
    seq.setStep(8, DrumKit::GM_KICK, 0.9f);

    // Snare on beats 2, 4 (steps 4, 12)
    seq.setStep(4, DrumKit::GM_SNARE, 0.95f);
    seq.setStep(12, DrumKit::GM_SNARE, 0.95f);

    // Closed hi-hat on every other 16th
    for (int i = 0; i < 16; i += 2) {
        float vel = (i % 4 == 0) ? 0.7f : 0.5f;  // Accent on beats
        seq.setStep(i, DrumKit::GM_CLOSED_HIHAT, vel);
    }

    // Open hi-hat accents
    seq.setStep(6, DrumKit::GM_OPEN_HIHAT, 0.6f);
    seq.setStep(14, DrumKit::GM_OPEN_HIHAT, 0.6f);

    // ----- EFFECTS -----
    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("drums");
    reverb.roomSize = 0.4f;
    reverb.damping = 0.6f;
    reverb.mix = 0.15f;

    // ----- AUDIO OUTPUT -----
    auto& out = chain.add<AudioOutput>("out");
    out.setInput("reverb");
    chain.audioOutput("out");

    // ----- VISUALS -----
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.1f, 0.08f, 0.12f, 1.0f);

    auto& waveform = chain.add<Shape>("waveform");
    waveform.type = ShapeType::Ellipse;
    waveform.position.set(0.5f, 0.5f);
    waveform.size.set(0.4f, 0.4f);
    waveform.color.set(1.0f, 0.5f, 0.3f, 0.9f);
    waveform.softness = 0.5f;

    auto& comp = chain.add<Composite>("comp");
    comp.inputA("bg");
    comp.inputB("waveform");
    comp.mode = BlendMode::Add;

    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    chain.process(ctx);

    auto& clock = chain.get<Clock>("clock");
    auto& seq = chain.get<Sequencer>("seq");
    auto& kit = chain.get<DrumKit>("drums");

    // ----- KEYBOARD CONTROLS -----
    // Tempo control
    if (ctx.key(static_cast<int>(Key::Up)).pressed) clock.bpm = std::min(200.0f, static_cast<float>(clock.bpm) + 5.0f);
    if (ctx.key(static_cast<int>(Key::Down)).pressed) clock.bpm = std::max(60.0f, static_cast<float>(clock.bpm) - 5.0f);

    // Manual drum triggers (for testing)
    if (ctx.key('K').pressed) kit.triggerDrum(DrumType::Kick, 1.0f);
    if (ctx.key('S').pressed) kit.triggerDrum(DrumType::Snare, 1.0f);
    if (ctx.key('H').pressed) kit.triggerDrum(DrumType::ClosedHiHat, 0.8f);
    if (ctx.key('O').pressed) kit.triggerDrum(DrumType::OpenHiHat, 0.8f);
    if (ctx.key('C').pressed) kit.triggerDrum(DrumType::Clap, 0.9f);

    // ----- STATUS DISPLAY -----
    auto& canvas = chain.get<Canvas>("canvas");
    auto& comp = chain.get<Composite>("comp");

    canvas.clear(0.0f, 0.0f, 0.0f, 0.0f);
    canvas.drawImage(comp, 0, 0, static_cast<float>(ctx.width()), static_cast<float>(ctx.height()));

    // Title
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("DrumKit Demo", 20, 30);

    // Status
    canvas.fillStyle(0.8f, 0.6f, 0.4f, 1.0f);
    char status[128];
    snprintf(status, sizeof(status), "BPM: %.0f  |  Step: %d / %d",
        static_cast<float>(clock.bpm),
        seq.currentStep() + 1,
        static_cast<int>(seq.steps));
    canvas.fillText(status, 20, 55);

    // Pattern visualization
    canvas.fillStyle(0.5f, 0.5f, 0.6f, 1.0f);
    float stepW = 30.0f;
    float startX = 20.0f;
    float y = 90.0f;

    for (int i = 0; i < 16; ++i) {
        float x = startX + i * stepW;
        bool active = seq.getStep(i);
        bool current = (seq.currentStep() == i);

        // Step box
        uint8_t r = active ? 200 : 60;
        uint8_t g = active ? 120 : 60;
        uint8_t b = active ? 80 : 60;
        if (current) { r = 255; g = 200; b = 100; }

        canvas.fillStyle(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
        canvas.fillRect(x, y, stepW - 4, 20);
    }

    // Instructions
    canvas.fillStyle(0.5f, 0.5f, 0.5f, 1.0f);
    int iy = ctx.height() - 80;
    canvas.fillText("Up/Down = Tempo", 20, static_cast<float>(iy)); iy += 20;
    canvas.fillText("K=Kick  S=Snare  H=HiHat  O=OpenHH  C=Clap", 20, static_cast<float>(iy)); iy += 20;

    chain.output("canvas");
}

VIVID_CHAIN(setup, update)
