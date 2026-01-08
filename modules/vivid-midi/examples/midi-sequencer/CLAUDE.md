# MIDI Sequencer

Send MIDI notes to external synthesizers.

## Operators Used

- **MidiOut** - Send MIDI messages to hardware/software
- **Clock** - Tempo control
- **Sequencer** - Step pattern

## Hardware Requirements

- MIDI interface or virtual MIDI (IAC Driver on macOS)
- External synthesizer or DAW receiving MIDI

## Key Concepts

### MIDI Output Setup
```cpp
auto& midi = chain.add<MidiOut>("midi");

// List available ports
auto ports = MidiOut::listPorts();

// Open by index
midi.openPort(0);

// Or by name (partial match)
midi.openPortByName("IAC Driver");
```

### Send MIDI Messages
```cpp
// Note on/off
midi.noteOn(0, 60, 0.8f);   // Channel 0, middle C, velocity 80%
midi.noteOff(0, 60);

// Control Change (CC)
midi.sendCC(0, 1, 0.5f);    // Mod wheel to 50%
midi.sendCC(0, 7, 0.7f);    // Volume to 70%

// Pitch Bend
midi.sendPitchBend(0, 0.5f);  // Center = 0.5, range 0-1

// Program Change
midi.sendProgramChange(0, 5);  // Change to program 5
```

### Sequencer Integration
```cpp
if (clock.triggered()) {
    // Turn off previous note
    midi.noteOff(0, lastNote);
    
    seq.advance();
    if (seq.triggered()) {
        int note = noteSequence[seq.currentStep()];
        float vel = seq.currentVelocity();
        midi.noteOn(0, note, vel);
        lastNote = note;
    }
}
```

## macOS Virtual MIDI Setup

1. Open **Audio MIDI Setup**
2. Window → Show MIDI Studio
3. Double-click **IAC Driver**
4. Check "Device is online"
5. Use "IAC Driver Bus 1" in your DAW

## Controls

- **Mouse X** - BPM (60-180)
- **Mouse Y** - Velocity (0.3-1.0)

## Common MIDI CC Numbers

| CC | Name | Use |
|----|------|-----|
| 1 | Modulation | Vibrato, filter |
| 7 | Volume | Channel volume |
| 10 | Pan | Left/right |
| 64 | Sustain | Pedal |
| 74 | Cutoff | Filter frequency |
| 71 | Resonance | Filter Q |

## Tips

- Always send noteOff before the next noteOn on same note
- Use velocity 0 for noteOff if your synth expects it
- Keep note numbers in MIDI range (0-127)
