# Audio-Visual Parity: Exploration Report

## Vision

Create a workflow where audio and visuals are equal peers in code, encouraging true co-creation rather than "musician adding visuals" or "visual artist adding audio."

**Core Principle:** Keep creators in a unified audio-visual headspace by making both domains equally expressive through the same code-first workflow. External plugins create distance between creator and creation—native operators keep everything immediate and learnable.

---

## Part 1: Current Capabilities

### Audio Foundation (vivid-audio)
| Category | Operators |
|----------|-----------|
| **Timing** | Clock (BPM, swing, divisions), Sequencer (16-step), Euclidean |
| **Drums** | Kick, Snare, HiHat, Clap |
| **Synth** | Oscillator, Synth, Envelope, Formant |
| **Effects** | Delay, Reverb, Chorus, Compressor, Limiter, Flanger, Phaser |
| **Lo-fi** | Bitcrush, Overdrive, Crackle |
| **Analysis** | FFT, BandSplit, BeatDetect, Levels |
| **I/O** | AudioIn, AudioFile, SamplePlayer, MidiIn, MidiFilePlayer |

### Visual Foundation (core + addons)
| Category | Operators |
|----------|-----------|
| **2D Effects** | Noise, Blur, Feedback, Bloom, HSV, Brightness, Quantize, Dither |
| **Distortion** | CRTEffect, Scanlines, BarrelDistortion, ChromaticAberration, Displace |
| **Composition** | Composite, Blend, Mask, Transform, Mirror, Tile |
| **Particles** | Particles (emit rate, velocity, color, life) |
| **3D** | Render3D, PBR materials, CSG, lights, shadows |
| **Modulation** | LFO (sine, triangle, saw, square, noise) |
| **Video** | VideoPlayer (HAP, H.264, ProRes), Webcam |

### Current Connection Pattern
```cpp
// Audio → Visual (works today)
void update(Context& ctx) {
    float bass = chain.get<BandSplit>("bands").bass();
    chain.get<Noise>("noise").scale = 5.0f + bass * 15.0f;
}

// Visual → Audio (also works, but undocumented)
void update(Context& ctx) {
    float mouseX = (ctx.mouseNorm().x + 1.0f) * 0.5f;
    chain.get<Oscillator>("osc").frequency = 200 + mouseX * 800;
}
```

---

## Part 2: Gap Analysis

### The Imbalance

The visual side of Vivid has sophisticated features:
- PBR materials with metallic/roughness workflows
- Real-time shadows (directional, point, spot) with PCF soft shadows
- CSG boolean operations on 3D geometry
- Particle systems with physics
- Temporal effects (Feedback, TimeMachine, FrameCache)

The audio side has basic building blocks but lacks equivalent sophistication:
- Single-voice oscillators (no polyphony)
- Basic waveforms only (no wavetable morphing)
- No tape/analog modeling
- Manual modulation routing (requires update() code)
- No granular synthesis for textures

### What's Missing for True Co-Creation

| Gap | Description |
|-----|-------------|
| **Examples show one domain** | Audio examples don't include visuals; visual examples lack audio |
| **Manual wiring required** | Users must write update() code to connect domains |
| **No shared modulation** | LFO drives visuals OR audio, not both simultaneously |
| **Pattern isolation** | Sequencer triggers audio only, not visual events |
| **No song structure** | No way to define sections that affect both domains |
| **Basic synthesis** | Visual side has PBR/shadows/CSG; audio has basic oscillators |
| **No tape character** | Visuals have CRT/VHS looks; audio lacks equivalent warmth |

---

## Part 3: Feature Paths

### Path A: Templates & Documentation (Low Effort, High Impact)

**Goal:** Show what's possible with existing features.

#### A1. Audio-Visual Starter Templates
```
examples/audiovisual/
  drum-reactive/        # Drum machine + synchronized particle bursts
  generative-ambient/   # Evolving synth + flowing feedback
  spectrum-art/         # FFT spectrum as visual art
  bidirectional/        # Mouse controls both pitch AND particle size
  prelinger-nostalgia/  # BoC-style ambient + retro video
```

#### A2. RECIPES.md Audio-Visual Patterns
- "Bass-reactive particles" - BandSplit → Particles emit rate
- "Beat-synced strobe" - BeatDetect → Flash opacity
- "Mouse theremin" - Mouse position → Oscillator frequency
- "Particle density synth" - Particle count → filter cutoff

#### A3. LFO as Shared Modulation Source (Already Works)
```cpp
void update(Context& ctx) {
    float mod = chain.get<LFO>("lfo").outputValue();

    // Drive BOTH domains with same value
    chain.get<Noise>("noise").scale = 5.0f + mod * 15.0f;
    chain.get<AudioFilter>("filter").cutoff = 500 + mod * 3000;
}
```

**Effort:** 2-3 days | **Files:** examples/, docs/RECIPES.md

---

### Path B: Parameter Binding Syntax (Medium Effort)

**Goal:** Reduce boilerplate in update() with declarative bindings.

#### B1. Simple Bind API
```cpp
// In setup() - declarative connections
noise.scale.bind(bands, &BandSplit::bass, 5.0f, 20.0f);
bloom.intensity.bind(beat, &BeatDetect::energy, 0.5f, 3.0f);

// Bidirectional: visual → audio
filter.cutoff.bind(ctx.mouseNormX(), 500.0f, 4000.0f);

// LFO → multiple targets
lfo.bind(&filter.cutoff, 500.0f, 4000.0f);
lfo.bind(&noise.scale, 5.0f, 20.0f);
lfo.bind(&bloom.intensity, 0.5f, 3.0f);
```

#### B2. Implementation Sketch
```cpp
template<typename T>
class Param {
    std::function<float()> m_binding;
    float m_min, m_max;

public:
    void bind(auto& source, auto getter, float min, float max) {
        m_binding = [&source, getter]() { return (source.*getter)(); };
        m_min = min; m_max = max;
    }

    float get() const {
        if (m_binding) {
            float t = m_binding();
            return m_min + t * (m_max - m_min);
        }
        return m_value;
    }
};
```

**Effort:** 1 week | **Files:** core/include/vivid/param.h, operators

---

### Path C: Trigger Routing (Medium Effort)

**Goal:** Sequencer patterns trigger both audio AND visual events.

#### C1. Callback-Based Triggers
```cpp
auto& kickSeq = chain.add<Sequencer>("kickSeq");
kickSeq.onTrigger([&]() {
    kick.trigger();              // Audio
    flash.trigger();             // Visual flash
    particles.burst(50);         // Visual particles
});
```

#### C2. Flash Operator (New Visual Operator)
```cpp
class Flash : public TextureOperator {
    Param<float> decay{"decay", 0.95f, 0.8f, 0.99f};
    Param<glm::vec3> color{"color", glm::vec3(1.0f)};
    float m_intensity = 0.0f;

public:
    void trigger() { m_intensity = 1.0f; }
    void process(Context& ctx) override {
        m_intensity *= static_cast<float>(decay);
        // Render colored overlay with m_intensity alpha
    }
};
```

**Effort:** 3-5 days | **Files:** core/include/vivid/effects/flash.h, sequencer.h

---

### Path D: Shared Modulation Bus (Higher Effort)

**Goal:** Named modulation sources accessible by both domains.

#### D1. ModulationBus Concept
```cpp
void setup(Context& ctx) {
    auto& bus = ctx.modulationBus();

    // Register modulation sources
    bus.add("lfo", chain.get<LFO>("lfo"), &LFO::outputValue);
    bus.add("bass", chain.get<BandSplit>("bands"), &BandSplit::bass);
    bus.add("beat", chain.get<BeatDetect>("beat"), &BeatDetect::energy);
    bus.add("mouseX", [&]() { return ctx.mouseNorm().x * 0.5f + 0.5f; });
}

void update(Context& ctx) {
    auto& bus = ctx.modulationBus();

    // Any operator can read any modulation source
    float intensity = bus.get("beat") * 0.5f + bus.get("lfo") * 0.5f;

    chain.get<Bloom>("bloom").intensity = intensity * 3.0f;
    chain.get<Synth>("synth").drive = intensity * 0.8f;
}
```

**Effort:** 1-2 weeks | **Files:** core/include/vivid/modulation_bus.h, context.h

---

### Path E: Value Operators & Visual Metrics (Higher Effort)

**Goal:** Visual operators expose readable metrics for audio modulation.

#### E1. Add Metrics to Existing Operators
```cpp
// Particles - expose count, velocity, life
class Particles : public TextureOperator {
public:
    float activeCount() const { return m_activeParticles; }
    float avgVelocity() const { return m_avgVelocity; }
    float avgLife() const { return m_avgLife; }
};

// Usage: particles → audio
void update(Context& ctx) {
    float density = chain.get<Particles>("p").activeCount() / 500.0f;
    chain.get<Synth>("synth").polyphony = 1 + int(density * 8);
}
```

**Effort:** 1 week | **Files:** operator.h, particles.h, shape.h

---

### Path F: Song Structure System (Higher Effort)

**Goal:** Define sections that affect both audio and visual behavior.

#### F1. Song Operator
```cpp
auto& song = chain.add<Song>("song");
song.syncTo(clock);
song.addSection("intro", 0, 16);      // bars 0-16
song.addSection("buildup", 16, 24);
song.addSection("drop", 24, 40);

void update(Context& ctx) {
    auto& song = chain.get<Song>("song");

    if (song.section() == "drop") {
        kick.volume = 1.0f;
        particles.emitRate = 500;
        bloom.intensity = 2.0f;
    } else if (song.section() == "buildup") {
        float t = song.sectionProgress();  // 0-1
        filter.cutoff = 500 + t * 3500;
        particles.emitRate = 50 + t * 200;
    }
}
```

**Effort:** 2 weeks | **Files:** core/include/vivid/song.h

---

### Path G: Macro Parameters (Medium Effort)

**Goal:** Single control point that affects both domains.

```cpp
auto& macro = chain.add<Macro>("intensity");
macro.range(0.0f, 1.0f);

// Bind multiple parameters to one macro
macro.bind(&synth.drive, 0.0f, 0.8f);
macro.bind(&bloom.intensity, 0.5f, 3.0f);
macro.bind(&particles.emitRate, 50.0f, 500.0f);
macro.bind(&feedback.decay, 0.9f, 0.99f);

// Control via MIDI, mouse, or code
macro.value = midiIn.cc(1) / 127.0f;
```

**Effort:** 1 week | **Files:** core/include/vivid/macro.h

---

### Path H: Native Synthesis Parity (High Effort, High Impact) - PRIORITY

**Goal:** Bring audio synthesis to the same level of sophistication as visual rendering.

Just as visuals have PBR materials, shadows, and CSG—audio needs wavetables, polyphony, and tape modeling. All implemented as native operators that users can understand and modify in code.

#### H1. TapeEffect - Analog Warmth

The audio equivalent of CRTEffect. Adds the character that defines lo-fi and vintage sounds.

```cpp
class TapeEffect : public AudioOperator {
    Param<float> wow{"wow", 0.3f, 0.0f, 1.0f};           // Slow pitch drift (0.1-2 Hz)
    Param<float> flutter{"flutter", 0.2f, 0.0f, 1.0f};   // Fast pitch jitter (5-20 Hz)
    Param<float> saturation{"saturation", 0.5f, 0.0f, 1.0f}; // Soft compression
    Param<float> hiss{"hiss", 0.1f, 0.0f, 1.0f};         // Tape noise floor
    Param<float> dropouts{"dropouts", 0.0f, 0.0f, 0.5f}; // Random signal loss
    Param<float> age{"age", 0.0f, 0.0f, 1.0f};           // Combined degradation

    void process(Context& ctx) override {
        // Variable delay line for wow/flutter
        // Soft saturation curve (tanh-based)
        // Filtered noise for hiss
        // Random gain drops for dropouts
    }
};
```

**Effort:** 3-4 days

#### H2. PolySynth - Polyphonic Synthesis

Play chords and pads with proper voice management.

```cpp
class PolySynth : public AudioOperator {
    Param<int> voices{"voices", 4, 1, 16};
    Param<Waveform> waveform{"waveform", Waveform::Saw};
    Param<float> detune{"detune", 0.1f, 0.0f, 1.0f};     // Voice detuning (cents)
    Param<float> spread{"spread", 0.5f, 0.0f, 1.0f};     // Stereo spread
    Param<float> attack{"attack", 0.01f, 0.001f, 2.0f};
    Param<float> decay{"decay", 0.1f, 0.001f, 2.0f};
    Param<float> sustain{"sustain", 0.7f, 0.0f, 1.0f};
    Param<float> release{"release", 0.3f, 0.001f, 5.0f};

    void noteOn(int note, float velocity = 1.0f);
    void noteOff(int note);
    void allNotesOff();

    // Voice stealing: oldest, quietest, or same-note
    void voiceStealMode(VoiceSteal mode);
};
```

**Effort:** 1 week

#### H3. WavetableSynth - Morphing Timbres

The audio equivalent of procedural textures—rich, evolving sounds from mathematical tables.

```cpp
class WavetableSynth : public AudioOperator {
    Param<float> position{"position", 0.0f, 0.0f, 1.0f}; // Morph through table
    Param<int> voices{"voices", 1, 1, 8};
    Param<float> detune{"detune", 0.0f, 0.0f, 1.0f};

    // Built-in wavetables
    void loadBuiltin(BuiltinTable table);  // Basic, Analog, Digital, Vocal, etc.

    // Load from file (Serum-compatible single-cycle format)
    void loadWavetable(const std::string& path);

    // Generate programmatically
    void generateFromHarmonics(const std::vector<float>& harmonics);
    void generateFromFormula(std::function<float(float phase, float position)> fn);
};

enum class BuiltinTable {
    Basic,      // Sine → Triangle → Saw → Square
    Analog,     // Warm, slightly detuned classics
    Digital,    // Harsh, FM-like timbres
    Vocal,      // Formant-based vowel sounds
    Texture,    // Noise-based, granular feel
    PWM         // Pulse width modulation sweep
};
```

**Effort:** 2 weeks

#### H4. Granular - Textural Synthesis

Create ambient clouds, stretch time, and mangle samples.

```cpp
class Granular : public AudioOperator {
    Param<float> grainSize{"grainSize", 100.0f, 10.0f, 500.0f};   // ms
    Param<float> density{"density", 10.0f, 1.0f, 100.0f};         // grains/sec
    Param<float> position{"position", 0.5f, 0.0f, 1.0f};          // playhead
    Param<float> positionSpray{"positionSpray", 0.1f, 0.0f, 0.5f};
    Param<float> pitch{"pitch", 1.0f, 0.25f, 4.0f};
    Param<float> pitchSpray{"pitchSpray", 0.0f, 0.0f, 1.0f};
    Param<float> pan{"pan", 0.0f, -1.0f, 1.0f};
    Param<float> panSpray{"panSpray", 0.0f, 0.0f, 1.0f};
    Param<GrainWindow> window{"window", GrainWindow::Hann};

    void loadSample(const std::string& path);
    void freeze(bool enabled);  // Stop position movement, just spray
};

enum class GrainWindow { Hann, Triangle, Rectangle, Gaussian };
```

**Effort:** 2 weeks

#### H5. FMSynth - Frequency Modulation

Classic DX7-style synthesis with modern usability.

```cpp
class FMSynth : public AudioOperator {
    Param<FMAlgorithm> algorithm{"algorithm", FMAlgorithm::Stack4};
    Param<float> ratio[4];      // Frequency ratios per operator
    Param<float> level[4];      // Output levels per operator
    Param<float> feedback{"feedback", 0.0f, 0.0f, 1.0f};  // Op4 self-mod

    // Envelope per operator
    void setEnvelope(int op, float a, float d, float s, float r);

    // Presets for common sounds
    void loadPreset(FMPreset preset);  // EPiano, Bass, Bell, Brass, etc.
};

enum class FMAlgorithm {
    Stack4,     // 1→2→3→4 (classic FM bass)
    Stack2x2,   // 1→2, 3→4 mixed
    Branch,     // 1→2,3,4 (one modulator, three carriers)
    Parallel,   // 1,2,3,4 independent (additive)
    // ... 32 algorithms like DX7
};
```

**Effort:** 2 weeks

#### H6. Advanced Filters

More filter types to match the variety of visual effects.

```cpp
enum class FilterType {
    // Existing
    Lowpass, Highpass, Bandpass, Notch,
    Lowshelf, Highshelf, Peak,

    // New - Analog modeled
    Ladder,         // Moog-style 24dB/oct with resonance
    StateVariable,  // Multimode with continuous morph
    Diode,          // Harsh, aggressive character

    // New - Creative
    Comb,           // For metallic/resonant textures
    Formant,        // Already exists, but enhance
    Phaser,         // All-pass chain
    Vowel           // A-E-I-O-U morphing
};
```

**Effort:** 1 week

#### H7. FilmGrain - Visual Complement

Complete the vintage aesthetic on the visual side.

```cpp
class FilmGrain : public TextureOperator {
    Param<float> intensity{"intensity", 0.3f, 0.0f, 1.0f};
    Param<float> size{"size", 1.0f, 0.5f, 3.0f};
    Param<float> speed{"speed", 24.0f, 1.0f, 60.0f};
    Param<bool> colored{"colored", false};
    Param<float> scratches{"scratches", 0.0f, 0.0f, 1.0f};  // Vertical lines
    Param<float> flicker{"flicker", 0.0f, 0.0f, 1.0f};      // Brightness variation
};
```

**Effort:** 1 day

---

### Path I: Advanced Features (Future)

| Feature | Description |
|---------|-------------|
| **GPU-side FFT** | Pass spectrum data as uniform buffer to shaders |
| **Ableton Link** | Sync tempo across devices and apps |
| **Timeline Editor** | Visual arrangement of audio + visual clips |
| **Live Performance** | Cue points, scene switching, MIDI mapping |
| **Recording Mode Sync** | Frame-locked audio/video export |

---

## Part 4: Case Study - Boards of Canada meets Prelinger Archive

**Prompt:** "Make me an evolving Boards of Canada style track (lead, bass, ambient beat, atmospherics), with a retro, nostalgic, reactive visual, featuring footage from the prelinger archive"

This case study tests whether Vivid can deliver audio-visual co-creation at a professional level.

### What Boards of Canada Sound Requires

| Element | What It Needs | Vivid Today | Gap |
|---------|---------------|-------------|-----|
| **Detuned synths** | Pitch drift, wow/flutter | ❌ No tape modulation | Critical |
| **Warm pads** | Filtered saw, slow attack | ✅ Oscillator + Filter + ADSR | Ready |
| **Lo-fi texture** | Tape saturation, bit reduction | ⚠️ Bitcrush (digital), Overdrive | Partial |
| **Hypnotic beats** | Kick, snare, hats | ✅ Drum synths + Sequencer | Ready |
| **Vinyl crackle** | Random impulses | ✅ Crackle operator | Ready |
| **Sample manipulation** | Pitch shift, slow down | ⚠️ SamplePlayer (0.5x-2x, changes length) | Partial |
| **Washed filters** | Slow LP sweeps | ⚠️ Filter exists, no easy LFO routing | Partial |
| **Polyphonic chords** | 4+ note pads | ❌ Single-voice synths only | Critical |
| **Granular atmosphere** | Texture clouds | ❌ Not implemented | Missing |

### What Retro Visuals Require

| Element | What It Needs | Vivid Today | Gap |
|---------|---------------|-------------|-----|
| **Prelinger footage** | Video playback | ✅ VideoPlayer (HAP, H.264) | Ready |
| **Retro CRT look** | Scanlines, barrel distortion | ✅ CRTEffect, Scanlines | Ready |
| **Film grain** | Noise overlay | ⚠️ Can fake with Noise + Composite | Partial |
| **Sepia/faded** | Color shift | ✅ HSV + Brightness | Ready |
| **VHS tracking** | Jitter, color bleed | ❌ Not implemented | Missing |
| **Audio reactivity** | BandSplit → visuals | ✅ Works today | Ready |

### Priority List for BoC-Style Creation

| Priority | Feature | Effort | Impact |
|----------|---------|--------|--------|
| **1** | TapeEffect (wow/flutter/saturation) | 3-4 days | Defines the sound |
| **2** | PolySynth (4-8 voice) | 1 week | Enables chords/pads |
| **3** | Modulation routing (LFO → any param) | 1 week | Reduces boilerplate |
| **4** | FilmGrain visual | 1 day | Completes the look |
| **5** | Granular synthesis | 2 weeks | Atmospheric depth |
| **6** | WavetableSynth | 2 weeks | Evolving timbres |

### Example: Prelinger Nostalgia (What's Possible Today)

This example shows what can be achieved with current Vivid capabilities, plus notes on what would improve with new features.

```cpp
// examples/audiovisual/prelinger-nostalgia/chain.cpp
//
// "Boards of Canada meets Prelinger Archive"
// Evolving ambient track with retro reactive visuals
//
// NOTE: This example works today but would benefit from:
// - TapeEffect (wow/flutter) for authentic BoC sound
// - PolySynth for chord pads
// - FilmGrain for authentic film look
// - Granular for textural atmosphere

#include <vivid/vivid.h>
#include <vivid/audio/audio.h>
#include <vivid/video/video.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // AUDIO: Boards of Canada style ambient
    // =========================================================================

    // Clock - slow, hypnotic tempo
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 85.0f;  // BoC typically 80-100 BPM
    clock.swing = 0.1f; // Slight human feel

    // --- DRUMS: Dusty, understated ---

    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.steps = 16;
    kickSeq.pattern = 0b1000100010001000;  // Four on floor, sparse
    kickSeq.clockInput(&clock);

    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 55.0f;
    kick.decay = 0.4f;
    kick.drive = 0.3f;  // Slight warmth
    kick.triggerInput(&kickSeq);

    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.steps = 16;
    snareSeq.pattern = 0b0000100000001000;  // Off-beat snare
    snareSeq.clockInput(&clock);

    auto& snare = chain.add<Snare>("snare");
    snare.tone = 0.3f;  // Less snappy, more body
    snare.decay = 0.25f;
    snare.triggerInput(&snareSeq);

    auto& hatSeq = chain.add<Euclidean>("hatSeq");
    hatSeq.steps = 16;
    hatSeq.fills = 5;  // Sparse, irregular
    hatSeq.clockInput(&clock);

    auto& hihat = chain.add<HiHat>("hihat");
    hihat.decay = 0.05f;
    hihat.tone = 0.6f;
    hihat.triggerInput(&hatSeq);

    // --- BASS: Warm, filtered ---

    auto& bassSeq = chain.add<Sequencer>("bassSeq");
    bassSeq.steps = 16;
    bassSeq.pattern = 0b1000001010000010;
    bassSeq.clockInput(&clock);

    auto& bassOsc = chain.add<Oscillator>("bassOsc");
    bassOsc.waveform = Waveform::Saw;
    bassOsc.frequency = 55.0f;  // A1

    auto& bassEnv = chain.add<Envelope>("bassEnv");
    bassEnv.attack = 0.01f;
    bassEnv.decay = 0.3f;
    bassEnv.sustain = 0.6f;
    bassEnv.release = 0.2f;
    bassEnv.triggerInput(&bassSeq);

    auto& bassFilter = chain.add<AudioFilter>("bassFilter");
    bassFilter.type = FilterType::Lowpass;
    bassFilter.cutoff = 400.0f;
    bassFilter.resonance = 2.0f;
    bassFilter.input(&bassOsc);

    auto& bassVCA = chain.add<AudioGain>("bassVCA");
    bassVCA.input(&bassFilter);

    // --- LEAD: Detuned, nostalgic ---
    // (Manual detuning to simulate PolySynth - would be cleaner with PolySynth)

    auto& leadOsc1 = chain.add<Oscillator>("leadOsc1");
    leadOsc1.waveform = Waveform::Square;
    leadOsc1.frequency = 440.0f;
    leadOsc1.detune = -8.0f;  // Slightly flat
    leadOsc1.volume = 0.3f;

    auto& leadOsc2 = chain.add<Oscillator>("leadOsc2");
    leadOsc2.waveform = Waveform::Square;
    leadOsc2.frequency = 440.0f;
    leadOsc2.detune = 8.0f;   // Slightly sharp
    leadOsc2.volume = 0.3f;

    auto& leadMix = chain.add<AudioMixer>("leadMix");
    leadMix.addInput(&leadOsc1);
    leadMix.addInput(&leadOsc2);

    auto& leadFilter = chain.add<AudioFilter>("leadFilter");
    leadFilter.type = FilterType::Lowpass;
    leadFilter.cutoff = 2000.0f;
    leadFilter.resonance = 3.0f;
    leadFilter.input(&leadMix);

    // --- ATMOSPHERICS: Crackle + reverb ---

    auto& crackle = chain.add<Crackle>("crackle");
    crackle.density = 0.02f;  // Sparse vinyl crackle
    crackle.volume = 0.15f;

    // --- FX CHAIN ---

    auto& drumMix = chain.add<AudioMixer>("drumMix");
    drumMix.addInput(&kick, 0.8f);
    drumMix.addInput(&snare, 0.5f);
    drumMix.addInput(&hihat, 0.3f);

    auto& mainMix = chain.add<AudioMixer>("mainMix");
    mainMix.addInput(&drumMix, 0.7f);
    mainMix.addInput(&bassVCA, 0.6f);
    mainMix.addInput(&leadFilter, 0.4f);
    mainMix.addInput(&crackle, 1.0f);

    // Lo-fi processing
    auto& bitcrush = chain.add<Bitcrush>("bitcrush");
    bitcrush.bits = 12;           // Subtle digital degradation
    bitcrush.sampleRate = 22050;  // Half sample rate
    bitcrush.mix = 0.3f;          // Blend with clean
    bitcrush.input(&mainMix);

    // Warm saturation
    auto& overdrive = chain.add<Overdrive>("overdrive");
    overdrive.drive = 1.5f;
    overdrive.tone = 0.4f;  // Dark
    overdrive.mix = 0.4f;
    overdrive.input(&bitcrush);

    // Delay - eighth note, lots of feedback
    auto& delay = chain.add<Delay>("delay");
    delay.time = 60000.0f / 85.0f / 2.0f;  // Eighth note at 85 BPM
    delay.feedback = 0.5f;
    delay.mix = 0.3f;
    delay.input(&overdrive);

    // Lush reverb
    auto& reverb = chain.add<Reverb>("reverb");
    reverb.roomSize = 0.85f;
    reverb.damping = 0.6f;
    reverb.wet = 0.4f;
    reverb.input(&delay);

    // Master compression
    auto& compressor = chain.add<Compressor>("compressor");
    compressor.threshold = -12.0f;
    compressor.ratio = 3.0f;
    compressor.attack = 0.01f;
    compressor.release = 0.1f;
    compressor.input(&reverb);

    chain.audioOutput("compressor");

    // --- ANALYSIS for visuals ---

    auto& bands = chain.add<BandSplit>("bands");
    bands.input(&mainMix);

    auto& beat = chain.add<BeatDetect>("beat");
    beat.input(&kick);

    // =========================================================================
    // VISUALS: Prelinger Archive nostalgia
    // =========================================================================

    // Video from Prelinger Archive (user provides their own footage)
    // Example: educational film, industrial footage, home movies
    auto& video = chain.add<VideoPlayer>("video");
    video.path = "assets/prelinger_footage.mp4";
    video.loop = true;
    video.playbackSpeed = 0.8f;  // Slightly slowed for dreaminess

    // Desaturate and warm color grade
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input(&video);
    hsv.saturation = 0.6f;  // Faded
    hsv.hueShift = 0.05f;   // Warm shift toward sepia
    hsv.value = 0.95f;

    // Brightness/contrast for aged look
    auto& brightness = chain.add<Brightness>("brightness");
    brightness.input(&hsv);
    brightness.brightness = -0.05f;
    brightness.contrast = 0.85f;
    brightness.gamma = 1.1f;  // Lifted blacks

    // Subtle color quantization for vintage film stock
    auto& quantize = chain.add<Quantize>("quantize");
    quantize.input(&brightness);
    quantize.levels = 32;  // Subtle banding

    // Audio-reactive bloom (bass drives glow)
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(&quantize);
    bloom.threshold = 0.6f;
    bloom.radius = 20.0f;

    // Audio-reactive feedback (beat drives trails)
    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input(&bloom);
    feedback.decay = 0.92f;
    feedback.zoom = 1.005f;

    // Scanlines for CRT feel
    auto& scanlines = chain.add<Scanlines>("scanlines");
    scanlines.input(&feedback);
    scanlines.spacing = 3;
    scanlines.thickness = 0.4f;
    scanlines.intensity = 0.3f;

    // CRT curve and vignette
    auto& crt = chain.add<CRTEffect>("crt");
    crt.input(&scanlines);
    crt.curvature = 0.08f;
    crt.vignette = 0.4f;
    crt.chromatic = 0.015f;

    // Noise overlay for film grain (workaround until FilmGrain operator)
    auto& grain = chain.add<Noise>("grain");
    grain.type = NoiseType::Value;  // Blocky, like film grain
    grain.scale = 200.0f;
    grain.speed = 30.0f;
    grain.octaves = 1;

    auto& grainBrightness = chain.add<Brightness>("grainBrightness");
    grainBrightness.input(&grain);
    grainBrightness.brightness = 0.5f;  // Center around gray
    grainBrightness.contrast = 0.3f;    // Low contrast

    // Composite grain over video
    auto& composite = chain.add<Composite>("composite");
    composite.base(&crt);
    composite.overlay(&grainBrightness);
    composite.mode = BlendMode::Overlay;
    composite.opacity = 0.15f;  // Subtle grain

    chain.output("composite");

    // =========================================================================
    // MODULATION (LFO for evolving textures)
    // =========================================================================

    auto& filterLFO = chain.add<LFO>("filterLFO");
    filterLFO.rate = 0.05f;  // Very slow - 20 second cycle
    filterLFO.shape = LFOShape::Sine;

    auto& pitchLFO = chain.add<LFO>("pitchLFO");
    pitchLFO.rate = 0.1f;
    pitchLFO.shape = LFOShape::Triangle;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = ctx.time();

    // --- AUDIO MODULATION ---

    // Slow filter sweep on lead
    float filterMod = chain.get<LFO>("filterLFO").outputValue();
    chain.get<AudioFilter>("leadFilter").cutoff = 800.0f + filterMod * 1500.0f;

    // Subtle pitch drift on lead (simulates tape wow)
    // NOTE: Would be cleaner with TapeEffect operator
    float pitchMod = chain.get<LFO>("pitchLFO").outputValue();
    float baseFreq = 440.0f;
    chain.get<Oscillator>("leadOsc1").frequency = baseFreq * (1.0f + pitchMod * 0.01f);
    chain.get<Oscillator>("leadOsc2").frequency = baseFreq * (1.0f - pitchMod * 0.01f);

    // Slow filter on bass
    float bassFilterMod = sin(time * 0.3f);
    chain.get<AudioFilter>("bassFilter").cutoff = 300.0f + bassFilterMod * 200.0f;

    // Envelope follower for bass VCA
    auto& bassEnv = chain.get<Envelope>("bassEnv");
    chain.get<AudioGain>("bassVCA").gain = bassEnv.outputValue();

    // --- VISUAL MODULATION (audio-reactive) ---

    auto& bands = chain.get<BandSplit>("bands");
    auto& beat = chain.get<BeatDetect>("beat");

    float bass = bands.bass();
    float mid = bands.mid();
    float high = bands.high();
    float energy = beat.energy();

    // Bass drives bloom intensity
    chain.get<Bloom>("bloom").intensity = 0.5f + bass * 2.0f;

    // Beat energy drives feedback
    chain.get<Feedback>("feedback").decay = 0.88f + energy * 0.08f;
    chain.get<Feedback>("feedback").zoom = 1.002f + energy * 0.005f;

    // Mids drive chromatic aberration
    chain.get<CRTEffect>("crt").chromatic = 0.01f + mid * 0.02f;

    // High frequencies drive grain intensity
    chain.get<Composite>("composite").opacity = 0.1f + high * 0.1f;

    // Slow video speed modulation
    float speedMod = sin(time * 0.1f);
    chain.get<VideoPlayer>("video").playbackSpeed = 0.7f + speedMod * 0.2f;

    // Subtle hue shift over time
    chain.get<HSV>("hsv").hueShift = 0.05f + sin(time * 0.05f) * 0.03f;

    chain.process();
}

VIVID_CHAIN(setup, update)
```

### What This Example Demonstrates

**Working Today:**
- Layered drum pattern with Euclidean rhythms
- Detuned oscillators (manual, two oscillators)
- Filter modulation via LFO
- Lo-fi chain (Bitcrush → Overdrive → Delay → Reverb)
- Vinyl crackle atmosphere
- Audio-reactive visuals (BandSplit → Bloom, Feedback)
- CRT aesthetic (scanlines, barrel distortion, chromatic aberration)
- Noise as stand-in for film grain

**Would Be Better With:**
- `TapeEffect` - Real wow/flutter instead of LFO pitch modulation
- `PolySynth` - Proper chord pads instead of layered oscillators
- `FilmGrain` - Authentic film grain instead of noise composite
- `Granular` - Textural atmosphere layers
- Parameter binding - `filter.cutoff.bind(lfo)` instead of update() code

---

## Part 5: Recommended Approach

### Philosophy: Code-First Parity

The goal is to make audio creation as expressive and immediate as visual creation—all through code. Every audio operator should be:

1. **Understandable** - Users can read the source and learn
2. **Modifiable** - Parameters exposed, internals accessible
3. **Composable** - Chains together with visual operators
4. **Native** - No external dependencies or plugin GUIs

### Implementation Phases

#### Phase 1: Foundation (2 weeks) ✅ COMPLETE
| Feature | Effort | Impact | Status |
|---------|--------|--------|--------|
| TapeEffect | 3-4 days | Defines vintage/lo-fi character | ✅ Complete |
| FilmGrain | 1 day | Visual complement to tape | ✅ Complete |
| PolySynth | 1 week | Enables chords and pads | ✅ Complete |

#### Phase 2: Workflow (2 weeks) ✅ COMPLETE
| Feature | Effort | Impact | Status |
|---------|--------|--------|--------|
| Parameter binding | 1 week | Reduces update() boilerplate | ✅ Complete |
| Trigger callbacks | 3-5 days | Sequencer → visual events | ✅ Complete |
| Flash operator | 1 day | Beat-synced visuals | ✅ Complete |

#### Phase 3: Synthesis Depth (4 weeks)
| Feature | Effort | Impact | Status |
|---------|--------|--------|--------|
| WavetableSynth | 2 weeks | Evolving, complex timbres | ✅ Complete |
| Granular | 2 weeks | Atmospheric textures | Pending |

#### Phase 4: Advanced (4 weeks)
| Feature | Effort | Impact |
|---------|--------|--------|
| FMSynth | 2 weeks | Classic digital sounds |
| Advanced filters (Ladder, Comb) | 1 week | More sonic variety |
| Song structure | 1 week | Section-based composition |

### Quick Wins (This Week)
1. Create `prelinger-nostalgia` example from case study
2. Document bidirectional patterns in RECIPES.md
3. Start TapeEffect implementation

### Audio-Visual Operator Pairs

For conceptual symmetry, we're building matching capabilities:

| Visual | Audio | Purpose |
|--------|-------|---------|
| CRTEffect | TapeEffect | Vintage character |
| FilmGrain | Crackle | Surface texture |
| Noise | Oscillator | Generation |
| Feedback | Delay | Temporal recursion |
| Bloom | Reverb | Diffusion/space |
| Particles | Granular | Discrete elements |
| PBR Materials | WavetableSynth | Rich, layered textures |

---

## Part 6: Key Files for Implementation

| Feature | Primary Files |
|---------|---------------|
| TapeEffect | `addons/vivid-audio/include/vivid/audio/tape_effect.h` (new) |
| PolySynth | `addons/vivid-audio/include/vivid/audio/poly_synth.h` (new) |
| WavetableSynth | `addons/vivid-audio/include/vivid/audio/wavetable_synth.h` (new) |
| Granular | `addons/vivid-audio/include/vivid/audio/granular.h` (new) |
| FMSynth | `addons/vivid-audio/include/vivid/audio/fm_synth.h` (new) |
| FilmGrain | `core/include/vivid/effects/film_grain.h` (new) |
| Flash | `core/include/vivid/effects/flash.h` (new) |
| Parameter binding | `core/include/vivid/param.h` (modify) |
| Trigger routing | `addons/vivid-audio/include/vivid/audio/sequencer.h` (modify) |
| Templates | `examples/audiovisual/*/chain.cpp` (new) |

---

## Appendix: CLAP Plugin Hosting (Future Option)

While native synthesis is the priority, CLAP plugin hosting remains a future option for users who want access to commercial plugins they already own. This would be implemented as an optional addon (`vivid-clap`) that doesn't affect the core workflow.

Key considerations:
- Would only be used for effects/instruments users specifically request
- Plugin GUIs would be optional (parameter control via code preferred)
- Native operators would always be documented first in examples

This is explicitly de-prioritized because:
1. External plugins create distance between creator and creation
2. Plugin UIs break the code-first workflow
3. Users can't read/modify plugin source to learn
4. Dependency on third-party software reduces portability

If implemented later, it would supplement—not replace—native synthesis.
