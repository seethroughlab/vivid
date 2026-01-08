// DMX Lighting - Vivid Example
// Demonstrates: DMXOut, SerialOut - Stage lighting control
//
// Control DMX fixtures via Enttec DMX USB Pro adapter.
// NOTE: Requires hardware - Enttec DMX USB Pro or compatible.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/serial/serial.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;
using namespace vivid::serial;

// DMX channel assignments (adjust for your fixtures)
static constexpr int FIXTURE_1_START = 1;   // RGB Par Can #1
static constexpr int FIXTURE_2_START = 4;   // RGB Par Can #2
static constexpr int FIXTURE_3_START = 7;   // RGB Par Can #3
static constexpr int STROBE_CHANNEL = 10;   // Strobe dimmer
static constexpr int FOG_CHANNEL = 11;      // Fog machine

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- DMX OUTPUT -----
    // Connect to Enttec DMX USB Pro
    auto& dmx = chain.add<DMXOut>("dmx");
    // Uncomment and set your port:
    // dmx.port("/dev/tty.usbserial-EN123456");  // macOS
    // dmx.port("COM3");                          // Windows
    dmx.universe = 1;
    dmx.startChannel = 1;

    // ----- AUDIO INPUT (for reactive lighting) -----
    // Use FFT for frequency analysis
    auto& audio = chain.add<AudioIn>("audio");
    audio.gain = 1.5f;

    auto& fft = chain.add<FFT>("fft");
    fft.input("audio");
    fft.size(FFTSize::FFT_512);

    // ----- CLOCK (for chase patterns) -----
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Eighth);
    clock.start();

    // ----- VISUALS (preview of lighting) -----
    // Dark background
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.02f, 0.02f, 0.02f, 1.0f);

    // Three circles representing fixtures
    auto& fix1 = chain.add<Shape>("fix1");
    fix1.type(ShapeType::Circle);
    fix1.size.set(0.15f, 0.15f);
    fix1.position.set(-0.35f, 0.0f);
    fix1.softness = 0.5f;

    auto& fix2 = chain.add<Shape>("fix2");
    fix2.type(ShapeType::Circle);
    fix2.size.set(0.15f, 0.15f);
    fix2.position.set(0.0f, 0.0f);
    fix2.softness = 0.5f;

    auto& fix3 = chain.add<Shape>("fix3");
    fix3.type(ShapeType::Circle);
    fix3.size.set(0.15f, 0.15f);
    fix3.position.set(0.35f, 0.0f);
    fix3.softness = 0.5f;

    // Composite fixtures
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("bg");
    comp1.inputB("fix1");
    comp1.mode(BlendMode::Add);

    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("fix2");
    comp2.mode(BlendMode::Add);

    auto& comp3 = chain.add<Composite>("comp3");
    comp3.inputA("comp2");
    comp3.inputB("fix3");
    comp3.mode(BlendMode::Add);

    // Bloom for glow effect
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("comp3");
    bloom.threshold = 0.3f;
    bloom.intensity = 2.0f;
    bloom.radius = 40.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& dmx = chain.get<DMXOut>("dmx");
    auto& fft = chain.get<FFT>("fft");
    auto& clock = chain.get<Clock>("clock");

    // ----- AUDIO-REACTIVE COLORS -----
    // Get frequency bands
    float bass = fft.band(0);      // Low frequencies
    float mid = fft.band(2);       // Mid frequencies
    float high = fft.band(5);      // High frequencies

    // Scale for visibility
    bass = std::min(1.0f, bass * 3.0f);
    mid = std::min(1.0f, mid * 4.0f);
    high = std::min(1.0f, high * 5.0f);

    // ----- CHASE PATTERN (on clock) -----
    static int chaseStep = 0;
    if (clock.triggered()) {
        chaseStep = (chaseStep + 1) % 3;
    }

    // Base colors for each fixture (RGB)
    float r1, g1, b1;  // Fixture 1
    float r2, g2, b2;  // Fixture 2
    float r3, g3, b3;  // Fixture 3

    // Chase pattern with audio reactivity
    switch (chaseStep) {
        case 0:  // Fixture 1 hot
            r1 = 1.0f; g1 = bass * 0.5f; b1 = 0.0f;
            r2 = 0.2f; g2 = mid * 0.3f; b2 = high;
            r3 = 0.1f; g3 = 0.0f; b3 = high * 0.5f;
            break;
        case 1:  // Fixture 2 hot
            r1 = 0.1f; g1 = 0.0f; b1 = high * 0.5f;
            r2 = 1.0f; g2 = mid * 0.5f; b2 = 0.0f;
            r3 = 0.2f; g3 = bass * 0.3f; b3 = high;
            break;
        case 2:  // Fixture 3 hot
            r1 = 0.2f; g1 = high * 0.3f; b1 = bass;
            r2 = 0.1f; g2 = 0.0f; b2 = high * 0.5f;
            r3 = 1.0f; g3 = mid * 0.5f; b3 = 0.0f;
            break;
    }

    // Apply bass pulse to brightness
    float pulse = 1.0f + bass * 0.5f;
    r1 *= pulse; g1 *= pulse; b1 *= pulse;
    r2 *= pulse; g2 *= pulse; b2 *= pulse;
    r3 *= pulse; g3 *= pulse; b3 *= pulse;

    // Clamp to valid range
    auto clamp = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
    r1 = clamp(r1); g1 = clamp(g1); b1 = clamp(b1);
    r2 = clamp(r2); g2 = clamp(g2); b2 = clamp(b2);
    r3 = clamp(r3); g3 = clamp(g3); b3 = clamp(b3);

    // ----- SEND DMX DATA -----
    // RGB fixtures (values 0-255)
    dmx.rgb(FIXTURE_1_START,
            static_cast<uint8_t>(r1 * 255),
            static_cast<uint8_t>(g1 * 255),
            static_cast<uint8_t>(b1 * 255));

    dmx.rgb(FIXTURE_2_START,
            static_cast<uint8_t>(r2 * 255),
            static_cast<uint8_t>(g2 * 255),
            static_cast<uint8_t>(b2 * 255));

    dmx.rgb(FIXTURE_3_START,
            static_cast<uint8_t>(r3 * 255),
            static_cast<uint8_t>(g3 * 255),
            static_cast<uint8_t>(b3 * 255));

    // Strobe on high frequencies
    uint8_t strobeVal = (high > 0.7f) ? 255 : 0;
    dmx.channel(STROBE_CHANNEL, strobeVal);

    // Fog on bass hits
    static float fogLevel = 0.0f;
    if (bass > 0.8f) {
        fogLevel = 1.0f;  // Trigger fog
    }
    fogLevel *= 0.98f;  // Decay
    dmx.channel(FOG_CHANNEL, static_cast<uint8_t>(fogLevel * 200));

    // ----- UPDATE VISUAL PREVIEW -----
    auto& fix1 = chain.get<Shape>("fix1");
    auto& fix2 = chain.get<Shape>("fix2");
    auto& fix3 = chain.get<Shape>("fix3");

    fix1.color.set(r1, g1, b1, 1.0f);
    fix2.color.set(r2, g2, b2, 1.0f);
    fix3.color.set(r3, g3, b3, 1.0f);

    // Size pulse
    float s1 = 0.12f + r1 * 0.08f;
    float s2 = 0.12f + r2 * 0.08f;
    float s3 = 0.12f + r3 * 0.08f;
    fix1.size.set(s1, s1);
    fix2.size.set(s2, s2);
    fix3.size.set(s3, s3);

    // Debug output (uncomment to see DMX values)
    // ctx.debug("bass", bass);
    // ctx.debug("dmx_ch1", static_cast<float>(dmx.getChannel(1)));
}

VIVID_CHAIN(setup, update)
