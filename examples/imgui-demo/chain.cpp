// ImGui Demo Example
// Demonstrates Dear ImGui integration for parameter control
//
// This example shows how to use ImGui to create interactive controls
// that modify visual parameters in real-time.

#include <vivid/vivid.h>
#include <vivid/imgui/imgui_integration.h>
#include <imgui.h>

using namespace vivid;

// GUI-controlled parameters
static float noiseScale = 4.0f;
static float noiseSpeed = 0.5f;
static int noiseOctaves = 4;
static float hueShift = 0.0f;
static float saturation = 1.5f;
static float brightness = 1.0f;
static bool autoHue = true;

// GUI state
static Texture guiTexture;
static bool guiInitialized = false;

void setup(Chain& chain) {
    // Create noise generator
    chain.add<Noise>("noise")
        .scale(noiseScale)
        .speed(noiseSpeed)
        .octaves(noiseOctaves);

    // Color adjustment - colorize(true) needed for grayscale noise input!
    chain.add<HSV>("color")
        .input("noise")
        .saturation(saturation)
        .brightness(brightness)
        .colorize(true);

    // Composite: background (noise) + foreground (gui)
    chain.add<Composite>("output")
        .a("color")
        .b("gui");

    chain.setOutput("output");
}

void update(Chain& chain, Context& ctx) {
    // Initialize ImGui on first frame
    if (!guiInitialized) {
        guiTexture = ctx.createTexture();
        vivid::imgui::init(ctx);
        guiInitialized = true;
    }

    // Begin ImGui frame
    vivid::imgui::beginFrame(ctx);

    // Create control panel
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 350), ImGuiCond_FirstUseEver);

    ImGui::Begin("Visual Controls");

    ImGui::SeparatorText("Noise Parameters");
    if (ImGui::SliderFloat("Scale", &noiseScale, 0.5f, 20.0f)) {
        chain.get<Noise>("noise").scale(noiseScale);
    }
    if (ImGui::SliderFloat("Speed", &noiseSpeed, 0.0f, 3.0f)) {
        chain.get<Noise>("noise").speed(noiseSpeed);
    }
    if (ImGui::SliderInt("Octaves", &noiseOctaves, 1, 8)) {
        chain.get<Noise>("noise").octaves(noiseOctaves);
    }

    ImGui::SeparatorText("Color");
    ImGui::Checkbox("Auto Hue Cycle", &autoHue);
    if (!autoHue) {
        if (ImGui::SliderFloat("Hue Shift", &hueShift, 0.0f, 1.0f)) {
            chain.get<HSV>("color").hueShift(hueShift);
        }
    }
    if (ImGui::SliderFloat("Saturation", &saturation, 0.0f, 2.0f)) {
        chain.get<HSV>("color").saturation(saturation);
    }
    if (ImGui::SliderFloat("Brightness", &brightness, 0.0f, 2.0f)) {
        chain.get<HSV>("color").brightness(brightness);
    }

    ImGui::SeparatorText("Info");
    ImGui::Text("FPS: %.1f", 1.0f / ctx.dt());
    ImGui::Text("Time: %.2f", ctx.time());
    ImGui::Text("Resolution: %dx%d", ctx.width(), ctx.height());

    if (ImGui::Button("Reset to Defaults")) {
        noiseScale = 4.0f;
        noiseSpeed = 0.5f;
        noiseOctaves = 4;
        hueShift = 0.0f;
        saturation = 1.5f;
        brightness = 1.0f;
        autoHue = true;

        chain.get<Noise>("noise").scale(noiseScale).speed(noiseSpeed).octaves(noiseOctaves);
        chain.get<HSV>("color").saturation(saturation).brightness(brightness);
    }

    ImGui::End();

    // Auto-cycle hue if enabled
    if (autoHue) {
        hueShift = std::fmod(ctx.time() * 0.1f, 1.0f);
        chain.get<HSV>("color").hueShift(hueShift);
    }

    // Render ImGui to GUI texture (transparent background)
    vivid::imgui::render(ctx, guiTexture, {0, 0, 0, 0});

    // Make the GUI texture available to the chain for compositing
    ctx.setTextureForNode("gui", guiTexture);

    // Keyboard shortcuts
    if (ctx.wasKeyPressed(Key::F11) || ctx.wasKeyPressed(Key::F)) {
        ctx.toggleFullscreen();
    }
}

VIVID_CHAIN(setup, update)
