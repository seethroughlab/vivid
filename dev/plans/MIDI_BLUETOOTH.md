# MIDI over Bluetooth (BLE-MIDI) - Future Implementation

This document explores MIDI over Bluetooth Low Energy (BLE-MIDI) for future implementation in Vivid.

## Overview

BLE-MIDI allows wireless MIDI communication with iOS/Android apps, wireless MIDI controllers, and other Bluetooth-enabled devices. This enables:

- Wireless control from mobile apps (GarageBand, Koala Sampler, etc.)
- Connection to Bluetooth MIDI controllers (Korg nanoKEY, Akai LPK25, etc.)
- Cross-platform wireless MIDI without hardware dongles

## BLE-MIDI Protocol

BLE-MIDI uses a standardized Bluetooth LE profile defined by Apple and adopted by the MIDI Manufacturers Association (MMA).

### Key Characteristics

- **Service UUID**: `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`
- **Characteristic UUID**: `7772E5DB-3868-4112-A1A9-F2669D106BF3`
- **Data Format**: Timestamped MIDI packets with header byte
- **MTU**: Typically 20-512 bytes per packet
- **Latency**: 10-30ms typical (acceptable for most uses)

### Packet Format

```
[Header] [Timestamp High] [Timestamp Low] [Status] [Data1] [Data2] ...
```

- Header byte indicates timestamp presence (bit 7) and running status (bit 6)
- Timestamps are 13-bit millisecond values with 8192 wraparound
- Multiple MIDI messages can be packed in a single BLE packet

## Platform-Specific APIs

### macOS / iOS (CoreMIDI)

Apple provides native BLE-MIDI support through CoreMIDI since macOS 10.11 and iOS 8.0.

**Advantages:**
- First-class system integration
- BLE-MIDI devices appear as standard MIDI ports
- Automatic device discovery and pairing
- System handles all BLE-MIDI packet encoding/decoding

**Implementation:**
```objc
// CoreMIDI with BLE-MIDI (Objective-C)
#import <CoreMIDI/CoreMIDI.h>

// BLE-MIDI devices appear as standard MIDI sources/destinations
// No special handling required - use existing MIDIPacketList APIs

// To discover BLE devices explicitly:
MIDINetworkSession* session = [MIDINetworkSession defaultSession];
session.enabled = YES;
session.connectionPolicy = MIDINetworkConnectionPolicy_Anyone;
```

**For Vivid:** The existing RtMidi-based MidiIn/MidiOut should work with BLE-MIDI devices on macOS because CoreMIDI handles the translation. No code changes needed for basic support.

### Windows (Windows MIDI Services)

Windows 10+ supports BLE-MIDI through Windows MIDI Services (WinRT API).

**Implementation:**
```cpp
// Windows Runtime API
#include <winrt/Windows.Devices.Midi.h>
#include <winrt/Windows.Devices.Enumeration.h>

// Enumerate BLE-MIDI devices
auto selector = MidiInPort::GetDeviceSelector();
auto devices = await DeviceInformation::FindAllAsync(selector);
for (auto device : devices) {
    if (device.Properties().HasKey(L"System.Devices.Aep.Bluetooth.Le.IsConnectable")) {
        // This is a BLE-MIDI device
    }
}
```

**Challenges:**
- RtMidi uses WinMM (older API) which doesn't support BLE-MIDI
- Need to either:
  1. Use Windows MIDI Services API directly
  2. Wait for RtMidi to add WinRT backend
  3. Create a bridge layer

### Linux (BlueZ + ALSA)

Linux support requires BlueZ 5.x and ALSA with BLE-MIDI plugin.

**Status:** Experimental, requires manual configuration

**Implementation:**
```bash
# Pair BLE-MIDI device via bluetoothctl
bluetoothctl
> scan on
> pair <MAC_ADDRESS>
> trust <MAC_ADDRESS>

# Create ALSA MIDI port (requires bluez-alsa-midi)
bluealsa-aplay -v --profile-midi <MAC_ADDRESS>
```

**For Vivid:** Would require significant work to support on Linux. Consider it lower priority.

### Android (Android MIDI API)

Android has native BLE-MIDI support since Android 6.0 (API level 23).

```java
// android.media.midi
MidiManager midiManager = (MidiManager) context.getSystemService(Context.MIDI_SERVICE);
midiManager.registerDeviceCallback(new MidiManager.DeviceCallback() {
    @Override
    public void onDeviceAdded(MidiDeviceInfo device) {
        // Check for BLE-MIDI
        Bundle properties = device.getProperties();
        if (properties.getInt(MidiDeviceInfo.PROPERTY_BLUETOOTH_DEVICE) != 0) {
            // This is a BLE-MIDI device
        }
    }
}, handler);
```

**For Vivid:** Not currently relevant (no Android build), but good to document for future mobile companion apps.

## Library Options

### 1. RtMidi (Current)

- **BLE-MIDI Support:** Native on macOS (via CoreMIDI), not on Windows/Linux
- **Recommendation:** Continue using for wired MIDI; add platform-specific BLE layers

### 2. JUCE

- **BLE-MIDI Support:** Yes, cross-platform
- **License:** GPLv3 or commercial
- **Consideration:** Significant dependency, but very mature

### 3. libremidi

- **BLE-MIDI Support:** Partial (macOS native, Windows planned)
- **License:** BSD-2-Clause
- **Status:** Active development, modern C++ API

### 4. Custom Implementation

- Direct platform API usage
- Most control, most work
- Recommended only if specific needs not met by libraries

## Recommended Implementation Approach

### Phase 1: macOS Native (No Code Changes)

BLE-MIDI devices already work on macOS through CoreMIDI, which RtMidi uses. Users can:
1. Pair BLE-MIDI device in System Preferences > Bluetooth
2. Open Audio MIDI Setup
3. Enable BLE device in MIDI Studio window
4. Device appears as standard MIDI port

**Documentation needed:** Add user guide for BLE-MIDI setup.

### Phase 2: Windows Support

1. Create `MidiBleWin` class using Windows MIDI Services
2. Enumerate BLE-MIDI devices separately
3. Bridge to existing MIDI event system
4. Fall back to RtMidi for non-BLE devices

### Phase 3: Device Discovery UI

Add operator or MCP tools for BLE-MIDI:
- `listBleMidiDevices()` - List discovered BLE-MIDI devices
- `connectBleMidiDevice(name)` - Connect to specific device
- `disconnectBleMidiDevice(name)` - Disconnect from device

## Mobile App Integration Patterns

### GarageBand / Logic Remote

- Advertises as BLE-MIDI peripheral
- Vivid acts as central (receiver)
- Can receive notes, CCs, and control data

### TouchOSC

- Can act as either central or peripheral
- Custom OSC + MIDI layouts
- Vivid can receive MIDI over BLE-MIDI

### Koala Sampler / Other iOS Apps

- Many iOS music apps support BLE-MIDI output
- Enable "MIDI Out" in app settings
- Pair with computer's Bluetooth
- Appears as MIDI input

### Pattern: Vivid as BLE-MIDI Peripheral

Future enhancement: Let Vivid advertise as BLE-MIDI peripheral so mobile apps can connect directly:

```cpp
// Hypothetical API
auto& bleOut = chain.add<BleMidiOut>("ble");
bleOut.setDeviceName("Vivid");
bleOut.advertise();  // Start advertising

// Mobile app connects, receives MIDI from Vivid
```

## Current MIDI Routing Architecture

Vivid's MIDI system uses two key interfaces that enable flexible routing:

### MidiReceiver Interface
Audio operators that can receive MIDI implement `MidiReceiver`:
- `PolySynth`, `WavetableSynth`, `FMSynth`, `Synth`, `Sampler`
- `DrumKit` - MIDI-controlled drum machine
- `Arpeggiator` - receives held notes, outputs arpeggiated patterns

### MidiSender Interface
Operators that can send MIDI externally implement `MidiSender`:
- `MidiOut` - sends to external hardware/software via RtMidi

### Sequencer/Arpeggiator → External Gear

The Sequencer and Arpeggiator can route MIDI to both internal synths and external devices:

```cpp
auto& clock = chain.add<Clock>("clock");
clock.bpm = 120.0f;
clock.division(ClockDiv::Sixteenth);

// Sequencer generates notes
auto& seq = chain.add<Sequencer>("seq");
seq.setTriggerSource("clock");

// Internal synth receives MIDI
auto& synth = chain.add<PolySynth>("synth");
seq.setTarget("synth");

// External device also receives MIDI (including future BLE-MIDI)
auto& midiOut = chain.add<MidiOut>("midiOut");
midiOut.openPortByName("External Synth");  // Or BLE-MIDI device
seq.setMidiOutput("midiOut");

// Pattern: C minor arpeggio
seq.setStep(0, 60, 0.8f);   // C4
seq.setStep(4, 63, 0.7f);   // Eb4
seq.setStep(8, 67, 0.8f);   // G4
seq.setStep(12, 72, 0.9f);  // C5
```

When BLE-MIDI is fully implemented, the same `MidiOut` operator would work with Bluetooth devices - users would simply select a BLE-MIDI port name instead of a USB MIDI port.

## Latency Considerations

| Connection Type | Typical Latency |
|----------------|-----------------|
| USB MIDI       | 1-3ms           |
| BLE-MIDI       | 10-30ms         |
| WiFi MIDI (RTP) | 5-20ms         |
| DIN MIDI       | 1-2ms           |

BLE-MIDI latency is acceptable for:
- Live performance (with practice)
- Parameter control
- Non-critical triggering

Consider wired MIDI for:
- Tight timing requirements
- Drum triggering
- Sync-critical applications

## Security Considerations

- BLE-MIDI uses standard Bluetooth pairing
- Devices must be explicitly paired before use
- No authentication beyond pairing
- Consider: Should Vivid require user confirmation for new BLE-MIDI connections?

## Implementation Timeline

1. **Short term (v0.2):** Document macOS BLE-MIDI setup for users
   - ✅ MidiReceiver/MidiSender interfaces implemented
   - ✅ Sequencer can route to MidiOut for external devices
   - ✅ Arpeggiator can route to MidiOut for external devices
   - ✅ DrumKit receives MIDI from Sequencer
   - Remaining: User documentation for BLE-MIDI pairing workflow
2. **Medium term (v0.3):** Add Windows MIDI Services support
3. **Long term (v0.4+):** Device discovery UI, peripheral mode

## References

- [BLE-MIDI Specification (MMA)](https://www.midi.org/specifications/midi-transports-specifications/midi-over-bluetooth-low-energy-midi-ble)
- [Apple CoreMIDI Documentation](https://developer.apple.com/documentation/coremidi)
- [Windows MIDI Services](https://docs.microsoft.com/en-us/windows/uwp/audio-video-camera/midi)
- [RtMidi Library](https://www.music.mcgill.ca/~gary/rtmidi/)
- [libremidi](https://github.com/jcelerier/libremidi)
