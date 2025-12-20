// OSC Control Example
// Demonstrates receiving and sending OSC messages for remote control
//
// Receives on port 8000, sends to port 9000
// Compatible with TouchOSC, Max/MSP, Pure Data, etc.
//
// Test with:
//   Send: oscsend localhost 8000 /fader/1 f 0.5
//   Receive: oscdump 9000

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/network/network.h>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::network;

// Controlled parameters (updated by OSC)
static float g_hueShift = 0.0f;
static float g_saturation = 1.0f;
static float g_blurRadius = 0.0f;
static float g_noiseScale = 4.0f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // OSC input (receive from TouchOSC, etc.)
    chain.add<OscIn>("oscIn").port(8000);

    // OSC output (send feedback to controller)
    chain.add<OscOut>("oscOut").host("127.0.0.1").port(9000);

    // Visual chain
    auto& noise = chain.add<Noise>("noise");
    noise.scale = g_noiseScale;
    noise.speed = 0.5f;
    noise.octaves = 4;

    auto& hsv = chain.add<HSV>("hsv");
    hsv.input(&noise);
    hsv.hueShift = g_hueShift;
    hsv.saturation = g_saturation;

    auto& blur = chain.add<Blur>("blur");
    blur.input(&hsv);
    blur.radius = g_blurRadius;

    chain.output("blur");

    std::cout << "OSC Control Example" << std::endl;
    std::cout << "  Receiving on port 8000" << std::endl;
    std::cout << "  Sending to 127.0.0.1:9000" << std::endl;
    std::cout << std::endl;
    std::cout << "Expected OSC addresses:" << std::endl;
    std::cout << "  /fader/hue    (0-1 -> hue shift)" << std::endl;
    std::cout << "  /fader/sat    (0-1 -> saturation)" << std::endl;
    std::cout << "  /fader/blur   (0-1 -> blur radius)" << std::endl;
    std::cout << "  /fader/scale  (0-1 -> noise scale)" << std::endl;
    std::cout << "  /button/*     (any button press)" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& oscIn = chain.get<OscIn>("oscIn");
    auto& oscOut = chain.get<OscOut>("oscOut");

    // Process incoming OSC messages
    for (const auto& msg : oscIn.messages()) {
        std::cout << "[OSC] " << msg.address;
        if (msg.argCount() > 0) {
            std::cout << " = " << msg.floatArg(0);
        }
        std::cout << std::endl;

        // Map OSC addresses to parameters
        if (msg.address == "/fader/hue" || msg.address == "/1/fader1") {
            g_hueShift = msg.floatArg(0);
        }
        else if (msg.address == "/fader/sat" || msg.address == "/1/fader2") {
            g_saturation = msg.floatArg(0);
        }
        else if (msg.address == "/fader/blur" || msg.address == "/1/fader3") {
            g_blurRadius = msg.floatArg(0) * 50.0f;  // Scale to 0-50
        }
        else if (msg.address == "/fader/scale" || msg.address == "/1/fader4") {
            g_noiseScale = 1.0f + msg.floatArg(0) * 15.0f;  // Scale to 1-16
        }
        else if (msg.address.find("/button") == 0) {
            // Button pressed - send feedback
            oscOut.send("/feedback/button", 1);
        }
    }

    // Apply parameters to operators
    auto& noise = chain.get<Noise>("noise");
    noise.scale = g_noiseScale;
    noise.offset.set(0.0f, 0.0f, static_cast<float>(ctx.time()) * 0.3f);

    auto& hsv = chain.get<HSV>("hsv");
    hsv.hueShift = g_hueShift;
    hsv.saturation = g_saturation;

    auto& blur = chain.get<Blur>("blur");
    blur.radius = g_blurRadius;

    // Send periodic updates back to controller (for bidirectional sync)
    static float lastSendTime = 0.0f;
    float time = static_cast<float>(ctx.time());
    if (time - lastSendTime > 0.1f) {  // 10 Hz update rate
        oscOut.send("/status/fps", static_cast<float>(1.0 / ctx.dt()));
        oscOut.send("/status/time", time);
        lastSendTime = time;
    }
}

VIVID_CHAIN(setup, update)
