// Nuklear Demo Example
// Demonstrates Nuklear UI integration with Vivid
// Uses: Nuklear GUI controls to modify visual parameters

#include <vivid/vivid.h>
#include <vivid/nuklear/nuklear_integration.h>
#include <iostream>

using namespace vivid;

// UI state
static nuklear::NuklearUI ui;
static Texture uiTexture;
static bool uiInitialized = false;

// Parameters controlled by UI
static float noiseScale = 4.0f;
static float noiseSpeed = 0.5f;
static int noiseOctaves = 4;
static float feedbackDecay = 0.95f;
static float feedbackZoom = 1.01f;
static float feedbackRotate = 0.02f;
static float hueShift = 0.0f;
static float saturation = 1.2f;
static bool autoRotateHue = true;

void setup(Chain& chain) {
    // Build the visual chain
    chain.add<Noise>("noise")
        .scale(noiseScale)
        .speed(noiseSpeed)
        .octaves(noiseOctaves);

    chain.add<Feedback>("feedback")
        .input("noise")
        .decay(feedbackDecay)
        .zoom(feedbackZoom)
        .rotate(feedbackRotate);

    chain.add<HSV>("colored")
        .input("feedback")
        .hueShift(hueShift)
        .saturation(saturation);

    // Composite UI overlay on top of the visual effect
    chain.add<Composite>("final")
        .a("colored")
        .b("ui_overlay")
        .mode(0);  // Alpha blend

    chain.setOutput("final");
}

void update(Chain& chain, Context& ctx) {
    // Initialize UI if needed
    if (!uiInitialized) {
        ui.init(ctx.width(), ctx.height(), 14.0f);
        uiTexture = ctx.createTexture();
        uiInitialized = true;
    }

    // Handle mouse input for UI
    ui.inputBegin();
    ui.inputMouse((int)ctx.mouseX(), (int)ctx.mouseY(),
                  ctx.isMouseDown(0), ctx.isMouseDown(1));
    ui.inputScroll(ctx.scrollDeltaX(), ctx.scrollDeltaY());
    ui.inputEnd();

    // Draw UI panel
    if (ui.begin("Controls", 10, 10, 250, 350)) {
        ui.layoutRow(25, 1);

        ui.label("Noise Scale:");
        if (ui.slider(&noiseScale, 0.5f, 20.0f, 0.1f)) {
            chain.get<Noise>("noise").scale(noiseScale);
        }

        ui.label("Noise Speed:");
        if (ui.slider(&noiseSpeed, 0.0f, 5.0f, 0.01f)) {
            chain.get<Noise>("noise").speed(noiseSpeed);
        }

        ui.label("Feedback Decay:");
        if (ui.slider(&feedbackDecay, 0.8f, 1.0f, 0.001f)) {
            chain.get<Feedback>("feedback").decay(feedbackDecay);
        }

        ui.label("Feedback Zoom:");
        if (ui.slider(&feedbackZoom, 0.95f, 1.05f, 0.001f)) {
            chain.get<Feedback>("feedback").zoom(feedbackZoom);
        }

        ui.label("Feedback Rotate:");
        if (ui.slider(&feedbackRotate, -0.1f, 0.1f, 0.001f)) {
            chain.get<Feedback>("feedback").rotate(feedbackRotate);
        }

        ui.checkbox("Auto-rotate Hue", &autoRotateHue);

        if (!autoRotateHue) {
            ui.label("Hue Shift:");
            if (ui.slider(&hueShift, 0.0f, 1.0f, 0.01f)) {
                chain.get<HSV>("colored").hueShift(hueShift);
            }
        }

        ui.label("Saturation:");
        if (ui.slider(&saturation, 0.0f, 3.0f, 0.01f)) {
            chain.get<HSV>("colored").saturation(saturation);
        }
    }
    ui.end();

    // Auto-rotate hue if enabled
    if (autoRotateHue) {
        hueShift = std::fmod(ctx.time() * 0.1f, 1.0f);
        chain.get<HSV>("colored").hueShift(hueShift);
    }

    // Render UI to texture and provide to chain for compositing
    ui.render(ctx, uiTexture);
    ctx.setTextureForNode("ui_overlay", uiTexture);
}

VIVID_CHAIN(setup, update)
