# vivid-serial

Serial communication addon for Vivid. Supports Arduino, sensors, and DMX lighting via Enttec devices.

## Operators

- **SerialOut** - Send data to serial devices (Arduino, etc.)
- **SerialIn** - Receive data from serial devices (sensors, etc.)
- **DMXOut** - DMX lighting output via Enttec USB Pro

## Installation

### Development (with vivid source)

```bash
cd ~/Developer/vivid-serial
cmake -B build \
    -DVIVID_SOURCE_DIR=~/Developer/vivid \
    -DVIVID_BUILD_DIR=~/Developer/vivid/build
cmake --build build
```

### User Installation

Copy the built library to `~/.vivid/addons/vivid-serial/`.

## Usage

### Arduino LED Control

```cpp
#include <vivid/vivid.h>
#include <vivid/serial/serial_out.h>

using namespace vivid;

void setup(Context& ctx) {
    auto& lfo = ctx.chain().add<LFO>("pulse");
    lfo.frequency = 0.5f;

    auto& serial = ctx.chain().add<SerialOut>("arduino");
    serial.port("/dev/tty.usbmodem14201");
    serial.baudRate = 9600;
}

void update(Context& ctx) {
    ctx.chain().process();

    auto& serial = ctx.chain().get<SerialOut>("arduino");
    float brightness = ctx.chain().get<LFO>("pulse").value();
    serial.sendInt(int(brightness * 255));
}

VIVID_CHAIN(setup, update)
```

### DMX Lighting

```cpp
#include <vivid/vivid.h>
#include <vivid/serial/dmx_out.h>

using namespace vivid;

void setup(Context& ctx) {
    auto& dmx = ctx.chain().add<DMXOut>("dmx");
    dmx.port("/dev/tty.usbserial-EN123456");
}

void update(Context& ctx) {
    ctx.chain().process();

    auto& dmx = ctx.chain().get<DMXOut>("dmx");
    dmx.rgb(1, 255, 0, 128);  // Set RGB fixture on channels 1-3
}

VIVID_CHAIN(setup, update)
```

## Platform Support

- **macOS**: Uses IOKit for port enumeration, POSIX for I/O
- **Windows**: Uses Win32 serial API
- **Linux**: Uses POSIX, optionally libserialport for better enumeration

## License

MIT
