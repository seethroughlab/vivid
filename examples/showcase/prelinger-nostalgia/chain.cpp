// Prelinger Nostalgia - Boards of Canada inspired audio-visual piece
// Combines polyphonic synthesis, tape warmth, and vintage film aesthetics
//
// Features all advanced audio operators:
// - Song: Section-based composition
// - PolySynth: Warm chord pads
// - WavetableSynth: Evolving lead
// - FMSynth: Bell textures
// - Granular: Atmospheric clouds
// - LadderFilter: Moog-style warmth
// - TapeEffect: Authentic wow/flutter
//
// Controls:
//   1-4: Switch chord/mood
//   SPACE: Pause/Resume
//   G: Toggle film grain
//   C: Toggle CRT effect
//   TAB: Show parameters
//
// Download Prelinger Archive videos first:
//   ./download-videos.sh           # Download curated public domain films
//   ./download-videos.sh --quick   # Download just one video

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio/notes.h>
#include <vivid/audio_output.h>
#include <vivid/video/video.h>
#include <cmath>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;
using namespace vivid::audio::freq;
using namespace vivid::video;

// ============================================================================
// Musical content - BoC-inspired progressions
// ============================================================================

struct ChordVoicing {
    float notes[6];
    int count;
    const char* name;
};

// Melancholic, nostalgic chord voicings
static const ChordVoicing moods[] = {
    // Mood 0: Am9 - dreamy, floating
    {{A2, E3, G3, B3, C4, E4}, 6, "Am9 (Dreamy)"},
    // Mood 1: Fmaj7 - warm, hopeful
    {{F2, C3, E3, A3, C4, E4}, 6, "Fmaj7 (Warm)"},
    // Mood 2: Dm7 - introspective
    {{D2, A2, C3, F3, A3, D4}, 6, "Dm7 (Introspective)"},
    // Mood 3: Em7 - mysterious
    {{E2, B2, D3, G3, B3, E4}, 6, "Em7 (Mysterious)"},
};
static const int numMoods = sizeof(moods) / sizeof(moods[0]);

// State
static int currentMood = 0;
static float moodTime = 0.0f;
static const float moodDuration = 16.0f;  // Longer for Song sections
static bool isPaused = false;
static bool grainEnabled = true;
static bool crtEnabled = true;

void triggerMood(Chain& chain, int moodIdx) {
    auto& synth = chain.get<PolySynth>("synth");

    // Release all current notes
    synth.allNotesOff();

    // Play new voicing
    const ChordVoicing& mood = moods[moodIdx];
    for (int i = 0; i < mood.count; ++i) {
        synth.noteOn(mood.notes[i]);
    }

    std::cout << "Mood: " << mood.name << std::endl;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // TIMING & SONG STRUCTURE
    // =========================================================================

    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 72.0f;  // Slower, more hypnotic
    clock.swing = 0.08f;

    auto& song = chain.add<Song>("song");
    song.syncTo("clock");
    song.addSection("intro", 0, 8);
    song.addSection("verse1", 8, 24);
    song.addSection("chorus", 24, 32);
    song.addSection("verse2", 32, 48);
    song.addSection("chorus2", 48, 56);
    song.addSection("outro", 56, 72);

    // =========================================================================
    // SYNTHESIS: Layered pad + lead + bells + atmosphere
    // =========================================================================

    // Main pad synth - thick, evolving sound
    auto& synth = chain.add<PolySynth>("synth");
    synth.waveform(Waveform::Saw);
    synth.maxVoices = 12;
    synth.volume = 0.45f;
    synth.attack = 2.0f;
    synth.decay = 1.0f;
    synth.sustain = 0.7f;
    synth.release = 3.0f;
    synth.unisonDetune = 12.0f;
    synth.detune = 3.0f;

    // Ladder filter for warm Moog-style filtering
    auto& padFilter = chain.add<LadderFilter>("padFilter");
    padFilter.input("synth");
    padFilter.cutoff = 1800.0f;
    padFilter.resonance = 0.25f;
    padFilter.drive = 1.3f;

    // Wavetable lead - evolving timbre
    auto& lead = chain.add<WavetableSynth>("lead");
    lead.loadBuiltin(BuiltinTable::Analog);
    lead.maxVoices = 2;
    lead.detune = 6.0f;
    lead.attack = 0.15f;
    lead.decay = 0.3f;
    lead.sustain = 0.5f;
    lead.release = 0.8f;
    lead.volume = 0.3f;

    auto& leadFilter = chain.add<LadderFilter>("leadFilter");
    leadFilter.input("lead");
    leadFilter.cutoff = 2500.0f;
    leadFilter.resonance = 0.35f;

    // FM bells for ethereal textures
    auto& bells = chain.add<FMSynth>("bells");
    bells.loadPreset(FMPreset::Bell);
    bells.volume = 0.2f;

    // Granular clouds for atmosphere
    auto& clouds = chain.add<Granular>("clouds");
    // Note: loadSample would be called if we had a texture.wav
    // For now, clouds will be silent but the operator is in place
    clouds.grainSize = 100.0f;
    clouds.density = 8.0f;
    clouds.position = 0.5f;
    clouds.positionSpray = 0.25f;
    clouds.pitch = 0.5f;
    clouds.pitchSpray = 0.4f;
    clouds.panSpray = 0.9f;
    clouds.volume = 0.0f;  // Start silent (no sample loaded)
    clouds.setFreeze(true);

    // =========================================================================
    // FX CHAIN: Tape warmth → Delay → Reverb
    // =========================================================================

    // Mix synths
    auto& synthMix = chain.add<AudioMixer>("synthMix");
    synthMix.setInput(0, "padFilter");
    synthMix.setGain(0, 0.7f);
    synthMix.setInput(1, "leadFilter");
    synthMix.setGain(1, 0.4f);
    synthMix.setInput(2, "bells");
    synthMix.setGain(2, 0.35f);
    synthMix.setInput(3, "clouds");
    synthMix.setGain(3, 0.5f);

    // Tape effect for authentic BoC character
    auto& tape = chain.add<TapeEffect>("tape");
    tape.input("synthMix");
    tape.wow = 0.25f;
    tape.flutter = 0.18f;
    tape.saturation = 0.4f;
    tape.hiss = 0.05f;
    tape.age = 0.35f;

    // Delay - dotted eighth for rhythmic interest
    auto& delay = chain.add<Delay>("delay");
    delay.input("tape");
    delay.delayTime = 60000.0f / 72.0f * 0.75f;  // Dotted eighth at 72 BPM
    delay.feedback = 0.4f;
    delay.mix = 0.25f;

    // Lush reverb
    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("delay");
    reverb.roomSize = 0.88f;
    reverb.damping = 0.55f;
    reverb.mix = 0.45f;

    // Limiter for safety
    auto& limiter = chain.add<Limiter>("limiter");
    limiter.input("reverb");
    limiter.ceiling = -1.0f;
    limiter.release = 100.0f;

    // Audio output
    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("limiter");
    audioOut.setVolume(0.7f);
    chain.audioOutput("audioOut");

    // =========================================================================
    // VISUALS: Vintage film aesthetic
    // =========================================================================

    auto& video = chain.add<VideoPlayer>("video");
    video.setFile("AboutBan1935.mp4");
    video.setLoop(true);
    video.setSpeed(0.85f);

    // Sepia-ish color grading
    auto& color = chain.add<HSV>("color");
    color.input("video");
    color.hueShift = 0.08f;
    color.saturation = 0.4f;
    color.value = 0.9f;

    // Audio-reactive bloom
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("color");
    bloom.threshold = 0.65f;
    bloom.radius = 15.0f;
    bloom.intensity = 0.8f;

    // Audio-reactive feedback
    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input("bloom");
    feedback.decay = 0.9f;
    feedback.zoom = 1.003f;

    // Film grain overlay
    auto& grain = chain.add<FilmGrain>("grain");
    grain.input("feedback");
    grain.intensity = 0.22f;
    grain.size = 1.2f;
    grain.speed = 24.0f;
    grain.colored = 0.15f;

    // Vignette for period look
    auto& vignette = chain.add<Vignette>("vignette");
    vignette.input("grain");
    vignette.intensity = 0.7f;
    vignette.softness = 0.8f;
    vignette.roundness = 0.8f;

    // CRT effect
    auto& crt = chain.add<CRTEffect>("crt");
    crt.input("vignette");
    crt.scanlines = 0.12f;
    crt.curvature = 0.025f;
    crt.vignette = 0.15f;
    crt.bloom = 0.015f;
    crt.chromatic = 0.008f;

    // Beat-synced flash
    auto& flash = chain.add<Flash>("flash");
    flash.input("crt");
    flash.decay = 0.88f;
    flash.color.set(1.0f, 0.97f, 0.92f);

    chain.output("flash");

    // =========================================================================
    // Initialize
    // =========================================================================

    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "Prelinger Nostalgia\n";
    std::cout << "============================================\n";
    std::cout << "A Boards of Canada inspired audio-visual piece\n";
    std::cout << "\n";
    std::cout << "Audio features:\n";
    std::cout << "  - PolySynth: Warm chord pads\n";
    std::cout << "  - WavetableSynth: Evolving lead\n";
    std::cout << "  - FMSynth: Bell textures\n";
    std::cout << "  - LadderFilter: Moog warmth\n";
    std::cout << "  - TapeEffect: Wow/flutter\n";
    std::cout << "  - Song: Section structure\n";
    std::cout << "\n";
    std::cout << "Controls:\n";
    std::cout << "  1-4: Switch mood\n";
    std::cout << "  SPACE: Pause/Resume\n";
    std::cout << "  G: Toggle grain\n";
    std::cout << "  C: Toggle CRT\n";
    std::cout << "  B: Trigger bells\n";
    std::cout << "  L: Trigger lead note\n";
    std::cout << "  F: Trigger flash\n";
    std::cout << "  TAB: Parameters\n";
    std::cout << "============================================\n\n";

    // Start with first mood
    triggerMood(chain, currentMood);
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = ctx.dt();
    float time = ctx.time();

    auto& song = chain.get<Song>("song");

    // =========================================================================
    // Input handling
    // =========================================================================

    // Mood selection
    if (ctx.key(GLFW_KEY_1).pressed) { currentMood = 0; moodTime = 0; triggerMood(chain, 0); }
    if (ctx.key(GLFW_KEY_2).pressed) { currentMood = 1; moodTime = 0; triggerMood(chain, 1); }
    if (ctx.key(GLFW_KEY_3).pressed) { currentMood = 2; moodTime = 0; triggerMood(chain, 2); }
    if (ctx.key(GLFW_KEY_4).pressed) { currentMood = 3; moodTime = 0; triggerMood(chain, 3); }

    // Pause toggle
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        isPaused = !isPaused;
        if (isPaused) {
            chain.get<PolySynth>("synth").allNotesOff();
            chain.get<WavetableSynth>("lead").allNotesOff();
            chain.get<FMSynth>("bells").allNotesOff();
            std::cout << "[PAUSED]\n";
        } else {
            triggerMood(chain, currentMood);
            std::cout << "[RESUMED]\n";
        }
    }

    // Effect toggles
    if (ctx.key(GLFW_KEY_G).pressed) {
        grainEnabled = !grainEnabled;
        chain.get<FilmGrain>("grain").intensity = grainEnabled ? 0.22f : 0.0f;
        std::cout << "Grain: " << (grainEnabled ? "ON" : "OFF") << "\n";
    }
    if (ctx.key(GLFW_KEY_C).pressed) {
        crtEnabled = !crtEnabled;
        chain.get<CRTEffect>("crt").scanlines = crtEnabled ? 0.12f : 0.0f;
        chain.get<CRTEffect>("crt").curvature = crtEnabled ? 0.025f : 0.0f;
        std::cout << "CRT: " << (crtEnabled ? "ON" : "OFF") << "\n";
    }

    // Manual triggers for demo
    if (ctx.key(GLFW_KEY_B).pressed) {
        auto& bells = chain.get<FMSynth>("bells");
        bells.noteOn(C5);
        bells.noteOn(G5);
        std::cout << "Bells triggered\n";
    }
    if (ctx.key(GLFW_KEY_L).pressed) {
        auto& lead = chain.get<WavetableSynth>("lead");
        lead.noteOn(E4);
        std::cout << "Lead triggered\n";
    }
    if (ctx.key(GLFW_KEY_F).pressed) {
        chain.get<Flash>("flash").trigger();
        std::cout << "Flash triggered\n";
    }

    // =========================================================================
    // Section-based behavior
    // =========================================================================

    if (!isPaused) {
        moodTime += dt;

        // Auto-advance moods based on song sections
        if (song.sectionJustStarted()) {
            std::cout << "Section: " << song.section() << std::endl;

            // Trigger different moods for different sections
            if (song.section() == "intro") {
                currentMood = 0;
                triggerMood(chain, currentMood);
            } else if (song.section() == "verse1") {
                currentMood = 1;
                triggerMood(chain, currentMood);
            } else if (song.section() == "chorus" || song.section() == "chorus2") {
                currentMood = 2;
                triggerMood(chain, currentMood);
                // Trigger bells on chorus
                auto& bells = chain.get<FMSynth>("bells");
                bells.noteOn(C5);
                bells.noteOn(E5);
            } else if (song.section() == "verse2") {
                currentMood = 3;
                triggerMood(chain, currentMood);
            } else if (song.section() == "outro") {
                currentMood = 0;
                triggerMood(chain, currentMood);
            }
        }

        // Section progress affects parameters
        float sectionProgress = song.sectionProgress();

        // Chorus sections are more intense
        if (song.section() == "chorus" || song.section() == "chorus2") {
            chain.get<LadderFilter>("padFilter").cutoff = 2200.0f + sectionProgress * 800.0f;
            chain.get<Feedback>("feedback").decay = 0.92f + sectionProgress * 0.04f;
            chain.get<Bloom>("bloom").intensity = 1.2f + sectionProgress * 0.8f;
        } else {
            chain.get<LadderFilter>("padFilter").cutoff = 1400.0f + sectionProgress * 600.0f;
            chain.get<Feedback>("feedback").decay = 0.88f + sectionProgress * 0.04f;
            chain.get<Bloom>("bloom").intensity = 0.6f + sectionProgress * 0.4f;
        }

        // Outro fades out
        if (song.section() == "outro") {
            chain.get<AudioOutput>("audioOut").setVolume(0.7f * (1.0f - sectionProgress * 0.5f));
            chain.get<Granular>("clouds").pitchSpray = 0.4f + sectionProgress * 0.4f;
        }

        // =========================================================================
        // Continuous modulation
        // =========================================================================

        // Tape parameter drift for organic feel
        auto& tape = chain.get<TapeEffect>("tape");
        tape.wow = 0.2f + 0.1f * std::sin(time * 0.2f);
        tape.saturation = 0.35f + 0.1f * std::sin(time * 0.15f + 1.0f);

        // Filter LFO
        float filterLFO = std::sin(time * 0.08f);
        chain.get<LadderFilter>("leadFilter").cutoff = 2000.0f + filterLFO * 1000.0f;

        // Wavetable position modulation
        float posLFO = (std::sin(time * 0.05f) + 1.0f) * 0.5f;
        chain.get<WavetableSynth>("lead").position = posLFO;
    }

    // =========================================================================
    // Visual evolution
    // =========================================================================

    // Ensure video is playing
    auto& video = chain.get<VideoPlayer>("video");
    video.play();

    // Mood affects color temperature
    float targetHue = 0.06f + currentMood * 0.02f;
    chain.get<HSV>("color").hueShift = targetHue + 0.02f * std::sin(time * 0.3f);

    // Flickering brightness for vintage feel
    float flicker = 0.95f + 0.05f * std::sin(time * 17.3f) * std::sin(time * 23.7f);
    chain.get<HSV>("color").value = 0.85f * flicker;

    // Grain intensity varies
    if (grainEnabled) {
        chain.get<FilmGrain>("grain").intensity = 0.2f + 0.04f * std::sin(time * 0.7f);
    }

    // Feedback zoom pulses slightly
    float zoomPulse = 1.002f + 0.002f * std::sin(time * 0.4f);
    chain.get<Feedback>("feedback").zoom = zoomPulse;

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
