# DMX Control

Demonstrates DMX lighting control via Enttec DMX USB Pro.

## Operators Used

- **DMXOut** - Send DMX512 data to fixtures
- **LFO** - Generate oscillating color values

## Key Concepts

### DMX Output Setup
```cpp
auto& dmx = chain.add<DMXOut>("dmx");

// Set Enttec port (platform-specific)
// macOS: /dev/tty.usbserial-EN*
// Linux: /dev/ttyUSB0
// Windows: COM3, etc.
dmx.port("/dev/tty.usbserial-EN123456");
```

### Setting DMX Channels
```cpp
// Single channel (1-512)
dmx.channel(1, 255);  // Channel 1 to full

// RGB fixture (3 consecutive channels)
dmx.rgb(1,              // Start channel
    uint8_t(r * 255),   // Red
    uint8_t(g * 255),   // Green
    uint8_t(b * 255));  // Blue

// Multiple channels at once
dmx.channels(5, {100, 150, 200});  // Channels 5, 6, 7

// Set all channels to zero
dmx.blackout();
```

### Animated RGB Fixture
```cpp
// Create offset LFOs for smooth color cycling
auto& lfoR = chain.add<LFO>("red");
lfoR.frequency = 0.2f;

auto& lfoG = chain.add<LFO>("green");
lfoG.frequency = 0.3f;
lfoG.phase = 0.33f;  // 120 degree offset

auto& lfoB = chain.add<LFO>("blue");
lfoB.frequency = 0.5f;
lfoB.phase = 0.66f;  // 240 degree offset

// In update:
float r = lfoR.value();
float g = lfoG.value();
float b = lfoB.value();
dmx.rgb(1, uint8_t(r * 255), uint8_t(g * 255), uint8_t(b * 255));
```

## DMX Fixture Addressing

DMX fixtures are addressed by their starting channel. Common configurations:

| Fixture Type | Channels | Typical Mapping |
|--------------|----------|-----------------|
| RGB Par | 3 | R, G, B |
| RGBW Par | 4 | R, G, B, W |
| Moving Head | 16+ | Pan, Tilt, Color, Gobo, etc. |
| Dimmer Pack | 4-8 | Dim1, Dim2, ... |

## Hardware Requirements

- Enttec DMX USB Pro (or compatible)
- DMX cable (5-pin XLR)
- DMX-controllable fixture(s)

## Enttec Port Names

| Platform | Port Format |
|----------|-------------|
| macOS | `/dev/tty.usbserial-EN*` |
| Linux | `/dev/ttyUSB0` |
| Windows | `COM3`, `COM4`, etc. |

Find your port: `ls /dev/tty.usb*` (macOS/Linux)

## Controls

No interactive controls - RGB fixture animates automatically based on LFOs.
