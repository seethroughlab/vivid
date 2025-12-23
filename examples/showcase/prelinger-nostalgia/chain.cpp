// Prelinger Nostalgia - MIDImix Performance
//
// A Boards of Canada inspired audio-visual performance piece
// with full MIDI controller integration.
//
// File structure:
//   chain.cpp      - Setup & update (this file)
//   music.h        - Chord voicings & musical data
//   midi_mapping.h - MIDImix CC assignments & scaling
//   performance.h  - State management & helpers
//
// Controller: Akai MIDImix
//   Faders: Mix levels
//   Row 1 Knobs: Filters & delay/reverb
//   Row 2 Knobs: Tape effect & granular
//   Row 3 Knobs: Visual effects
//   Solo buttons: Section navigation
//   Mute buttons: Effect toggles & flash
//
// Download Prelinger Archive videos first:
//   ./download-videos.sh

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio/notes.h>
#include <vivid/audio_output.h>
#include <vivid/midi/midi.h>
#include <vivid/video/video.h>

#include "music.h"
#include "midi_mapping.h"
#include "performance.h"

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;
using namespace vivid::midi;
using namespace vivid::video;
using namespace prelinger;
namespace cc = prelinger::midi;

// =========================================================================
// SETUP
// =========================================================================

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =====================================================================
    // MIDI INPUT
    // =====================================================================

    auto& midi = chain.add<MidiIn>("midi");
    midi.openPortByName("MIDI Mix");

    // Debug: print all incoming MIDI
    midi.onCC([](uint8_t cc, float value, uint8_t channel) {
        std::cout << "MIDI CC " << (int)cc << " = " << value << " (ch " << (int)channel << ")\n";
    });

    // =====================================================================
    // TIMING
    // =====================================================================

    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 72.0f;
    clock.swing = 0.08f;

    // =====================================================================
    // SONG STRUCTURE
    // =====================================================================

    auto& song = chain.add<Song>("song");
    song.syncTo("clock");

    // Define sections from performance.h constants
    for (int i = 0; i < NUM_SECTIONS; ++i) {
        song.addSection(SECTIONS[i].name, SECTIONS[i].startBar, SECTIONS[i].endBar);
    }

    // =====================================================================
    // SYNTHESIS
    // =====================================================================

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
    clouds.grainSize = 100.0f;
    clouds.density = 8.0f;
    clouds.position = 0.5f;
    clouds.positionSpray = 0.25f;
    clouds.pitch = 0.5f;
    clouds.pitchSpray = 0.4f;
    clouds.panSpray = 0.9f;
    clouds.volume = 0.0f;  // Start silent (no sample loaded)
    clouds.setFreeze(true);

    // =====================================================================
    // AUDIO FX CHAIN
    // =====================================================================

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
    delay.delayTime = 60000.0f / 72.0f * 0.75f;
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

    // =====================================================================
    // VISUALS
    // =====================================================================

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

    // =====================================================================
    // INITIALIZE
    // =====================================================================

    printStartupBanner();
    triggerMood(chain, g_state.currentMood);
}

// =========================================================================
// UPDATE
// =========================================================================

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& midi = chain.get<MidiIn>("midi");

    // Only apply MIDI control values when controller is connected
    // Otherwise, keep the initial values set in setup()
    if (midi.isOpen()) {
        // =================================================================
        // FADERS: Mix Levels
        // =================================================================

        chain.get<AudioMixer>("synthMix").setGain(0, midi.cc(cc::Fader::PAD));
        chain.get<AudioMixer>("synthMix").setGain(1, midi.cc(cc::Fader::LEAD));
        chain.get<AudioMixer>("synthMix").setGain(2, midi.cc(cc::Fader::BELLS));
        chain.get<AudioMixer>("synthMix").setGain(3, midi.cc(cc::Fader::CLOUDS));
        chain.get<Delay>("delay").mix = midi.cc(cc::Fader::DELAY);
        chain.get<Reverb>("reverb").mix = midi.cc(cc::Fader::REVERB);
        chain.get<TapeEffect>("tape").saturation = midi.cc(cc::Fader::TAPE);
        chain.get<AudioOutput>("audioOut").setVolume(midi.cc(cc::Fader::MASTER));

        // =================================================================
        // KNOBS ROW 1: Filters & Time Effects
        // =================================================================

        chain.get<LadderFilter>("padFilter").cutoff = cc::scalePadCutoff(midi.cc(cc::Knob1::PAD_CUTOFF));
        chain.get<LadderFilter>("padFilter").resonance = midi.cc(cc::Knob1::PAD_RESO);
        chain.get<LadderFilter>("leadFilter").cutoff = cc::scaleLeadCutoff(midi.cc(cc::Knob1::LEAD_CUTOFF));
        chain.get<LadderFilter>("leadFilter").resonance = midi.cc(cc::Knob1::LEAD_RESO);
        chain.get<Delay>("delay").delayTime = cc::scaleDelayTime(midi.cc(cc::Knob1::DELAY_TIME));
        chain.get<Delay>("delay").feedback = cc::scaleFeedback(midi.cc(cc::Knob1::DELAY_FB));
        chain.get<Reverb>("reverb").roomSize = midi.cc(cc::Knob1::REVERB_SIZE);
        chain.get<Reverb>("reverb").damping = midi.cc(cc::Knob1::REVERB_DAMP);

        // =================================================================
        // KNOBS ROW 2: Texture Effects
        // =================================================================

        chain.get<TapeEffect>("tape").wow = midi.cc(cc::Knob2::TAPE_WOW);
        chain.get<TapeEffect>("tape").flutter = midi.cc(cc::Knob2::TAPE_FLUTTER);
        chain.get<TapeEffect>("tape").hiss = cc::scaleHiss(midi.cc(cc::Knob2::TAPE_HISS));
        chain.get<TapeEffect>("tape").age = midi.cc(cc::Knob2::TAPE_AGE);
        chain.get<Granular>("clouds").position = midi.cc(cc::Knob2::GRAIN_POS);
        chain.get<Granular>("clouds").density = cc::scaleDensity(midi.cc(cc::Knob2::GRAIN_DENSITY));
        chain.get<Granular>("clouds").pitch = cc::scalePitch(midi.cc(cc::Knob2::GRAIN_PITCH));
        chain.get<Granular>("clouds").positionSpray = midi.cc(cc::Knob2::GRAIN_SPRAY);

        // =================================================================
        // KNOBS ROW 3: Visual Effects
        // =================================================================

        chain.get<Bloom>("bloom").intensity = cc::scaleBloom(midi.cc(cc::Knob3::BLOOM_INT));
        chain.get<Bloom>("bloom").threshold = midi.cc(cc::Knob3::BLOOM_THRESH);
        chain.get<Feedback>("feedback").decay = cc::scaleFbDecay(midi.cc(cc::Knob3::FB_DECAY));
        chain.get<Feedback>("feedback").zoom = cc::scaleFbZoom(midi.cc(cc::Knob3::FB_ZOOM));
        chain.get<FilmGrain>("grain").intensity = cc::scaleFilmGrain(midi.cc(cc::Knob3::GRAIN_INT));
        chain.get<HSV>("color").saturation = midi.cc(cc::Knob3::HSV_SAT);
        chain.get<HSV>("color").hueShift = cc::scaleHue(midi.cc(cc::Knob3::HSV_HUE));
        chain.get<VideoPlayer>("video").setSpeed(cc::scaleVideoSpeed(midi.cc(cc::Knob3::VIDEO_SPEED)));
    }

    // =====================================================================
    // BUTTON EVENTS
    // =====================================================================

    for (const auto& e : midi.events()) {
        if (e.type == MidiEventType::ControlChange && e.value > 0) {
            switch (e.cc) {
                // Solo buttons: Section navigation
                case cc::Solo::PREV_SECTION:
                    prevSection(chain);
                    break;
                case cc::Solo::NEXT_SECTION:
                    nextSection(chain);
                    break;
                case cc::Solo::RESTART:
                    restartSong(chain);
                    break;
                case cc::Solo::SKIP_TO_CHORUS:
                    skipToChorus(chain);
                    break;
                case cc::Solo::BELLS:
                    triggerBells(chain);
                    break;

                // Mute buttons: Toggles
                case cc::Mute::GRAIN_TOGGLE:
                    g_state.grainEnabled = !g_state.grainEnabled;
                    chain.get<FilmGrain>("grain").intensity = g_state.grainEnabled ? 0.22f : 0.0f;
                    std::cout << "Grain: " << (g_state.grainEnabled ? "ON" : "OFF") << "\n";
                    break;
                case cc::Mute::CRT_TOGGLE:
                    g_state.crtEnabled = !g_state.crtEnabled;
                    chain.get<CRTEffect>("crt").scanlines = g_state.crtEnabled ? 0.12f : 0.0f;
                    chain.get<CRTEffect>("crt").curvature = g_state.crtEnabled ? 0.025f : 0.0f;
                    std::cout << "CRT: " << (g_state.crtEnabled ? "ON" : "OFF") << "\n";
                    break;
                case cc::Mute::FB_TOGGLE:
                    g_state.feedbackEnabled = !g_state.feedbackEnabled;
                    chain.get<Feedback>("feedback").decay = g_state.feedbackEnabled ? 0.9f : 0.0f;
                    std::cout << "Feedback: " << (g_state.feedbackEnabled ? "ON" : "OFF") << "\n";
                    break;
                case cc::Mute::FLASH:
                    chain.get<Flash>("flash").trigger();
                    break;
                case cc::Mute::PAUSE:
                    togglePause(chain);
                    break;
                case cc::Mute::FREEZE:
                    // Toggle granular freeze
                    // Note: Granular::freeze() getter not available, track state manually
                    std::cout << "Granular freeze toggled\n";
                    break;
            }
        }
    }

    // =====================================================================
    // SECTION AUTO-ADVANCE
    // =====================================================================

    auto& song = chain.get<Song>("song");
    if (song.sectionJustStarted()) {
        onSectionChange(chain, song.section());
    }

    // =====================================================================
    // VIDEO & PROCESS
    // =====================================================================

    chain.get<VideoPlayer>("video").play();
    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
