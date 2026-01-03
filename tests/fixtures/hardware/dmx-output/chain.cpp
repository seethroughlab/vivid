// Testing Fixture: DMX Output
// Tests DMXOut operator for controlling lighting fixtures
//
// Hardware requirement: USB-DMX adapter (ENTTEC, etc.)
// Simulates a simple RGB fixture on channels 1-3
//
// Visual: On-screen color preview matches DMX output

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/serial/serial.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::serial;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // DMX Output - configure for your USB-DMX adapter
    auto& dmx = chain.add<DMXOut>("dmx");
    // dmx.port("/dev/tty.usbserial-EN193448");  // Uncomment and adjust for your device
    dmx.universe(1);

    // Visual feedback - solid color matching DMX output
    auto& color = chain.add<SolidColor>("color");
    color.color.set(1.0f, 0.5f, 0.2f, 1.0f);

    // Add vignette for depth
    auto& vignette = chain.add<Vignette>("vignette");
    vignette.input("color");
    vignette.intensity = 0.5f;
    vignette.softness = 0.6f;

    chain.output("vignette");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = static_cast<float>(ctx.time());

    // Generate animated RGB values
    float r = (std::sin(t * 1.0f) * 0.5f + 0.5f);
    float g = (std::sin(t * 1.3f + 2.0f) * 0.5f + 0.5f);
    float b = (std::sin(t * 0.7f + 4.0f) * 0.5f + 0.5f);

    // Update visual feedback
    auto& color = chain.get<SolidColor>("color");
    color.color.set(r, g, b, 1.0f);

    // Send to DMX (channels 1-3 for RGB fixture)
    auto& dmx = chain.get<DMXOut>("dmx");
    dmx.setChannel(1, static_cast<uint8_t>(r * 255));
    dmx.setChannel(2, static_cast<uint8_t>(g * 255));
    dmx.setChannel(3, static_cast<uint8_t>(b * 255));
}

VIVID_CHAIN(setup, update)
