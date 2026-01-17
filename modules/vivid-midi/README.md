# vivid-midi

MIDI input, output, and routing for hardware controllers, synths, and DAWs.

## Installation

This addon is included with Vivid by default. No additional installation required.

## Operators

| Operator | Description |
|----------|-------------|
| `MidiIn` | Receive MIDI from hardware controllers and software |
| `MidiOut` | Send MIDI to synthesizers and DAWs |
| `MidiFilePlayer` | Play MIDI files with timing |

## Examples

| Example | Description |
|---------|-------------|
| [midi-input](examples/midi-input) | Basic MIDI event handling and visualization |
| [midi-synth](examples/midi-synth) | Native MIDI routing to synthesizers |
| [midi-sequencer](examples/midi-sequencer) | Clock-driven MIDI output sequencing |
| [midi-clock-sync](examples/midi-clock-sync) | Bidirectional MIDI clock sync |

## Quick Start

### Basic MIDI Input (Manual Polling)

```cpp
#include <vivid/midi/midi.h>
using namespace vivid::midi;

void setup(Context& ctx) {
    auto& midiIn = ctx.chain().add<MidiIn>("midi");
    midiIn.openPort(0);  // First available port
}

void update(Context& ctx) {
    auto& midiIn = ctx.chain().get<MidiIn>("midi");

    for (const auto& e : midiIn.events()) {
        if (e.type == MidiEventType::NoteOn) {
            // Handle note: e.note, e.velocity, e.channel
        }
    }
}
```

### Native MIDI Routing (Recommended)

Route MIDI directly to synths without manual event handling:

```cpp
#include <vivid/midi/midi.h>
#include <vivid/audio/audio.h>

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& midiIn = chain.add<MidiIn>("midi");
    midiIn.openPort(0);

    auto& synth = chain.add<PolySynth>("synth");
    synth.waveform(Waveform::Saw);

    // Route all MIDI notes to synth - velocity and pitch bend included!
    midiIn.setTarget("synth");

    auto& out = chain.add<AudioOutput>("out");
    out.input("synth");
    chain.audioOutput("out");
}

void update(Context& ctx) {
    ctx.chain().process();  // No manual MIDI handling needed
}
```

### CC-to-Parameter Mapping

Map any MIDI CC to any operator parameter:

```cpp
// CC1 (mod wheel) -> filter cutoff (scaled 200-8000 Hz)
midiIn.mapCC(1, "synth", "filterCutoff", 200.0f, 8000.0f);

// CC74 -> filter resonance (0.0 - 0.9)
midiIn.mapCC(74, "synth", "filterResonance", 0.0f, 0.9f);

// CC91 -> reverb mix (0.0 - 0.6)
midiIn.mapCC(91, "reverb", "mix", 0.0f, 0.6f);

// CC7 (volume) -> synth volume
midiIn.mapCC(7, "synth", "volume", 0.0f, 1.0f);

// Clear a specific mapping
midiIn.unmapCC(1);

// Clear all mappings
midiIn.clearCCMappings();
```

### MIDI Clock Sync

Sync to external MIDI clock (receive):

```cpp
auto& clock = chain.add<Clock>("clock");

// Receive mode: sync tempo from external MIDI clock
midiIn.setClockTarget("clock");
clock.setMidiClockSync(true);
```

Send MIDI clock to external gear:

```cpp
auto& clock = chain.add<Clock>("clock");
clock.bpm = 120.0f;
clock.start();

auto& midiOut = chain.add<MidiOut>("midiOut");
midiOut.openPortByName("External Synth");

// Send 24 PPQ clock + Start/Stop/Continue
clock.setMidiClockOutput("midiOut");
```

### MIDI Output

Send MIDI to external synths and DAWs:

```cpp
auto& midiOut = chain.add<MidiOut>("midiOut");
midiOut.openPortByName("IAC Driver");

// Send notes
midiOut.noteOn(0, 60, 0.8f);   // Channel 0, middle C, velocity 0.8
midiOut.noteOff(0, 60);

// Send control change
midiOut.sendCC(0, 1, 0.5f);    // Mod wheel to 50%

// Send pitch bend (-1.0 to +1.0)
midiOut.sendPitchBend(0, 0.25f);

// Send program change
midiOut.programChange(0, 12);

// Panic - all notes off
midiOut.panic();
```

## MidiReceiver Interface

All Vivid synths implement `MidiReceiver` for automatic MIDI routing:

- `PolySynth` - Polyphonic synthesizer
- `WavetableSynth` - Wavetable synthesizer
- `FMSynth` - FM synthesis
- `Synth` - Monophonic synthesizer
- `Sampler` - Sample playback

Features supported via MidiReceiver:
- Note on/off with velocity
- Pitch bend (configurable range, default ±2 semitones)
- All notes off / panic

## Device Discovery

```cpp
// List available ports
auto inPorts = MidiIn::listPorts();
auto outPorts = MidiOut::listPorts();

// Open by index
midiIn.openPort(0);

// Open by name (partial match, case-insensitive)
midiIn.openPortByName("Arturia");
midiOut.openPortByName("IAC Driver");
```

## Dependencies

- vivid-core
- vivid-audio (for MidiReceiver synths)
- RtMidi (bundled)

## License

MIT
