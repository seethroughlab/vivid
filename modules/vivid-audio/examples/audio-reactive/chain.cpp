// Audio-Reactive Demo - Vivid Example
// Demonstrates audio analysis driving visual effects
// Controls:
//   M: Toggle Microphone/File input
//   1-3: Switch audio files
//   SPACE: Pause/Play (file mode only)
//   TAB: Open parameter controls

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// Audio files
static const std::vector<std::string> audioFiles = {
    "tests/assets/audio/836863__josefpres__piano-loops-197-octave-short-loop-120-bpm.wav",
    "tests/assets/audio/836911__josefpres__piano-loops-197-octave-down-short-loop-120-bpm.wav",
    "tests/assets/audio/file_example_WAV_5MG.wav",
};

static int currentFileIndex = 0;
static bool useMic = false;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Audio Sources
    // =========================================================================

    auto& audioFile = chain.add<AudioFile>("audioFile");
    audioFile.setFile(audioFiles[currentFileIndex]);
    audioFile.loop(true);
    audioFile.volume = 0.8f;

    auto& mic = chain.add<AudioIn>("mic");
    mic.volume = 1.0f;
    mic.setMute(true);

    // =========================================================================
    // Audio Analysis
    // =========================================================================

    // Levels - overall amplitude
    auto& levels = chain.add<Levels>("levels");
    levels.input("audioFile");
    levels.smoothing = 0.85f;

    // BandSplit - frequency bands (bass, mids, highs)
    auto& bands = chain.add<BandSplit>("bands");
    bands.input("audioFile");
    bands.smoothing = 0.9f;

    // BeatDetect - onset detection
    auto& beat = chain.add<BeatDetect>("beat");
    beat.input("audioFile");
    beat.sensitivity = 1.5f;
    beat.decay = 0.92f;

    // FFT - full spectrum
    auto& fft = chain.add<FFT>("fft");
    fft.input("audioFile");
    fft.setSize(512);
    fft.smoothing = 0.7f;

    // =========================================================================
    // Audio Output
    // =========================================================================

    auto& out = chain.add<AudioOutput>("out");
    out.setInput("audioFile");
    out.setVolume(0.8f);
    chain.audioOutput("out");

    // =========================================================================
    // Visual Effects
    // =========================================================================

    // Background gradient - color shifts with bass
    auto& gradient = chain.add<Gradient>("bg");
    gradient.colorA.set(0.05f, 0.02f, 0.1f, 1.0f);
    gradient.colorB.set(0.02f, 0.05f, 0.08f, 1.0f);

    // Noise layer - reacts to mids
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 20.0f;
    noise.speed = 0.5f;
    noise.octaves = 4;

    // Shape - pulses with beat
    auto& shape = chain.add<Shape>("shape");
    shape.type = ShapeType::Circle;
    shape.size.set(0.3f, 0.3f);
    shape.softness = 0.05f;
    shape.color.set(1.0f, 0.84f, 0.0f, 1.0f);  // Gold

    // Composite layers
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("bg");
    comp1.inputB("noise");
    comp1.mode = BlendMode::Add;
    comp1.opacity = 0.3f;

    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("shape");
    comp2.mode = BlendMode::Add;
    comp2.opacity = 1.0f;

    // Final bloom for glow effect
    auto& bloom = chain.add<Blur>("bloom");
    bloom.input("comp2");
    bloom.radius = 8.0f;

    auto& final_ = chain.add<Composite>("final");
    final_.inputA("comp2");
    final_.inputB("bloom");
    final_.mode = BlendMode::Add;
    final_.opacity = 0.4f;

    chain.output("final");

    // =========================================================================
    // Console Output
    // =========================================================================

    std::cout << "\n========================================" << std::endl;
    std::cout << "Audio-Reactive Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  M: Toggle Microphone/File input" << std::endl;
    std::cout << "  1-3: Switch audio files" << std::endl;
    std::cout << "  SPACE: Pause/Play (file mode)" << std::endl;
    std::cout << "  TAB: Open parameter controls" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Get operators
    auto& audioFile = chain.get<AudioFile>("audioFile");
    auto& mic = chain.get<AudioIn>("mic");
    auto& levels = chain.get<Levels>("levels");
    auto& bands = chain.get<BandSplit>("bands");
    auto& beat = chain.get<BeatDetect>("beat");
    auto& fft = chain.get<FFT>("fft");

    auto& gradient = chain.get<Gradient>("bg");
    auto& noise = chain.get<Noise>("noise");
    auto& shape = chain.get<Shape>("shape");
    auto& bloom = chain.get<Blur>("bloom");
    auto& comp1 = chain.get<Composite>("comp1");

    // =========================================================================
    // Input Controls
    // =========================================================================

    // M key - toggle mic/file
    if (ctx.key(GLFW_KEY_M).pressed) {
        useMic = !useMic;
        if (useMic) {
            audioFile.pause();
            mic.setMute(false);
            levels.input("mic");
            bands.input("mic");
            beat.input("mic");
            fft.input("mic");
            chain.get<AudioOutput>("out").setInput("mic");
            std::cout << "[Audio] Switched to MICROPHONE" << std::endl;
        } else {
            mic.setMute(true);
            levels.input("audioFile");
            bands.input("audioFile");
            beat.input("audioFile");
            fft.input("audioFile");
            chain.get<AudioOutput>("out").setInput("audioFile");
            audioFile.play();
            std::cout << "[Audio] Switched to FILE" << std::endl;
        }
    }

    // Number keys - switch files
    if (!useMic) {
        for (int i = 0; i < std::min(static_cast<int>(audioFiles.size()), 3); i++) {
            if (ctx.key(GLFW_KEY_1 + i).pressed && i != currentFileIndex) {
                currentFileIndex = i;
                audioFile.setFile(audioFiles[currentFileIndex]);
                std::cout << "[Audio] Switched to: " << audioFiles[currentFileIndex] << std::endl;
            }
        }
    }

    // Space - pause/play
    if (!useMic && ctx.key(GLFW_KEY_SPACE).pressed) {
        if (audioFile.isPlaying()) {
            audioFile.pause();
            std::cout << "[Audio] PAUSED" << std::endl;
        } else {
            audioFile.play();
            std::cout << "[Audio] PLAYING" << std::endl;
        }
    }

    // =========================================================================
    // Audio-Reactive Visuals
    // =========================================================================

    // Get analysis values
    float rms = levels.rms();
    float bass = bands.bass();
    float subBass = bands.subBass();
    float mid = bands.mid();
    float high = bands.high();
    bool isBeat = beat.beat();
    float beatIntensity = beat.intensity();
    float energy = beat.energy();

    // Background color shifts with bass
    float r = 0.05f + bass * 0.2f;
    float g = 0.02f + subBass * 0.1f;
    float b = 0.1f + mid * 0.15f;
    gradient.colorA.set(r, g, b, 1.0f);
    gradient.colorB.set(r * 0.4f, g * 0.8f, b * 0.6f, 1.0f);

    // Noise reacts to mids and highs
    float noiseScale = 15.0f + mid * 30.0f + high * 20.0f;
    float noiseSpeed = 0.3f + energy * 2.0f;
    noise.scale = noiseScale;
    noise.speed = noiseSpeed;
    comp1.opacity = 0.2f + rms * 0.5f;

    // Shape pulses with beat
    float baseSize = 0.2f + rms * 0.2f;
    float beatPulse = isBeat ? 0.3f : beatIntensity * 0.2f;
    shape.size.set(baseSize + beatPulse, baseSize + beatPulse);
    shape.softness = 0.02f + beatIntensity * 0.1f;

    // Shape color shifts with HSV
    float hue = std::fmod(static_cast<float>(ctx.time()) * 0.1f + bass, 1.0f);
    float sat = 0.7f + high * 0.3f;
    float val = 0.8f + beatIntensity * 0.2f;
    // Convert HSV to RGB
    float c = val * sat;
    float x = c * (1.0f - std::abs(std::fmod(hue * 6.0f, 2.0f) - 1.0f));
    float m = val - c;
    float rs, gs, bs;
    int hi = static_cast<int>(hue * 6.0f) % 6;
    switch (hi) {
        case 0: rs = c; gs = x; bs = 0; break;
        case 1: rs = x; gs = c; bs = 0; break;
        case 2: rs = 0; gs = c; bs = x; break;
        case 3: rs = 0; gs = x; bs = c; break;
        case 4: rs = x; gs = 0; bs = c; break;
        default: rs = c; gs = 0; bs = x; break;
    }
    shape.color.set(rs + m, gs + m, bs + m, 1.0f);

    // Bloom radius increases with energy
    bloom.radius = 4.0f + energy * 20.0f;
}

// Large canvas for audio visualizer
VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = true
}))
