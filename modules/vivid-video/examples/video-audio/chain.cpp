// Video Audio - Video playback with audio
//
// This example demonstrates two modes:
// 1. Internal audio (default): Perfect A/V sync via AVPlayer, no effects
// 2. Chain audio: Route through effects chain, may have sync issues
//
// Controls:
//   Space - Play/Pause
//   R - Restart
//   Left/Right - Seek 5 seconds
//   TAB - Toggle panel

#include <vivid/vivid.h>
#include <vivid/video/video.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <vivid/gui/imgui.h>
#include <iostream>

using namespace vivid;
using namespace vivid::video;
using namespace vivid::audio;

// Mode: true = use AVPlayer internal audio (synced), false = use chain routing
static bool g_useInternalAudio = true;

// Effect parameters (only used when g_useInternalAudio = false)
static float g_masterVolume = 0.8f;
static float g_delayTime = 300.0f;
static float g_delayFeedback = 0.4f;
static float g_delayMix = 0.3f;
static float g_reverbSize = 0.5f;
static float g_reverbDamping = 0.5f;
static float g_reverbMix = 0.2f;
static bool g_effectsEnabled = false;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Video playback
    auto& video = chain.add<VideoPlayer>("video");
    video.setFile("assets/videos/sample.mov");
    video.setLoop(true);

    // When using internal audio, AVPlayer handles everything in sync
    // When using chain audio, we extract and route through effects
    video.setInternalAudioEnabled(g_useInternalAudio);

    if (!g_useInternalAudio) {
        // Extract audio from video for chain routing
        auto& videoAudio = chain.add<VideoAudio>("videoAudio");
        videoAudio.setSource("video");

        // Audio effects chain
        auto& delay = chain.add<Delay>("delay");
        delay.input("videoAudio");
        delay.delayTime = g_delayTime;
        delay.feedback = g_delayFeedback;
        delay.mix = g_delayMix;
        delay.bypass(!g_effectsEnabled);

        auto& reverb = chain.add<Reverb>("reverb");
        reverb.input("delay");
        reverb.roomSize = g_reverbSize;
        reverb.damping = g_reverbDamping;
        reverb.mix = g_reverbMix;
        reverb.bypass(!g_effectsEnabled);

        auto& gain = chain.add<AudioGain>("gain");
        gain.input("reverb");
        gain.gain = g_masterVolume;

        // Audio output
        auto& output = chain.add<AudioOutput>("out");
        output.setInput("gain");
        chain.audioOutput("out");
    }

    chain.output("video");

    // Initialize ImGui
    vivid::imgui::init(ctx);

    std::cout << "\n========================================" << std::endl;
    std::cout << "Video Audio Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Mode: " << (g_useInternalAudio ? "Internal Audio (synced)" : "Chain Audio (effects)") << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Space      - Play/Pause" << std::endl;
    std::cout << "  R          - Restart" << std::endl;
    std::cout << "  Left/Right - Seek 5 seconds" << std::endl;
    std::cout << "  TAB        - Toggle panel" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& video = chain.get<VideoPlayer>("video");

    // Keyboard controls
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        if (video.isPlaying()) {
            video.pause();
        } else {
            video.play();
        }
    }

    if (ctx.key(GLFW_KEY_R).pressed) {
        video.restart();
    }

    if (ctx.key(GLFW_KEY_LEFT).pressed) {
        video.seek(std::max(0.0f, video.currentTime() - 5.0f));
    }

    if (ctx.key(GLFW_KEY_RIGHT).pressed) {
        video.seek(std::min(video.duration(), video.currentTime() + 5.0f));
    }

    if (ctx.key(GLFW_KEY_TAB).pressed) {
        vivid::imgui::toggleVisible();
    }

    // ImGui panel
    if (vivid::imgui::isVisible()) {
        ImGui::Begin("Video Audio", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        // Playback info
        ImGui::Text("Time: %.1f / %.1f", video.currentTime(), video.duration());
        ImGui::Text("Status: %s", video.isPlaying() ? "Playing" : "Paused");
        ImGui::Separator();

        // Audio mode info
        if (g_useInternalAudio) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Mode: Internal Audio (synced)");
            ImGui::TextWrapped("Using AVPlayer's internal audio for perfect A/V sync. "
                              "No effects available in this mode.");
            ImGui::Separator();
            ImGui::TextDisabled("To enable effects, change g_useInternalAudio to false");
            ImGui::TextDisabled("and restart. Note: may have sync issues.");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Mode: Chain Audio (effects)");
            ImGui::TextWrapped("Audio routed through effects chain. "
                              "May have A/V sync issues.");
            ImGui::Separator();

            // Get effect operators
            auto& delay = chain.get<Delay>("delay");
            auto& reverb = chain.get<Reverb>("reverb");
            auto& gain = chain.get<AudioGain>("gain");

            if (ImGui::SliderFloat("Master Volume", &g_masterVolume, 0.0f, 2.0f)) {
                gain.gain = g_masterVolume;
            }

            if (ImGui::Checkbox("Enable Effects", &g_effectsEnabled)) {
                delay.bypass(!g_effectsEnabled);
                reverb.bypass(!g_effectsEnabled);
            }
            ImGui::Separator();

            if (g_effectsEnabled) {
                ImGui::Text("Delay");
                if (ImGui::SliderFloat("Time (ms)", &g_delayTime, 0.0f, 1000.0f)) {
                    delay.delayTime = g_delayTime;
                }
                if (ImGui::SliderFloat("Feedback", &g_delayFeedback, 0.0f, 0.95f)) {
                    delay.feedback = g_delayFeedback;
                }
                if (ImGui::SliderFloat("Delay Mix", &g_delayMix, 0.0f, 1.0f)) {
                    delay.mix = g_delayMix;
                }
                ImGui::Separator();

                ImGui::Text("Reverb");
                if (ImGui::SliderFloat("Room Size", &g_reverbSize, 0.0f, 1.0f)) {
                    reverb.roomSize = g_reverbSize;
                }
                if (ImGui::SliderFloat("Damping", &g_reverbDamping, 0.0f, 1.0f)) {
                    reverb.damping = g_reverbDamping;
                }
                if (ImGui::SliderFloat("Reverb Mix", &g_reverbMix, 0.0f, 1.0f)) {
                    reverb.mix = g_reverbMix;
                }
            }
        }

        ImGui::End();
    }

    ctx.debug("time", video.currentTime());
}

VIVID_CHAIN(setup, update)
