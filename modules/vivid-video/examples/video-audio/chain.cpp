// Video Audio - Vivid Example
// Demonstrates: VideoPlayer, VideoAudio - Extract and process audio from video files
//
// Shows how to route video audio through the chain's audio system

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/video/video.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;
using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- VIDEO PLAYBACK -----
    // Load a video file with audio
    auto& video = chain.add<VideoPlayer>("video");
    video.setFile("assets/videos/sample.mov");  // Adjust path to your video
    video.setLoop(true);
    video.play();

    // ----- VIDEO AUDIO EXTRACTION -----
    // VideoAudio extracts audio from VideoPlayer for processing
    auto& videoAudio = chain.add<VideoAudio>("videoAudio");
    videoAudio.setSource("video");  // Connect to video player

    // ----- AUDIO PROCESSING -----
    // Apply effects to the video audio

    // FFT for visualization
    auto& fft = chain.add<FFT>("fft");
    fft.input("videoAudio");
    fft.size(FFTSize::FFT_512);

    // Levels for volume metering
    auto& levels = chain.add<Levels>("levels");
    levels.input("videoAudio");

    // Optional: Add delay effect
    auto& delay = chain.add<Delay>("delay");
    delay.input("videoAudio");
    delay.time = 0.25f;       // 250ms delay
    delay.feedback = 0.3f;    // 30% feedback
    delay.mix = 0.0f;         // Start with no effect (mouse X controls)

    // ----- AUDIO OUTPUT -----
    auto& output = chain.add<AudioOutput>("audioOut");
    output.input("delay");

    // ----- VISUAL CHAIN -----
    // Show video with audio-reactive effects

    // Video texture
    // Transform to fit screen
    auto& videoTransform = chain.add<Transform>("videoTransform");
    videoTransform.input("video");
    videoTransform.scale.set(1.0f, 1.0f);

    // Audio-reactive vignette
    auto& vignette = chain.add<Vignette>("vignette");
    vignette.input("videoTransform");
    vignette.intensity = 0.3f;
    vignette.softness = 0.5f;

    // Bloom based on audio
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("vignette");
    bloom.threshold = 0.7f;
    bloom.intensity = 0.5f;
    bloom.radius = 20.0f;

    // Overlay for audio meters
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);

    auto& comp = chain.add<Composite>("comp");
    comp.inputA("bloom");
    comp.inputB("canvas");
    comp.mode = BlendMode::Add;

    chain.output("comp");
    chain.audioOutput("audioOut");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& video = chain.get<VideoPlayer>("video");
    auto& fft = chain.get<FFT>("fft");
    auto& levels = chain.get<Levels>("levels");
    auto& delay = chain.get<Delay>("delay");
    auto& vignette = chain.get<Vignette>("vignette");
    auto& bloom = chain.get<Bloom>("bloom");
    auto& canvas = chain.get<Canvas>("canvas");

    // ----- PLAYBACK CONTROLS -----
    // Space: Play/Pause
    if (ctx.keyPressed(Key::Space)) {
        if (video.isPlaying()) {
            video.pause();
        } else {
            video.play();
        }
    }

    // R: Restart
    if (ctx.keyPressed(Key::R)) {
        video.restart();
    }

    // Left/Right: Seek
    if (ctx.keyPressed(Key::Left)) {
        float newTime = std::max(0.0f, video.currentTime() - 5.0f);
        video.seek(newTime);
    }
    if (ctx.keyPressed(Key::Right)) {
        float newTime = std::min(video.duration(), video.currentTime() + 5.0f);
        video.seek(newTime);
    }

    // ----- MOUSE CONTROLS -----
    // Mouse X: Delay mix
    float mouseX = ctx.mouseNorm().x;
    delay.mix = mouseX * 0.8f;

    // Mouse Y: Bloom intensity
    float mouseY = ctx.mouseNorm().y;
    bloom.intensity = 0.5f + mouseY * 1.5f;

    // ----- AUDIO-REACTIVE VISUALS -----
    // Get audio levels
    float leftLevel = levels.level(0);
    float rightLevel = levels.level(1);
    float avgLevel = (leftLevel + rightLevel) * 0.5f;

    // Pulse vignette with audio
    vignette.intensity = 0.2f + avgLevel * 0.4f;

    // Bass affects bloom
    float bass = fft.band(0);
    bloom.radius = 15.0f + bass * 30.0f;

    // ----- DRAW AUDIO METERS -----
    canvas.clear(0.0f, 0.0f, 0.0f, 0.0f);

    // Progress bar at bottom
    float progress = video.duration() > 0 ? video.currentTime() / video.duration() : 0.0f;
    canvas.fill(Color(0.3f, 0.3f, 0.3f, 0.8f));
    canvas.rect(50, 680, 1180, 10);  // Background
    canvas.fill(Color(0.2f, 0.6f, 1.0f, 1.0f));
    canvas.rect(50, 680, 1180 * progress, 10);  // Progress

    // Time display
    int mins = static_cast<int>(video.currentTime()) / 60;
    int secs = static_cast<int>(video.currentTime()) % 60;
    int totalMins = static_cast<int>(video.duration()) / 60;
    int totalSecs = static_cast<int>(video.duration()) % 60;

    canvas.fill(Color(1.0f, 1.0f, 1.0f, 0.9f));
    canvas.textSize(16);
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d / %02d:%02d", mins, secs, totalMins, totalSecs);
    canvas.text(timeStr, 50, 660);

    // Playback state
    const char* state = video.isPlaying() ? "Playing" : "Paused";
    canvas.text(state, 200, 660);

    // FFT visualization (spectrum analyzer)
    float barWidth = 10.0f;
    float maxHeight = 150.0f;
    float startX = 50.0f;
    float baseY = 100.0f;

    canvas.noStroke();
    for (int i = 0; i < 32; i++) {
        float val = fft.band(i);
        float height = val * maxHeight;

        // Color gradient based on frequency
        float hue = static_cast<float>(i) / 32.0f;
        canvas.fill(Color(
            0.5f + 0.5f * std::sin(hue * 6.28f),
            0.5f + 0.5f * std::sin(hue * 6.28f + 2.09f),
            0.5f + 0.5f * std::sin(hue * 6.28f + 4.19f),
            0.8f
        ));

        canvas.rect(startX + i * (barWidth + 2), baseY - height, barWidth, height);
    }

    // Level meters (L/R)
    float meterX = 1150.0f;
    float meterHeight = 100.0f;

    // Left channel
    canvas.fill(Color(0.2f, 0.2f, 0.2f, 0.8f));
    canvas.rect(meterX, baseY - meterHeight, 15, meterHeight);
    canvas.fill(Color(0.2f, 1.0f, 0.3f, 0.9f));
    canvas.rect(meterX, baseY - leftLevel * meterHeight, 15, leftLevel * meterHeight);

    // Right channel
    canvas.fill(Color(0.2f, 0.2f, 0.2f, 0.8f));
    canvas.rect(meterX + 20, baseY - meterHeight, 15, meterHeight);
    canvas.fill(Color(0.2f, 1.0f, 0.3f, 0.9f));
    canvas.rect(meterX + 20, baseY - rightLevel * meterHeight, 15, rightLevel * meterHeight);

    // Labels
    canvas.fill(Color(0.7f, 0.7f, 0.7f, 1.0f));
    canvas.textSize(12);
    canvas.text("L", meterX + 3, baseY + 15);
    canvas.text("R", meterX + 23, baseY + 15);

    // Controls help
    canvas.textSize(14);
    canvas.fill(Color(0.6f, 0.6f, 0.6f, 0.8f));
    canvas.text("Space: Play/Pause | R: Restart | Left/Right: Seek 5s | Mouse X: Delay | Mouse Y: Bloom", 50, 30);

    // Debug
    ctx.debug("progress", progress);
    ctx.debug("avgLevel", avgLevel);
    ctx.debug("delayMix", delay.mix);
}

VIVID_CHAIN(setup, update)
