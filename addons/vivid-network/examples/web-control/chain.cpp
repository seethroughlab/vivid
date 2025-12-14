// Web Control Example
// HTTP server with REST API for remote parameter control
//
// Access the web interface at: http://localhost:8080
// API endpoints:
//   GET  /api/operators      - List all operators
//   GET  /api/operator/:id   - Get operator params
//   POST /api/operator/:id   - Set operator params

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/network/network.h>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::network;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Some operators with controllable parameters
    auto& noise = chain.add<Noise>("noise")
        .scale(4.0f)
        .speed(0.5f)
        .octaves(4);

    auto& hsv = chain.add<HSV>("hsv")
        .input(&noise)
        .hueShift(0.0f)
        .saturation(1.0f)
        .value(1.0f);

    auto& blur = chain.add<Blur>("blur")
        .input(&hsv)
        .radius(0.0f)
        .passes(2);

    // Web server
    chain.add<WebServer>("web")
        .port(8080)
        .staticDir("examples/network/web-control/web/");

    chain.output("blur");

    std::cout << "Web server running at http://localhost:8080" << std::endl;
    std::cout << "API: GET /api/operators" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Animate noise
    auto& noise = chain.get<Noise>("noise");
    noise.offset(0, 0, static_cast<float>(ctx.time()) * 0.3f);
}

VIVID_CHAIN(setup, update)
