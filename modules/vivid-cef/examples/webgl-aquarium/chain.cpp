/**
 * WebGL Aquarium Example
 *
 * Demonstrates using the Browser operator to render WebGL content.
 * Loads the WebGL Aquarium demo from webglsamples.org.
 *
 * This example shows:
 * - Loading a remote WebGL URL
 * - Handling JavaScript console messages
 * - Interactive input (mouse/keyboard forwarding)
 *
 * Build with: cmake -B build -DVIVID_USE_CEF=ON && cmake --build build
 * Run with:   ./build/bin/vivid modules/vivid-cef/examples/webgl-aquarium
 */

#include <vivid/vivid.h>
#include <vivid/cef/browser.h>

using namespace vivid;
using namespace vivid::cef;

// Global CEF state
static bool g_cefReady = false;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Initialize CEF if not already done
    // Note: In a real application, you'd call this once in main()
    // before creating the window. Here we do it in setup() for simplicity.
    if (!isCefInitialized()) {
        // CEF needs argc/argv, but we can pass dummy values for this example
        static char* argv[] = { (char*)"vivid", nullptr };
        if (!initializeCef(1, argv)) {
            fprintf(stderr, "Failed to initialize CEF!\n");
            return;
        }
        g_cefReady = true;
    }

    // Create browser operator
    auto& browser = chain.add<Browser>("aquarium");

    // Set size to match window (or use a fixed size)
    browser.setSize(1280, 720);

    // Enable input so we can interact with the WebGL scene
    browser.setInputEnabled(true);

    // Set up console message callback to see JS logs/errors
    browser.onConsole([](ConsoleMessage::Level level, const std::string& message,
                         const std::string& source, int line) {
        const char* levelStr = "INFO";
        switch (level) {
            case ConsoleMessage::Level::Debug:   levelStr = "DEBUG"; break;
            case ConsoleMessage::Level::Info:    levelStr = "INFO"; break;
            case ConsoleMessage::Level::Warning: levelStr = "WARN"; break;
            case ConsoleMessage::Level::Error:   levelStr = "ERROR"; break;
        }
        printf("[JS %s] %s (%s:%d)\n", levelStr, message.c_str(), source.c_str(), line);
    });

    // Set up load completion callback
    browser.onLoadEnd([](const std::string& url, int httpStatus) {
        printf("[Browser] Loaded: %s (HTTP %d)\n", url.c_str(), httpStatus);
    });

    // Load the WebGL Aquarium demo
    browser.setUrl("https://webglsamples.org/aquarium/aquarium.html");

    // Output the browser texture
    chain.output("aquarium");
}

void update(Context& ctx) {
    // Pump CEF message loop - required for CEF to function
    pumpCefMessageLoop();

    // Process input for the browser
    auto& browser = ctx.chain().get<Browser>("aquarium");
    browser.processInput(ctx);

    // Process the chain
    ctx.chain().process(ctx);
}

// Note: CEF cleanup would normally happen in main() after the window closes.
// For hot-reload compatibility, we don't call shutdownCef() here since
// that would break subsequent reloads.

VIVID_CHAIN(setup, update)
