// WebSocket UI - Vivid Example
// Demonstrates: WebServer with WebSocket for real-time browser control
//
// Access the control interface at: http://localhost:8080
// WebSocket messages update parameters in real-time

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/network/network.h>
#include <cmath>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::network;

// State variables that can be controlled via WebSocket
static float g_hue = 0.0f;
static float g_scale = 4.0f;
static float g_speed = 0.5f;
static float g_rotation = 0.0f;
static bool g_animate = true;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- WEB SERVER -----
    auto& web = chain.add<WebServer>("web");
    web.port(8080);

    // Serve static files from web/ subdirectory
    web.staticDir("modules/vivid-network/examples/websocket-ui/web/");

    // Custom route for getting current state
    web.route("/api/state", [](const std::string& method,
                               const std::string& path,
                               const std::string& body) -> std::string {
        // Return current state as JSON
        char json[256];
        snprintf(json, sizeof(json),
            "{\"hue\":%.2f,\"scale\":%.2f,\"speed\":%.2f,\"rotation\":%.2f,\"animate\":%s}",
            g_hue, g_scale, g_speed, g_rotation, g_animate ? "true" : "false");
        return json;
    });

    // Custom route for updating state
    web.route("/api/update", [](const std::string& method,
                                const std::string& path,
                                const std::string& body) -> std::string {
        // Parse simple key=value format
        // In production, use proper JSON parsing
        if (body.find("hue=") != std::string::npos) {
            g_hue = std::stof(body.substr(body.find("hue=") + 4));
        }
        if (body.find("scale=") != std::string::npos) {
            g_scale = std::stof(body.substr(body.find("scale=") + 6));
        }
        if (body.find("speed=") != std::string::npos) {
            g_speed = std::stof(body.substr(body.find("speed=") + 6));
        }
        if (body.find("rotation=") != std::string::npos) {
            g_rotation = std::stof(body.substr(body.find("rotation=") + 9));
        }
        if (body.find("animate=") != std::string::npos) {
            g_animate = body.find("animate=true") != std::string::npos;
        }
        return "{\"status\":\"ok\"}";
    });

    // ----- VISUAL CHAIN -----
    // Noise generator
    auto& noise = chain.add<Noise>("noise");
    noise.scale = g_scale;
    noise.speed = g_speed;
    noise.octaves = 4;

    // HSV color adjustment
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input("noise");
    hsv.hueShift = g_hue;

    // Transform for rotation
    auto& transform = chain.add<Transform>("transform");
    transform.input("hsv");

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("transform");
    bloom.threshold = 0.5f;
    bloom.intensity = 1.0f;
    bloom.radius = 20.0f;

    chain.output("bloom");

    std::cout << "=== WebSocket UI Example ===" << std::endl;
    std::cout << "Open http://localhost:8080 in your browser" << std::endl;
    std::cout << "Use sliders to control the visualization" << std::endl;
    std::cout << "WebSocket provides real-time updates" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& web = chain.get<WebServer>("web");
    auto& noise = chain.get<Noise>("noise");
    auto& hsv = chain.get<HSV>("hsv");
    auto& transform = chain.get<Transform>("transform");

    // Apply state to operators
    noise.scale = g_scale;
    noise.speed = g_animate ? g_speed : 0.0f;
    hsv.hueShift = g_hue;
    transform.rotation = g_rotation;

    // Animate noise offset
    if (g_animate) {
        noise.offset.set(0.0f, 0.0f, t * g_speed * 0.5f);
    }

    // Broadcast state to WebSocket clients periodically
    static float lastBroadcast = 0.0f;
    if (t - lastBroadcast > 0.1f) {  // 10Hz update rate
        char json[256];
        snprintf(json, sizeof(json),
            "{\"type\":\"state\",\"hue\":%.2f,\"scale\":%.2f,\"speed\":%.2f,\"rotation\":%.2f,\"animate\":%s,\"time\":%.2f}",
            g_hue, g_scale, g_speed, g_rotation, g_animate ? "true" : "false", t);
        web.broadcast(json);
        lastBroadcast = t;
    }
}

VIVID_CHAIN(setup, update)
