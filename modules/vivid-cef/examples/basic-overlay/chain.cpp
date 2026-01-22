// Browser - Basic Overlay Example
//
// This example demonstrates using Browser (CEF) as a transparent UI overlay
// on top of a Vivid effect.
//
// Controls:
//   Space - Toggle overlay visibility
//   R - Reload web page
//   TAB - Toggle chain visualizer

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/cef/browser.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::cef;

static bool g_overlayVisible = true;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Initialize CEF if not already done
    if (!isCefInitialized()) {
        static char* argv[] = { (char*)"vivid", nullptr };
        if (!initializeCef(1, argv)) {
            fprintf(stderr, "Failed to initialize CEF!\n");
            return;
        }
    }

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

    // Browser UI overlay
    auto& ui = chain.add<Browser>("ui");
    ui.setUrl("file://assets/overlay.html");
    ui.setSize(ctx.width(), ctx.height());
    ui.setTransparent(true);
    ui.setInputEnabled(true);

    // Console logging
    ui.onConsole([](ConsoleMessage::Level level, const std::string& message,
                    const std::string& source, int line) {
        const char* levelStr = "INFO";
        switch (level) {
            case ConsoleMessage::Level::Debug:   levelStr = "DEBUG"; break;
            case ConsoleMessage::Level::Info:    levelStr = "INFO"; break;
            case ConsoleMessage::Level::Warning: levelStr = "WARN"; break;
            case ConsoleMessage::Level::Error:   levelStr = "ERROR"; break;
        }
        printf("[JS %s] %s\n", levelStr, message.c_str());
    });

    // Register callback from JavaScript
    ui.registerCallback("onButtonClick", [](const std::string& args) {
        printf("[Vivid] Button clicked: %s\n", args.c_str());
    });

    // Composite UI over background
    auto& comp = chain.add<Composite>("composite");
    comp.inputA("bright");
    comp.inputB("ui");
    comp.mode = BlendMode::Over;

    chain.output("composite");

    printf("\n========================================\n");
    printf("Browser Overlay Demo (CEF)\n");
    printf("========================================\n");
    printf("Controls:\n");
    printf("  Space - Toggle overlay visibility\n");
    printf("  R     - Reload web page\n");
    printf("  TAB   - Toggle chain visualizer\n");
    printf("========================================\n\n");
}

void update(Context& ctx) {
    // Pump CEF message loop
    pumpCefMessageLoop();

    auto& chain = ctx.chain();
    auto& ui = chain.get<Browser>("ui");
    auto& comp = chain.get<Composite>("composite");

    // Toggle overlay visibility
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        g_overlayVisible = !g_overlayVisible;
        comp.opacity = g_overlayVisible ? 1.0f : 0.0f;
        printf("Overlay: %s\n", g_overlayVisible ? "visible" : "hidden");
    }

    // Reload page
    if (ctx.key(GLFW_KEY_R).pressed) {
        ui.reload();
        printf("Reloading...\n");
    }

    // Update UI size if window resized
    if (ui.browserWidth() != ctx.width() || ui.browserHeight() != ctx.height()) {
        ui.setSize(ctx.width(), ctx.height());
    }

    // Process input for the browser
    ui.processInput(ctx);

    // Show loading status
    if (ui.isLoading()) {
        ctx.debug("browser", "Loading...");
    } else if (ui.isReady()) {
        ctx.debug("browser", "Ready");
    }

    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
