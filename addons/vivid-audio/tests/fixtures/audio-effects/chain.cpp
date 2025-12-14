// Audio Effects Demo - Vivid Example
// Demonstrates the vivid-audio addon with various audio effects
// Controls:
//   1-4: Switch between audio files
//   M: Toggle Microphone input
//   D: Toggle Delay effect
//   R: Toggle Reverb effect
//   C: Toggle Compressor effect
//   O: Toggle Overdrive effect
//   B: Toggle Bitcrush effect
//   SPACE: Pause/Play (file mode only)
//   TAB: Open parameter controls

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
static bool useMic = false;

void printStatus(Chain& chain) {
    auto& delay = chain.get<Delay>("delay");
    auto& reverb = chain.get<Reverb>("reverb");
    auto& comp = chain.get<Compressor>("comp");
    auto& overdrive = chain.get<Overdrive>("overdrive");
    auto& bitcrush = chain.get<Bitcrush>("bitcrush");

    std::cout << "\n[Audio Effects] Source: " << (useMic ? "[M]icrophone" : audioFiles[currentFileIndex]) << std::endl;
    std::cout << "Effects: "
              << (!delay.isBypassed() ? "[D]elay " : "delay ")
              << (!reverb.isBypassed() ? "[R]everb " : "reverb ")
              << (!comp.isBypassed() ? "[C]ompressor " : "compressor ")
              << (!overdrive.isBypassed() ? "[O]verdrive " : "overdrive ")
              << (!bitcrush.isBypassed() ? "[B]itcrush " : "bitcrush ")
              << std::endl;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio sources - file and microphone
    auto& audioFile = chain.add<AudioFile>("audioFile");
    audioFile.file(audioFiles[currentFileIndex]).loop(true).volume(0.8f);

    auto& mic = chain.add<AudioIn>("mic");
    mic.volume(1.0f).mute(true);  // Start muted

    // Effects chain - delay takes input from file by default
    auto& delay = chain.add<Delay>("delay");
    delay.input("audioFile").delayTime(300).feedback(0.4f).mix(0.5f);

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

    // Set initial bypass states (effects start enabled except comp/overdrive/bitcrush)
    delay.bypass(false);
    reverb.bypass(false);
    comp.bypass(true);
    overdrive.bypass(true);
    bitcrush.bypass(true);

    std::cout << "\n========================================" << std::endl;
    std::cout << "Audio Effects Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  1-4: Switch audio files" << std::endl;
    std::cout << "  M: Toggle Microphone input" << std::endl;
    std::cout << "  D: Toggle Delay" << std::endl;
    std::cout << "  R: Toggle Reverb" << std::endl;
    std::cout << "  C: Toggle Compressor" << std::endl;
    std::cout << "  O: Toggle Overdrive" << std::endl;
    std::cout << "  B: Toggle Bitcrush" << std::endl;
    std::cout << "  SPACE: Pause/Play (file mode)" << std::endl;
    std::cout << "  TAB: Open parameter controls" << std::endl;
    std::cout << "========================================\n" << std::endl;

    printStatus(chain);
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& audioFile = chain.get<AudioFile>("audioFile");
    auto& mic = chain.get<AudioIn>("mic");
    auto& delay = chain.get<Delay>("delay");
    auto& reverb = chain.get<Reverb>("reverb");
    auto& comp = chain.get<Compressor>("comp");
    auto& overdrive = chain.get<Overdrive>("overdrive");
    auto& bitcrush = chain.get<Bitcrush>("bitcrush");
    auto& noise = chain.get<Noise>("noise");

    // M key - toggle microphone input
    if (ctx.key(GLFW_KEY_M).pressed) {
        useMic = !useMic;
        if (useMic) {
            // Switch to mic: mute file, unmute mic, reconnect delay to mic
            audioFile.pause();
            mic.mute(false);
            delay.input("mic");
            std::cout << "[Audio] Switched to MICROPHONE" << std::endl;
        } else {
            // Switch to file: mute mic, unmute file, reconnect delay to file
            mic.mute(true);
            delay.input("audioFile");
            audioFile.play();
            std::cout << "[Audio] Switched to FILE" << std::endl;
        }
        printStatus(chain);
    }

    // Number keys - switch audio files (only when not using mic)
    if (!useMic) {
        for (int i = 0; i < std::min((int)audioFiles.size(), 4); i++) {
            if (ctx.key(GLFW_KEY_1 + i).pressed && i != currentFileIndex) {
                currentFileIndex = i;
                audioFile.file(audioFiles[currentFileIndex]);
                printStatus(chain);
            }
        }
    }

    // Effect toggles - use operator bypass directly
    if (ctx.key(GLFW_KEY_D).pressed) {
        delay.bypass(!delay.isBypassed());
        printStatus(chain);
    }
    if (ctx.key(GLFW_KEY_R).pressed) {
        reverb.bypass(!reverb.isBypassed());
        printStatus(chain);
    }
    if (ctx.key(GLFW_KEY_C).pressed) {
        comp.bypass(!comp.isBypassed());
        printStatus(chain);
    }
    if (ctx.key(GLFW_KEY_O).pressed) {
        overdrive.bypass(!overdrive.isBypassed());
        printStatus(chain);
    }
    if (ctx.key(GLFW_KEY_B).pressed) {
        bitcrush.bypass(!bitcrush.isBypassed());
        printStatus(chain);
    }

    // Space - pause/play (file mode only)
    if (!useMic && ctx.key(GLFW_KEY_SPACE).pressed) {
        if (audioFile.isPlaying()) {
            audioFile.pause();
            std::cout << "[Audio] PAUSED" << std::endl;
        } else {
            audioFile.play();
            std::cout << "[Audio] PLAYING" << std::endl;
        }
    }

    // Animate visual noise
    float time = ctx.time();
    noise.speed(0.5f);
    noise.scale(30.0f + std::sin(time * 2.0f) * 20.0f);
}

VIVID_CHAIN(setup, update)
