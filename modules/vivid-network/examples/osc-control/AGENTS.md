# OSC Control

External control via OSC (Open Sound Control).

## Operators Used

- **OscIn** - Receive OSC messages
- **OscOut** - Send OSC messages
- **Flash** - Triggerable visual effect

## Hardware Requirements

This example requires an OSC controller:
- **TouchOSC** (iOS/Android) - Most common
- **Lemur** (iOS) - Advanced layouts
- **Max/MSP, PureData** - Software OSC

## Key Concepts

### OSC Receiver Setup
```cpp
auto& osc = chain.add<OscIn>("osc");
osc.port(8000);  // Listen on UDP port 8000
```

### Reading Messages
```cpp
if (osc.hasMessage("/fader/1")) {
    float value = osc.getFloat("/fader/1");
}

// Get with default
float value = osc.getFloat("/fader/1", 0.5f);

// Multiple arguments
float x = osc.getFloat("/xy/1", 0);
float y = osc.getFloat("/xy/1", 1);
```

### OSC Sender
```cpp
auto& oscOut = chain.add<OscOut>("oscOut");
oscOut.host("192.168.1.100");
oscOut.port(9000);

oscOut.send("/led/1", 1.0f);
oscOut.send("/position", 0.5f, 0.3f);
```

## TouchOSC Setup

1. In TouchOSC Settings:
   - **Host**: Your computer IP
   - **Port (outgoing)**: 8000
   - **Port (incoming)**: 9000

## Address Mapping

| Address | Type | Controls |
|---------|------|----------|
| /fader/1 | float | Hue |
| /fader/2 | float | Saturation |
| /fader/3 | float | Size |
| /xy/1 | float,float | Noise |
| /button/1 | float | Flash |
| /knob/1 | float | Rotation |

## Testing Without Hardware

```bash
# Using oscsend
oscsend localhost 8000 /fader/1 f 0.5
oscsend localhost 8000 /button/1 f 1.0
```
