// DMX Lighting Control Example
//
// This example controls an RGB fixture via Enttec DMX USB Pro.
// The fixture's RGB values are controlled by an LFO.

#include <vivid/vivid.h>
#include <vivid/serial/dmx_out.h>

using namespace vivid;
using namespace vivid::serial;

void setup(Context& ctx) {
    // Create LFOs for R, G, B channels at different frequencies
    auto& lfoR = ctx.chain().add<LFO>("lfo_red");
    lfoR.frequency = 0.2f;

    auto& lfoG = ctx.chain().add<LFO>("lfo_green");
    lfoG.frequency = 0.3f;
    lfoG.phase = 0.33f;  // Offset phase

    auto& lfoB = ctx.chain().add<LFO>("lfo_blue");
    lfoB.frequency = 0.5f;
    lfoB.phase = 0.66f;  // Offset phase

    // DMX output via Enttec
    auto& dmx = ctx.chain().add<DMXOut>("dmx");

    // Change this to your Enttec's serial port:
    // macOS: /dev/tty.usbserial-EN123456
    // Linux: /dev/ttyUSB0
    // Windows: COM3, etc.
    dmx.port("/dev/tty.usbserial-EN123456");
}

void update(Context& ctx) {
    ctx.chain().process();

    // Get LFO values
    float r = ctx.chain().get<LFO>("lfo_red").value();
    float g = ctx.chain().get<LFO>("lfo_green").value();
    float b = ctx.chain().get<LFO>("lfo_blue").value();

    // Set RGB fixture on channels 1-3
    auto& dmx = ctx.chain().get<DMXOut>("dmx");
    dmx.rgb(1,
        static_cast<uint8_t>(r * 255),
        static_cast<uint8_t>(g * 255),
        static_cast<uint8_t>(b * 255)
    );

    // You can also set individual channels:
    // dmx.channel(4, 255);  // Set channel 4 to max

    // Or set multiple channels at once:
    // dmx.channels(5, {100, 150, 200});  // Set channels 5, 6, 7
}

VIVID_CHAIN(setup, update)
