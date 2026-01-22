/**
 * Local WebGL Example
 *
 * Demonstrates using the Browser operator to render local WebGL content.
 * Loads a simple rotating cube from the assets folder.
 *
 * This example works offline and is useful for testing CEF integration.
 *
 * Build with: cmake -B build -DVIVID_USE_CEF=ON && cmake --build build
 * Run with:   ./build/bin/vivid modules/vivid-cef/examples/local-webgl
 */

#include <vivid/vivid.h>
#include <vivid/cef/browser.h>

using namespace vivid;
using namespace vivid::cef;

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

    // Create browser operator
    auto& browser = chain.add<Browser>("webgl");

    // Set size to match a common resolution
    browser.setSize(1280, 720);

    // Enable input for interactivity
    browser.setInputEnabled(true);

    // Console logging
    browser.onConsole([](ConsoleMessage::Level level, const std::string& message,
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

    // Load complete callback
    browser.onLoadEnd([](const std::string& url, int httpStatus) {
        printf("[Browser] Loaded: %s (HTTP %d)\n", url.c_str(), httpStatus);
    });

    // Load local HTML file
    // file:// URLs are resolved relative to the project directory
    browser.setUrl("file://assets/index.html");

    // Output the browser texture
    chain.output("webgl");
}

void update(Context& ctx) {
    // Pump CEF message loop
    pumpCefMessageLoop();

    // Process input
    auto& browser = ctx.chain().get<Browser>("webgl");
    browser.processInput(ctx);

    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
