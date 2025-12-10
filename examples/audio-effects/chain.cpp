// Audio Effects Demo - Vivid Example
// Demonstrates the vivid-audio addon with various audio effects
// Controls:
//   1-4: Switch between audio files
//   D: Toggle Delay effect
//   R: Toggle Reverb effect
//   C: Toggle Compressor effect
//   O: Toggle Overdrive effect
//   B: Toggle Bitcrush effect
//   SPACE: Pause/Play
//   Mouse X: Control effect intensity

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <iostream>
#include <vector>
#include <string>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// Audio files
static const std::vector<std::string> audioFiles = {
    "assets/audio/836863__josefpres__piano-loops-197-octave-short-loop-120-bpm.wav",
    "assets/audio/836911__josefpres__piano-loops-197-octave-down-short-loop-120-bpm.wav",
    "assets/audio/837025__josefpres__piano-loops-197-octave-up-short-loop-120-bpm.wav",
    "assets/audio/file_example_WAV_5MG.wav",
};

static int currentFileIndex = 0;

// Effect toggle states
static bool delayEnabled = true;
static bool reverbEnabled = true;
static bool compEnabled = false;
static bool overdriveEnabled = false;
static bool bitcrushEnabled = false;

void printStatus() {
    std::cout << "\n[Audio Effects] Current file: " << audioFiles[currentFileIndex] << std::endl;
    std::cout << "Effects: "
              << (delayEnabled ? "[D]elay " : "delay ")
              << (reverbEnabled ? "[R]everb " : "reverb ")
              << (compEnabled ? "[C]ompressor " : "compressor ")
              << (overdriveEnabled ? "[O]verdrive " : "overdrive ")
              << (bitcrushEnabled ? "[B]itcrush " : "bitcrush ")
              << std::endl;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio source - load WAV file
    auto& audio = chain.add<AudioFile>("audio");
    audio.file(audioFiles[currentFileIndex]).loop(true).volume(0.8f);

    // Effects chain
    auto& delay = chain.add<Delay>("delay");
    delay.input("audio").delayTime(300).feedback(0.4f).mix(0.5f);

    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("delay").roomSize(0.7f).damping(0.3f).mix(0.4f);

    auto& comp = chain.add<Compressor>("comp");
    comp.input("reverb").threshold(-18).ratio(4).attack(10).release(100);

    auto& overdrive = chain.add<Overdrive>("overdrive");
    overdrive.input("comp").drive(2.0f).tone(0.5f).level(0.7f);

    auto& bitcrush = chain.add<Bitcrush>("bitcrush");
    bitcrush.input("overdrive").bits(12).sampleRate(22050);

    // Audio output
    auto& out = chain.add<AudioOutput>("out");
    out.input("bitcrush").volume(0.8f);

    // Visual feedback - simple waveform-inspired display
    auto& gradient = chain.add<Gradient>("bg");
    gradient.colorA(0.1f, 0.1f, 0.2f).colorB(0.05f, 0.1f, 0.15f);

    auto& noise = chain.add<Noise>("noise");
    noise.scale(50.0f).speed(0.5f);

    auto& composite = chain.add<Composite>("vis");
    composite.inputA(&gradient).inputB(&noise).mode(BlendMode::Add).opacity(0.3f);

    // Set outputs
    chain.output("vis");
    chain.audioOutput("out");

    // Set initial bypass states
    delay.bypass(!delayEnabled);
    reverb.bypass(!reverbEnabled);
    comp.bypass(!compEnabled);
    overdrive.bypass(!overdriveEnabled);
    bitcrush.bypass(!bitcrushEnabled);

    std::cout << "\n========================================" << std::endl;
    std::cout << "Audio Effects Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  1-4: Switch audio files" << std::endl;
    std::cout << "  D: Toggle Delay" << std::endl;
    std::cout << "  R: Toggle Reverb" << std::endl;
    std::cout << "  C: Toggle Compressor" << std::endl;
    std::cout << "  O: Toggle Overdrive" << std::endl;
    std::cout << "  B: Toggle Bitcrush" << std::endl;
    std::cout << "  SPACE: Pause/Play" << std::endl;
    std::cout << "  Mouse X: Effect intensity" << std::endl;
    std::cout << "========================================\n" << std::endl;

    printStatus();
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& audio = chain.get<AudioFile>("audio");
    auto& delay = chain.get<Delay>("delay");
    auto& reverb = chain.get<Reverb>("reverb");
    auto& comp = chain.get<Compressor>("comp");
    auto& overdrive = chain.get<Overdrive>("overdrive");
    auto& bitcrush = chain.get<Bitcrush>("bitcrush");
    auto& noise = chain.get<Noise>("noise");

    // Number keys - switch audio files
    for (int i = 0; i < std::min((int)audioFiles.size(), 4); i++) {
        if (ctx.key(GLFW_KEY_1 + i).pressed && i != currentFileIndex) {
            currentFileIndex = i;
            audio.file(audioFiles[currentFileIndex]);
            printStatus();
        }
    }

    // Effect toggles
    if (ctx.key(GLFW_KEY_D).pressed) {
        delayEnabled = !delayEnabled;
        delay.bypass(!delayEnabled);
        printStatus();
    }
    if (ctx.key(GLFW_KEY_R).pressed) {
        reverbEnabled = !reverbEnabled;
        reverb.bypass(!reverbEnabled);
        printStatus();
    }
    if (ctx.key(GLFW_KEY_C).pressed) {
        compEnabled = !compEnabled;
        comp.bypass(!compEnabled);
        printStatus();
    }
    if (ctx.key(GLFW_KEY_O).pressed) {
        overdriveEnabled = !overdriveEnabled;
        overdrive.bypass(!overdriveEnabled);
        printStatus();
    }
    if (ctx.key(GLFW_KEY_B).pressed) {
        bitcrushEnabled = !bitcrushEnabled;
        bitcrush.bypass(!bitcrushEnabled);
        printStatus();
    }

    // Space - pause/play
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        if (audio.isPlaying()) {
            audio.pause();
            std::cout << "[Audio] PAUSED" << std::endl;
        } else {
            audio.play();
            std::cout << "[Audio] PLAYING" << std::endl;
        }
    }

    // Mouse X controls effect intensity
    float intensity = ctx.mouseNorm().x;

    // Modulate effect parameters based on mouse position
    if (delayEnabled) {
        delay.feedback(0.2f + intensity * 0.5f);
        delay.mix(0.2f + intensity * 0.5f);
    }
    if (reverbEnabled) {
        reverb.roomSize(0.3f + intensity * 0.6f);
        reverb.mix(0.2f + intensity * 0.4f);
    }
    if (overdriveEnabled) {
        overdrive.drive(1.0f + intensity * 5.0f);
    }
    if (bitcrushEnabled) {
        // Lower bits = more crushed
        int bits = 16 - static_cast<int>(intensity * 12);
        bitcrush.bits(bits);
    }

    // Animate visual noise based on audio playback
    float time = ctx.time();
    noise.speed(0.3f + intensity * 0.7f);
    noise.scale(30.0f + std::sin(time * 2.0f) * 20.0f);
}

VIVID_CHAIN(setup, update)
