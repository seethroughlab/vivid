/**
 * Song Arrangement Example
 *
 * Demonstrates: Song, Sequencer, Clock
 *
 * Shows how to structure a composition with sections
 * (intro, verse, chorus) and sync visuals to musical structure.
 */

#include <vivid/vivid.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Master clock at 120 BPM
    chain.add<Clock>("clock");
    auto& clock = chain.get<Clock>("clock");
    clock.bpm = 120.0f;
    clock.beatsPerBar = 4;

    // Song structure
    chain.add<Song>("song");
    auto& song = chain.get<Song>("song");
    song.syncTo("clock");

    // Define sections (bar numbers)
    song.addSection("intro", 0, 8);      // Bars 0-7
    song.addSection("verse", 8, 24);     // Bars 8-23
    song.addSection("chorus", 24, 32);   // Bars 24-31
    song.addSection("verse2", 32, 48);   // Bars 32-47
    song.addSection("chorus2", 48, 56);  // Bars 48-55
    song.addSection("outro", 56, 64);    // Bars 56-63

    // Simple drum sequencer
    chain.add<Sequencer>("drums");
    auto& seq = chain.get<Sequencer>("drums");
    seq.syncTo("clock");
    seq.setSteps(16);  // 16-step pattern

    // Basic kick pattern: 1---1---1---1---
    seq.setStep(0, 0, 1.0f);   // Beat 1
    seq.setStep(0, 4, 1.0f);   // Beat 2
    seq.setStep(0, 8, 1.0f);   // Beat 3
    seq.setStep(0, 12, 1.0f);  // Beat 4

    // Kick drum
    chain.add<Kick>("kick");
    auto& kick = chain.get<Kick>("kick");
    kick.decay = 200.0f;
    kick.pitch = 50.0f;

    // Visual feedback - noise texture
    chain.add<Noise>("visual");
    auto& noise = chain.get<Noise>("visual");
    noise.scale = 4.0f;

    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& clock = chain.get<Clock>("clock");
    auto& song = chain.get<Song>("song");
    auto& seq = chain.get<Sequencer>("drums");
    auto& kick = chain.get<Kick>("kick");
    auto& noise = chain.get<Noise>("visual");

    // Trigger kick from sequencer
    if (seq.triggered(0)) {
        kick.trigger();
    }

    // Adjust visuals based on song section
    const std::string& section = song.section();
    float sectionProgress = song.sectionProgress();

    if (section == "intro" || section == "outro") {
        // Calm intro/outro
        noise.scale = 8.0f;
        noise.speed = 0.2f;
    }
    else if (section == "verse" || section == "verse2") {
        // Building verse
        noise.scale = 4.0f + sectionProgress * 2.0f;
        noise.speed = 0.5f;
    }
    else if (section == "chorus" || section == "chorus2") {
        // Intense chorus
        noise.scale = 2.0f;
        noise.speed = 1.0f + clock.beat() * 0.2f;
    }

    // Flash on new bar
    if (song.barJustStarted()) {
        noise.offset.set(t, t);
    }

    chain.process();
}

VIVID_CHAIN(setup, update)
