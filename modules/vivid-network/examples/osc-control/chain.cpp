// OSC Control - Vivid Example
// Demonstrates: OscIn, OscOut
//
// External control via OSC (TouchOSC, Lemur, Max/MSP, etc.)

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/network/network.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::network;

// OSC-controlled parameters
static float g_hue = 0.0f;
static float g_saturation = 1.0f;
static float g_size = 0.3f;
static float g_rotation = 0.0f;
static float g_noiseScale = 3.0f;
static float g_noiseSpeed = 0.5f;
static bool g_triggerFlash = false;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- OSC INPUT -----
    // Listen for incoming OSC messages
    auto& oscIn = chain.add<OscIn>("oscIn");
    oscIn.port(8000);  // Listen on port 8000

    // ----- OSC OUTPUT -----
    // Send feedback to controller
    auto& oscOut = chain.add<OscOut>("oscOut");
    oscOut.host("localhost");  // Send to same machine
    oscOut.port(9000);         // Controller receives on port 9000

    // ----- VISUAL CHAIN -----
    // Noise background
    auto& noise = chain.add<Noise>("noise");
    noise.scale = g_noiseScale;
    noise.speed = g_noiseSpeed;
    noise.octaves = 3;

    // Animated shape
    auto& shape = chain.add<Shape>("shape");
    shape.type(ShapeType::Star);
    shape.sides = 6;
    shape.size.set(g_size, g_size);
    shape.softness = 0.1f;
    shape.color.set(1.0f, 1.0f, 1.0f, 1.0f);

    // HSV for color control
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input("noise");
    hsv.saturation = g_saturation;

    // Composite
    auto& comp = chain.add<Composite>("comp");
    comp.inputA("hsv");
    comp.inputB("shape");
    comp.mode(BlendMode::Add);

    // Flash effect (triggered by OSC)
    auto& flash = chain.add<Flash>("flash");
    flash.input("comp");
    flash.duration = 0.1f;

    // Bloom
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("flash");
    bloom.threshold = 0.5f;
    bloom.intensity = 1.5f;
    bloom.radius = 20.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& oscIn = chain.get<OscIn>("oscIn");
    auto& oscOut = chain.get<OscOut>("oscOut");

    // ----- RECEIVE OSC MESSAGES -----
    // Check for each expected address pattern

    // Fader 1: Hue (0-1)
    if (oscIn.hasMessage("/fader/1")) {
        g_hue = oscIn.getFloat("/fader/1");
    }

    // Fader 2: Saturation (0-1)
    if (oscIn.hasMessage("/fader/2")) {
        g_saturation = oscIn.getFloat("/fader/2");
    }

    // Fader 3: Size (0.1-0.5)
    if (oscIn.hasMessage("/fader/3")) {
        g_size = 0.1f + oscIn.getFloat("/fader/3") * 0.4f;
    }

    // XY Pad: Noise control
    if (oscIn.hasMessage("/xy/1")) {
        float x = oscIn.getFloat("/xy/1", 0);
        float y = oscIn.getFloat("/xy/1", 1);
        g_noiseScale = 1.0f + x * 8.0f;
        g_noiseSpeed = y * 2.0f;
    }

    // Button: Trigger flash
    if (oscIn.hasMessage("/button/1")) {
        float value = oscIn.getFloat("/button/1");
        if (value > 0.5f) {
            g_triggerFlash = true;
        }
    }

    // Knob: Rotation
    if (oscIn.hasMessage("/knob/1")) {
        g_rotation = oscIn.getFloat("/knob/1") * 6.28f;
    }

    // ----- SEND OSC FEEDBACK -----
    oscOut.send("/status/hue", g_hue);
    oscOut.send("/status/size", g_size);

    // Send beat pulse for LED sync
    static float lastPulse = 0.0f;
    float pulseInterval = 0.5f;
    if (t - lastPulse > pulseInterval) {
        oscOut.send("/beat", 1.0f);
        lastPulse = t;
    }

    // ----- APPLY OSC VALUES TO CHAIN -----
    auto& noise = chain.get<Noise>("noise");
    noise.scale = g_noiseScale;
    noise.speed = g_noiseSpeed;

    auto& hsv = chain.get<HSV>("hsv");
    hsv.hueShift = g_hue;
    hsv.saturation = g_saturation;

    auto& shape = chain.get<Shape>("shape");
    shape.size.set(g_size, g_size);
    shape.rotation = g_rotation;

    shape.color.set(
        0.5f + 0.5f * std::sin(g_hue * 6.28f),
        0.5f + 0.5f * std::sin(g_hue * 6.28f + 2.09f),
        0.5f + 0.5f * std::sin(g_hue * 6.28f + 4.19f),
        1.0f
    );

    if (g_triggerFlash) {
        auto& flash = chain.get<Flash>("flash");
        flash.trigger();
        g_triggerFlash = false;
    }

    shape.rotation = g_rotation + t * 0.1f;
}

VIVID_CHAIN(setup, update)
