// WebView - Basic Overlay Example
//
// This example demonstrates using WebView as a transparent UI overlay
// on top of a Vivid effect.
//
// Controls:
//   Space - Toggle overlay visibility
//   R - Reload web page
//   TAB - Toggle chain visualizer

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/webview/webview_all.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::webview;

static bool g_overlayVisible = true;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Background effect - animated noise
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 3.0f;
    noise.speed = 0.3f;
    noise.octaves = 4;

    // Adjust brightness
    auto& bright = chain.add<Brightness>("bright");
    bright.input("noise");
    bright.brightness = 0.1f;
    bright.contrast = 1.2f;

    // WebView UI overlay
    auto& ui = chain.add<WebView>("ui");
    ui.setUrl("file://assets/overlay.html");
    ui.setSize(ctx.width(), ctx.height());
    ui.setTransparent(true);
    ui.setInputEnabled(true);

    // Register callback from JavaScript
    ui.registerCallback("onButtonClick", [](const std::string& args) {
        std::cout << "[JS] Button clicked: " << args << std::endl;
    });

    // Composite UI over background
    auto& comp = chain.add<Composite>("composite");
    comp.inputA("bright");
    comp.inputB("ui");
    comp.mode = BlendMode::Over;

    chain.output("composite");

    std::cout << "\n========================================" << std::endl;
    std::cout << "WebView Overlay Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Space - Toggle overlay visibility" << std::endl;
    std::cout << "  R     - Reload web page" << std::endl;
    std::cout << "  TAB   - Toggle chain visualizer" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& ui = chain.get<WebView>("ui");
    auto& comp = chain.get<Composite>("composite");

    // Toggle overlay visibility
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        g_overlayVisible = !g_overlayVisible;
        comp.opacity = g_overlayVisible ? 1.0f : 0.0f;
        std::cout << "Overlay: " << (g_overlayVisible ? "visible" : "hidden") << std::endl;
    }

    // Reload page
    if (ctx.key(GLFW_KEY_R).pressed) {
        ui.reload();
        std::cout << "Reloading..." << std::endl;
    }

    // Update UI size if window resized
    if (ui.webviewWidth() != ctx.width() || ui.webviewHeight() != ctx.height()) {
        ui.setSize(ctx.width(), ctx.height());
    }

    // Show loading status
    if (ui.isLoading()) {
        ctx.debug("webview", "Loading...");
    } else if (ui.isReady()) {
        ctx.debug("webview", "Ready");
    }
}

VIVID_CHAIN(setup, update)
