# MIDI Input

Demonstrates hardware MIDI input/output with visual feedback.

## Operators Used

- **MidiIn** - Receive MIDI from hardware controllers
- **MidiOut** - Send MIDI to external synthesizers/DAWs
- **MidiFilePlayer** - Play Standard MIDI Files

## Key Concepts

### MidiIn - Receiving MIDI
```cpp
auto& midiIn = chain.add<MidiIn>("midiIn");

// List available ports
for (const auto& port : MidiIn::listPorts()) {
    printf("Port: %s\n", port.c_str());
}

// Open a port
midiIn.openPort(0);                    // By index
midiIn.openPortByName("Arturia");      // By name (partial match)

// Channel filter (0 = omni/all channels)
midiIn.channel = 1;  // Only channel 1

// Poll events each frame
for (const auto& e : midiIn.events()) {
    switch (e.type) {
        case MidiEventType::NoteOn:
            // e.note (0-127), e.velocity (0-127), e.channel (0-15)
            break;
        case MidiEventType::NoteOff:
            break;
        case MidiEventType::ControlChange:
            // e.cc (0-127), e.value (0-127)
            break;
        case MidiEventType::PitchBend:
            // e.pitchBend (-8192 to 8191)
            break;
    }
}

// Convenience methods
if (midiIn.noteOn()) {
    int note = midiIn.note();         // Most recent note
    float vel = midiIn.velocity();    // 0.0 to 1.0
}

float modWheel = midiIn.cc(1);        // CC1 value (0.0 to 1.0)
float pitchBend = midiIn.pitchBend(); // -1.0 to +1.0
```

### MidiOut - Sending MIDI
```cpp
auto& midiOut = chain.add<MidiOut>("midiOut");
midiOut.openPortByName("IAC Driver");

// Send messages
midiOut.noteOn(0, 60, 0.8f);    // Channel, note, velocity (0-1)
midiOut.noteOff(0, 60);         // Channel, note
midiOut.sendCC(0, 1, 0.5f);     // Channel, CC#, value (0-1)
midiOut.sendPitchBend(0, 0.5f); // Channel, bend (-1 to +1)
midiOut.programChange(0, 5);    // Channel, program

// Panic (all notes off)
midiOut.allNotesOff(0);         // Single channel
midiOut.panic();                // All channels
```

### MidiFilePlayer - SMF Playback
```cpp
auto& player = chain.add<MidiFilePlayer>("player");
player.load("song.mid");
player.loop = true;
player.play();

// Tempo sync
player.syncToClock(&clock);  // Use Clock's BPM
player.useFileTempo();       // Use file's tempo

// Transport
player.play();
player.pause();
player.stop();
player.seek(10.0);  // Seconds

// File info
int tracks = player.trackCount();
double duration = player.durationSeconds();
double tempo = player.tempo();

// Poll events (same as MidiIn)
for (const auto& e : player.events()) {
    if (e.type == MidiEventType::NoteOn) {
        synth.noteOn(midiToFreq(e.note));
    }
}
```

## MIDI Event Types
```cpp
enum class MidiEventType {
    NoteOff,
    NoteOn,
    PolyPressure,
    ControlChange,
    ProgramChange,
    ChannelPressure,
    PitchBend
};

struct MidiEvent {
    MidiEventType type;
    uint8_t channel;    // 0-15
    uint8_t note;       // 0-127
    uint8_t velocity;   // 0-127
    uint8_t cc;         // CC number (0-127)
    uint8_t value;      // CC value (0-127)
    int16_t pitchBend;  // -8192 to 8191
};
```

## Common Patterns

### MIDI-Controlled Visuals
```cpp
// Map CC to effect parameter
auto& blur = chain.get<Blur>("blur");
blur.radius = midiIn.cc(1) * 20.0f;  // CC1 controls blur

// Note-triggered flash
if (midiIn.noteOn()) {
    flash.trigger(midiIn.velocity());
}
```

### MIDI Echo/Thru
```cpp
for (const auto& e : midiIn.events()) {
    if (e.type == MidiEventType::NoteOn) {
        midiOut.noteOn(e.channel, e.note, e.velocity / 127.0f);
    }
}
```

### Note-to-Frequency Conversion
```cpp
float midiToFreq(int note) {
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

// Usage with synth
if (midiIn.noteOn()) {
    float freq = midiToFreq(midiIn.note());
    oscillator.frequency = freq;
}
```

## Platform Notes

- Uses RtMidi for cross-platform MIDI access
- Virtual MIDI ports supported (macOS IAC Driver, Windows loopMIDI)
- Hot-plugging supported - devices can connect/disconnect during runtime
- Port names may vary by platform and driver

## Controls

Connect a MIDI controller to see visual feedback for notes and CC values.
