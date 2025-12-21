// Prelinger Nostalgia - Boards of Canada inspired audio-visual piece
// Combines polyphonic synthesis, tape warmth, and vintage film aesthetics
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
static const float moodDuration = 8.0f;
static bool isPaused = false;
static bool grainEnabled = true;
static bool crtEnabled = true;

void triggerMood(Chain& chain, int moodIdx) {
    auto& synth = chain.get<PolySynth>("synth");

    // Release all current notes
    synth.allNotesOff();

    // Play new voicing with slight arpeggiation
    const ChordVoicing& mood = moods[moodIdx];
    for (int i = 0; i < mood.count; ++i) {
        synth.noteOn(mood.notes[i]);
    }

    std::cout << "Mood: " << mood.name << std::endl;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Audio: Polyphonic pad synth through tape
    // =========================================================================

    // Main pad synth - thick, evolving sound
    auto& synth = chain.add<PolySynth>("synth");
    synth.waveform(Waveform::Saw);
    synth.maxVoices = 12;  // Allow for rich voicings
    synth.volume = 0.5f;
    synth.attack = 2.0f;    // Very slow attack - pad-like
    synth.decay = 1.0f;
    synth.sustain = 0.7f;
    synth.release = 3.0f;   // Long release for ethereal sustain
    synth.unisonDetune = 12.0f;  // Wide stereo spread
    synth.detune = 3.0f;    // Slight pitch drift

    // Tape effect for warmth and character
    auto& tape = chain.add<TapeEffect>("tape");
    tape.input("synth");
    tape.wow = 0.2f;        // Gentle pitch drift
    tape.flutter = 0.15f;   // Subtle modulation
    tape.saturation = 0.35f; // Warm compression
    tape.hiss = 0.06f;      // Period-appropriate noise floor
    tape.age = 0.3f;        // Some high-frequency loss

    // Audio output
    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("tape");
    audioOut.setVolume(0.6f);
    chain.audioOutput("audioOut");

    // =========================================================================
    // Visuals: Vintage film aesthetic
    // =========================================================================

    // Base layer - Prelinger Archive footage
    // Path is relative to project directory (where chain.cpp is)
    auto& video = chain.add<VideoPlayer>("video");
    video.setFile("AboutBan1935.mp4");
    video.setLoop(true);
    video.setSpeed(0.85f);  // Slightly slowed for dreaminess

    // Sepia-ish color grading
    auto& color = chain.add<HSV>("color");
    color.input(&video);
    color.hueShift = 0.08f;  // Warm sepia tone
    color.saturation = 0.4f;  // Desaturated
    color.value = 0.9f;

    // Film grain overlay
    auto& grain = chain.add<FilmGrain>("grain");
    grain.input(&color);
    grain.intensity = 0.2f;
    grain.size = 1.2f;
    grain.speed = 24.0f;  // Film frame rate
    grain.colored = 0.15f;  // Slight color variation

    // Vignette for period look
    auto& vignette = chain.add<Vignette>("vignette");
    vignette.input(&grain);
    vignette.intensity = 0.7f;
    vignette.softness = 0.8f;
    vignette.roundness = 0.8f;

    // Optional CRT effect
    auto& crt = chain.add<CRTEffect>("crt");
    crt.input(&vignette);
    crt.scanlines = 0.15f;
    crt.curvature = 0.03f;
    crt.vignette = 0.2f;
    crt.bloom = 0.02f;
    crt.chromatic = 0.01f;

    chain.output("crt");

    // =========================================================================
    // Initialize
    // =========================================================================

    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "Prelinger Nostalgia\n";
    std::cout << "============================================\n";
    std::cout << "A Boards of Canada inspired audio-visual piece\n";
    std::cout << "\n";
    std::cout << "Controls:\n";
    std::cout << "  1-4: Switch mood\n";
    std::cout << "  SPACE: Pause/Resume\n";
    std::cout << "  G: Toggle grain\n";
    std::cout << "  C: Toggle CRT\n";
    std::cout << "  TAB: Parameters\n";
    std::cout << "============================================\n\n";

    // Start with first mood
    triggerMood(chain, currentMood);
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = ctx.dt();
    float time = ctx.time();

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
            std::cout << "[PAUSED]\n";
        } else {
            triggerMood(chain, currentMood);
            std::cout << "[RESUMED]\n";
        }
    }

    // Effect toggles
    if (ctx.key(GLFW_KEY_G).pressed) {
        grainEnabled = !grainEnabled;
        chain.get<FilmGrain>("grain").intensity = grainEnabled ? 0.2f : 0.0f;
        std::cout << "Grain: " << (grainEnabled ? "ON" : "OFF") << "\n";
    }
    if (ctx.key(GLFW_KEY_C).pressed) {
        crtEnabled = !crtEnabled;
        chain.get<CRTEffect>("crt").scanlines = crtEnabled ? 0.15f : 0.0f;
        chain.get<CRTEffect>("crt").curvature = crtEnabled ? 0.03f : 0.0f;
        std::cout << "CRT: " << (crtEnabled ? "ON" : "OFF") << "\n";
    }

    // =========================================================================
    // Audio evolution
    // =========================================================================

    if (!isPaused) {
        moodTime += dt;

        // Auto-advance moods
        if (moodTime >= moodDuration) {
            moodTime = 0.0f;
            currentMood = (currentMood + 1) % numMoods;
            triggerMood(chain, currentMood);
        }

        // Subtle parameter drift for organic feel
        auto& tape = chain.get<TapeEffect>("tape");
        tape.wow = 0.15f + 0.1f * std::sin(time * 0.2f);
        tape.saturation = 0.3f + 0.1f * std::sin(time * 0.15f + 1.0f);
    }

    // =========================================================================
    // Visual evolution
    // =========================================================================

    // Ensure video is playing
    auto& video = chain.get<VideoPlayer>("video");
    video.play();

    // Mood affects color temperature
    float moodProgress = moodTime / moodDuration;
    float targetHue = 0.06f + currentMood * 0.02f;  // Subtle hue shifts per mood
    chain.get<HSV>("color").hueShift = targetHue + 0.02f * std::sin(time * 0.3f);

    // Flickering brightness for vintage feel
    float flicker = 0.95f + 0.05f * std::sin(time * 17.3f) * std::sin(time * 23.7f);
    chain.get<HSV>("color").value = 0.85f * flicker;

    // Grain intensity varies slightly
    if (grainEnabled) {
        chain.get<FilmGrain>("grain").intensity = 0.18f + 0.04f * std::sin(time * 0.7f);
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
