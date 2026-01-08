# DMX Lighting

Audio-reactive stage lighting control via DMX512 protocol.

## Hardware Requirements

- **Enttec DMX USB Pro** (or compatible USB-to-DMX adapter)
- **DMX fixtures** (RGB par cans, strobes, fog machines, etc.)

## Operators Used

- **DMXOut** - Send DMX512 data to lighting fixtures
- **SerialOut** - Base serial communication (inherited by DMXOut)
- **FFT** - Audio frequency analysis for reactivity
- **Clock** - Timing for chase patterns

## DMX Basics

### Channel Assignment
```cpp
// Each fixture uses consecutive channels
static constexpr int FIXTURE_1_START = 1;   // Channels 1-3 (RGB)
static constexpr int FIXTURE_2_START = 4;   // Channels 4-6 (RGB)
static constexpr int STROBE_CHANNEL = 10;   // Single channel
```

### Setup
```cpp
auto& dmx = chain.add<DMXOut>("dmx");
dmx.port("/dev/tty.usbserial-EN123456");  // macOS
// dmx.port("COM3");                       // Windows
dmx.universe = 1;
dmx.startChannel = 1;
```

### Setting Channels
```cpp
// Single channel (0-255)
dmx.channel(10, 200);

// RGB fixture (3 channels)
dmx.rgb(1, 255, 0, 127);  // Channels 1,2,3 = R,G,B

// RGBW fixture (4 channels)
dmx.rgbw(1, 255, 0, 127, 50);

// Multiple channels at once
dmx.channels(1, {255, 128, 64, 32});

// Blackout (all channels to 0)
dmx.blackout();
```

### Reading Current Values
```cpp
uint8_t val = dmx.getChannel(1);
const auto& buffer = dmx.dmxBuffer();  // All 512 channels
```

## Audio-Reactive Lighting

### Bass Pulse
```cpp
float bass = fft.band(0);
uint8_t brightness = static_cast<uint8_t>(bass * 255);
dmx.channel(1, brightness);
```

### Color from Frequency Bands
```cpp
float bass = fft.band(0);   // Red
float mid = fft.band(2);    // Green
float high = fft.band(5);   // Blue

dmx.rgb(1,
    static_cast<uint8_t>(bass * 255),
    static_cast<uint8_t>(mid * 255),
    static_cast<uint8_t>(high * 255));
```

## Chase Patterns

### Clock-Based Chase
```cpp
auto& clock = chain.add<Clock>("clock");
clock.bpm = 120.0f;
clock.division(ClockDiv::Eighth);
clock.start();

// In update():
static int step = 0;
if (clock.triggered()) {
    step = (step + 1) % 4;
}

// Light one fixture at a time
for (int i = 0; i < 4; i++) {
    dmx.channel(i * 3 + 1, (i == step) ? 255 : 0);
}
```

### Time-Based Sweep
```cpp
float phase = std::fmod(ctx.time() * 0.5f, 1.0f);
for (int i = 0; i < 4; i++) {
    float dist = std::abs(phase - i / 4.0f);
    uint8_t val = static_cast<uint8_t>((1.0f - dist * 4.0f) * 255);
    dmx.channel(i + 1, val);
}
```

## Common Fixture Types

### RGB Par Can (3 channels)
- Ch1: Red (0-255)
- Ch2: Green (0-255)
- Ch3: Blue (0-255)

### RGBW Par (4 channels)
- Ch1: Red
- Ch2: Green
- Ch3: Blue
- Ch4: White

### Moving Head (typically 8-16 channels)
- Ch1-2: Pan (coarse/fine)
- Ch3-4: Tilt (coarse/fine)
- Ch5: Speed
- Ch6: Dimmer
- Ch7-9: RGB
- etc.

### Strobe
- Single channel: 0=off, 1-255=speed

### Fog Machine
- Ch1: Fog output (0-255)
- Ch2: Fan speed (optional)

## Finding Your Port

### macOS
```bash
ls /dev/tty.usbserial*
# e.g., /dev/tty.usbserial-EN123456
```

### Windows
```
Device Manager > Ports (COM & LPT)
# e.g., COM3
```

### Linux
```bash
ls /dev/ttyUSB*
# e.g., /dev/ttyUSB0
```
