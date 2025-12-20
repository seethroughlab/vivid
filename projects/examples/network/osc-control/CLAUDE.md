# OSC Control

Demonstrates receiving and sending OSC (Open Sound Control) messages for remote parameter control. Compatible with TouchOSC, Max/MSP, Pure Data, and other OSC-enabled software.

## Vision

Control visual parameters in real-time from external hardware or software. Faders adjust hue, saturation, blur, and noise scale. Button presses trigger feedback messages. Perfect for live performance setups.

## Network Configuration

- **Receive**: Port 8000
- **Send**: 127.0.0.1:9000

## OSC Address Mapping

| Address | Parameter | Range |
|---------|-----------|-------|
| `/fader/hue` | Hue shift | 0-1 |
| `/fader/sat` | Saturation | 0-1 |
| `/fader/blur` | Blur radius | 0-50 |
| `/fader/scale` | Noise scale | 1-16 |
| `/button/*` | Triggers feedback | - |

## Bidirectional Sync

- Sends `/status/fps` and `/status/time` at 10 Hz
- Sends `/feedback/button` on button press
- Allows controller UI to stay synchronized

## Testing

```bash
# Send test message
oscsend localhost 8000 /fader/hue f 0.5

# Monitor outgoing
oscdump 9000
```
