// Formant Pad - Vocal Synthesis Example
// A soft pad synth with formant filter that uses the keyboard as a piano
// Keys A-K play D minor scale, vowel changes randomly with each key press
//
// Controls:
//   A S D F G H J K - D minor scale (D3 to D4)
//   UP/DOWN - Adjust resonance
//   LEFT/RIGHT - Adjust reverb mix
//   F - Toggle fullscreen

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <iomanip>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// D minor scale frequencies (D3 to D4) using audio constants
// Minor scale has 7 notes, add the octave for 8 total keys
static const float D_MINOR[] = {
    freq::D3, freq::E3, freq::F3, freq::G3,
    freq::A3, freq::Bb3, freq::C4, freq::D4
};

static const char* NOTE_NAMES[] = {
    "D3", "E3", "F3", "G3", "A3", "Bb3", "C4", "D4"
};

static const char* VOWEL_NAMES[] = {
    "A (ah)", "E (eh)", "I (ee)", "O (oh)", "U (oo)"
};

// Keyboard mapping (piano style on home row)
static const int PIANO_KEYS[] = {
    GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D, GLFW_KEY_F,
    GLFW_KEY_G, GLFW_KEY_H, GLFW_KEY_J, GLFW_KEY_K
};

static float currentFreq = freq::D3;
static int currentVowel = 0;
static float resonance = 8.0f;
static float reverbMix = 0.4f;
static float visualHue = 0.0f;
static float noteDecay = 0.0f;

void printStatus() {
    std::cout << "\r[Formant Pad] Resonance: " << std::fixed << std::setprecision(1)
              << resonance << " | Reverb: " << static_cast<int>(reverbMix * 100) << "% | "
              << "Vowel: " << VOWEL_NAMES[currentVowel] << "          " << std::flush;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Seed random for vowel selection
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    // =========================================================================
    // PAD OSCILLATORS - Lush detuned saw waves
    // =========================================================================

    // Main oscillator - saw wave
    auto& osc1 = chain.add<Oscillator>("osc1");
    osc1.frequency(currentFreq)
        .waveform(Waveform::Saw)
        .volume(0.25f);

    // Detuned oscillator - slightly sharp
    auto& osc2 = chain.add<Oscillator>("osc2");
    osc2.frequency(currentFreq * 1.005f)
        .waveform(Waveform::Saw)
        .volume(0.20f);

    // Detuned oscillator - slightly flat
    auto& osc3 = chain.add<Oscillator>("osc3");
    osc3.frequency(currentFreq * 0.995f)
        .waveform(Waveform::Saw)
        .volume(0.20f);

    // Sub oscillator - one octave down, sine for warmth
    auto& sub = chain.add<Oscillator>("sub");
    sub.frequency(currentFreq * 0.5f)
       .waveform(Waveform::Sine)
       .volume(0.15f);

    // =========================================================================
    // ENVELOPE - Soft pad envelope
    // =========================================================================

    auto& env = chain.add<Envelope>("env");
    env.attack(0.15f)    // Slow attack for pad feel
       .decay(0.2f)
       .sustain(0.7f)
       .release(0.8f);   // Long release for pad

    // =========================================================================
    // MIXING & PROCESSING
    // =========================================================================

    // Mix oscillators
    auto& oscMix = chain.add<AudioMixer>("osc_mix");
    oscMix.input(0, "osc1").gain(0, 1.0f)
          .input(1, "osc2").gain(1, 1.0f)
          .input(2, "osc3").gain(2, 1.0f)
          .input(3, "sub").gain(3, 1.0f);

    // Apply envelope
    auto& enveloped = chain.add<AudioGain>("enveloped");
    enveloped.input("osc_mix");
    enveloped.gainInput("env");

    // =========================================================================
    // FORMANT FILTER - The vocal character
    // =========================================================================

    auto& formant = chain.add<Formant>("formant");
    formant.input("enveloped")
           .vowel(Vowel::A)
           .resonance(resonance)
           .mix(1.0f);

    // =========================================================================
    // EFFECTS - Reverb for space
    // =========================================================================

    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("formant")
          .roomSize(0.85f)
          .damping(0.4f)
          .mix(reverbMix);

    // Master gain
    auto& master = chain.add<AudioGain>("master");
    master.input("reverb");
    master.gain(0.6f);

    // Audio output
    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.input("master").volume(0.8f);

    // Levels for visualization
    auto& levels = chain.add<Levels>("levels");
    levels.input("master");

    chain.audioOutput("audioOut");

    // =========================================================================
    // VISUALIZATION - Vowel-reactive colors
    // =========================================================================

    auto& bg = chain.add<Noise>("bg");
    bg.scale(3.0f).speed(0.05f);

    auto& bgColor = chain.add<HSV>("bg_color");
    bgColor.input(&bg).saturation(0.6f).value(0.15f);

    // Pulsing shape that responds to audio
    auto& pulse = chain.add<Shape>("pulse");
    pulse.type(ShapeType::Circle)
         .size(0.3f)
         .color(1.0f, 0.5f, 0.3f, 0.8f);

    auto& final_ = chain.add<Composite>("final");
    final_.input(0, &bgColor)
          .input(1, &pulse)
          .mode(BlendMode::Add);

    chain.output("final");

    std::cout << "=== FORMANT PAD ===" << std::endl;
    std::cout << "Keys: A S D F G H J K = D minor scale" << std::endl;
    std::cout << "UP/DOWN = Resonance | LEFT/RIGHT = Reverb" << std::endl;
    std::cout << std::endl;
    printStatus();
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& osc1 = chain.get<Oscillator>("osc1");
    auto& osc2 = chain.get<Oscillator>("osc2");
    auto& osc3 = chain.get<Oscillator>("osc3");
    auto& sub = chain.get<Oscillator>("sub");
    auto& env = chain.get<Envelope>("env");
    auto& formant = chain.get<Formant>("formant");
    auto& reverb = chain.get<Reverb>("reverb");
    auto& bgColor = chain.get<HSV>("bg_color");
    auto& pulse = chain.get<Shape>("pulse");
    auto& levels = chain.get<Levels>("levels");

    // Check piano keys
    for (int i = 0; i < 8; ++i) {
        if (ctx.key(PIANO_KEYS[i]).pressed) {
            float freq = D_MINOR[i];
            currentFreq = freq;

            // Update oscillator frequencies
            osc1.frequency(freq);
            osc2.frequency(freq * 1.005f);
            osc3.frequency(freq * 0.995f);
            sub.frequency(freq * 0.5f);

            // Random vowel selection
            int newVowel = std::rand() % 5;
            currentVowel = newVowel;
            formant.vowel(static_cast<Vowel>(newVowel));

            // Trigger envelope
            env.trigger();

            // Visual feedback
            noteDecay = 1.0f;
            visualHue = static_cast<float>(newVowel) / 5.0f;

            std::cout << "\r[" << NOTE_NAMES[i] << "] " << VOWEL_NAMES[newVowel]
                      << "                              " << std::flush;
        }
    }

    // Resonance adjustment
    if (ctx.key(GLFW_KEY_UP).pressed) {
        resonance = std::min(resonance + 1.0f, 20.0f);
        formant.resonance(resonance);
        printStatus();
    }
    if (ctx.key(GLFW_KEY_DOWN).pressed) {
        resonance = std::max(resonance - 1.0f, 1.0f);
        formant.resonance(resonance);
        printStatus();
    }

    // Reverb adjustment
    if (ctx.key(GLFW_KEY_RIGHT).pressed) {
        reverbMix = std::min(reverbMix + 0.1f, 1.0f);
        reverb.mix(reverbMix);
        printStatus();
    }
    if (ctx.key(GLFW_KEY_LEFT).pressed) {
        reverbMix = std::max(reverbMix - 0.1f, 0.0f);
        reverb.mix(reverbMix);
        printStatus();
    }

    // Toggle fullscreen
    if (ctx.key(GLFW_KEY_F).pressed) {
        ctx.fullscreen(!ctx.fullscreen());
    }

    // Update visuals
    noteDecay *= 0.95f;

    // Hue shifts based on current vowel
    float hueOffset = visualHue + ctx.time() * 0.02f;
    bgColor.hueShift(std::fmod(hueOffset, 1.0f));

    // Pulse size based on audio level
    float level = levels.rms();
    pulse.size(0.15f + level * 0.3f);

    // Pulse color based on vowel (warm colors for back vowels, cool for front)
    float r = (currentVowel == 0 || currentVowel == 3 || currentVowel == 4) ? 1.0f : 0.4f;
    float g = 0.3f + noteDecay * 0.5f;
    float b = (currentVowel == 1 || currentVowel == 2) ? 1.0f : 0.4f;
    pulse.color(r, g, b, 0.7f + noteDecay * 0.3f);
}

VIVID_CHAIN(setup, update)
