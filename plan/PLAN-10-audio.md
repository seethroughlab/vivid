# PLAN-10: Audio System

Audio input, analysis, synthesis, and external protocol support for Vivid.

## Overview

The audio system enables:
1. **Audio Analysis** — FFT, band detection, beat tracking for audio-reactive visuals
2. **Audio Synthesis** — Oscillators, envelopes, filters for sound generation
3. **Audio Playback** — Sample playback synced to visuals
4. **External Protocols** — MIDI and OSC for hardware control

```
┌─────────────────────────────────────────────────────────────────┐
│                       AUDIO SYSTEM                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Input                  Analysis              Output            │
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────┐  │
│  │ System Audio │      │ FFT (bands)  │      │ Audio Player │  │
│  │ (loopback)   │─────▶│ Beat Detect  │─────▶│              │  │
│  │ Microphone   │      │ Onset Detect │      └──────────────┘  │
│  └──────────────┘      └──────────────┘                        │
│                                                                 │
│  Synthesis             External Protocols                       │
│  ┌──────────────┐      ┌──────────────┐                        │
│  │ Oscillators  │      │ MIDI In/Out  │                        │
│  │ Envelopes    │      │ OSC In/Out   │                        │
│  │ Filters      │      │              │                        │
│  └──────────────┘      └──────────────┘                        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 12.3: Audio Input & Analysis

### AudioIn Operator

Capture audio from system audio or microphone and analyze frequency content.

```cpp
class AudioIn : public Operator {
public:
    // Fluent API
    AudioIn& source(AudioSource src);  // System, Microphone, File
    AudioIn& fftSize(int size);        // 512, 1024, 2048, 4096
    AudioIn& smoothing(float s);       // 0.0-1.0, temporal smoothing

    // Outputs
    // - "spectrum"  : Full FFT spectrum (texture or array)
    // - "bass"      : Low frequency energy (0-250 Hz)
    // - "mids"      : Mid frequency energy (250-2000 Hz)
    // - "highs"     : High frequency energy (2000+ Hz)
    // - "volume"    : Overall RMS volume
    // - "waveform"  : Raw waveform samples
};
```

### Implementation Tasks

- [ ] **Platform Audio Capture**
  - [ ] macOS: Core Audio / AVAudioEngine
  - [ ] Windows: WASAPI loopback capture
  - [ ] Linux: PulseAudio / JACK

- [ ] **FFT Analysis**
  - [ ] Use KissFFT (lightweight, MIT license) or pffft
  - [ ] Hanning/Hamming windowing
  - [ ] Log-scale frequency binning
  - [ ] Temporal smoothing (exponential decay)

- [ ] **Band Energy Extraction**
  - [ ] Configurable frequency bands
  - [ ] Default: bass (20-250 Hz), mids (250-2000 Hz), highs (2000-20000 Hz)
  - [ ] Normalized 0-1 output

---

## Phase 12.5: Beat Detection

Detect beats and tempo from audio input.

### BeatDetector Class

```cpp
class BeatDetector {
public:
    struct BeatInfo {
        bool isBeat;           // True on beat onset
        float confidence;      // 0-1, how confident
        float bpm;             // Estimated tempo
        float phase;           // 0-1, position in beat cycle
        int beatCount;         // Total beats detected
    };

    void process(const float* samples, int count);
    BeatInfo getInfo() const;

    // Configuration
    void setMinBPM(float bpm);  // Default: 60
    void setMaxBPM(float bpm);  // Default: 180
    void setSensitivity(float s); // 0-1
};
```

### Algorithm Options

1. **Energy-Based Detection**
   - Track energy in bass frequencies
   - Detect sudden increases (onset detection)
   - Simple but effective for electronic music

2. **Onset Strength Envelope**
   - Spectral flux (change between FFT frames)
   - Peak picking with adaptive threshold
   - Works across genres

3. **Tempo Estimation**
   - Autocorrelation of onset envelope
   - Multiple tempo hypotheses
   - Tempo tracking with phase lock

### Implementation Tasks

- [ ] Implement onset detection (energy-based)
- [ ] Add spectral flux algorithm
- [ ] Implement tempo estimation
- [ ] Add phase tracking (predict next beat)
- [ ] Expose as `Beat` operator or Context method

---

## Phase 12.6: Audio Synthesis

Generate audio from code for sound design and audio-visual sync.

### Oscillator Types

```cpp
enum class OscillatorType {
    Sine,
    Square,
    Sawtooth,
    Triangle,
    Noise,       // White noise
    PinkNoise    // 1/f noise
};

class Oscillator {
public:
    Oscillator& type(OscillatorType t);
    Oscillator& frequency(float hz);
    Oscillator& amplitude(float amp);
    Oscillator& phase(float p);  // 0-1

    float sample(double time);
};
```

### Envelope Generator

```cpp
class ADSR {
public:
    ADSR& attack(float seconds);   // 0.001 - 10
    ADSR& decay(float seconds);    // 0.001 - 10
    ADSR& sustain(float level);    // 0 - 1
    ADSR& release(float seconds);  // 0.001 - 10

    void noteOn();
    void noteOff();
    float sample(double dt);
};
```

### Filter Types

```cpp
enum class FilterType {
    LowPass,
    HighPass,
    BandPass,
    Notch
};

class Filter {
public:
    Filter& type(FilterType t);
    Filter& cutoff(float hz);
    Filter& resonance(float q);  // 0.1 - 10

    float process(float input);
};
```

### Synth Operator

```cpp
class Synth : public Operator {
public:
    Synth& oscillator(OscillatorType type);
    Synth& frequency(float hz);
    Synth& frequency(const std::string& node);  // From LFO/envelope
    Synth& envelope(float a, float d, float s, float r);
    Synth& filter(FilterType type, float cutoff, float resonance);

    void noteOn();
    void noteOff();

    // Output: audio samples to audio output or file
};
```

### Implementation Tasks

- [ ] Implement basic oscillators (sine, square, saw, tri)
- [ ] Implement noise generators
- [ ] Implement ADSR envelope
- [ ] Implement biquad filter (LP, HP, BP, notch)
- [ ] Create Synth operator wrapper
- [ ] Audio output to speakers
- [ ] Audio recording to file

---

## Phase 12.7: MIDI Support

Receive and send MIDI messages for hardware control.

### MIDI Input

```cpp
class MidiIn : public Operator {
public:
    MidiIn& port(const std::string& name);  // Device name or index
    MidiIn& channel(int ch);  // 1-16, or 0 for all

    // Outputs (per-channel if monitoring specific channel)
    // - "noteOn"     : Note number of last note on (0-127)
    // - "noteOff"    : Note number of last note off
    // - "velocity"   : Velocity of last note (0-127)
    // - "cc:{n}"     : Control change value for CC# n
    // - "pitchBend"  : Pitch bend (-1 to +1)
    // - "aftertouch" : Channel aftertouch (0-127)
};
```

### MIDI Output

```cpp
class MidiOut : public Operator {
public:
    MidiOut& port(const std::string& name);
    MidiOut& channel(int ch);

    void noteOn(int note, int velocity);
    void noteOff(int note);
    void controlChange(int cc, int value);
    void pitchBend(int value);
};
```

### Implementation Tasks

- [ ] Use RtMidi library (cross-platform, MIT license)
- [ ] Enumerate available MIDI ports
- [ ] Parse incoming MIDI messages
- [ ] Generate outgoing MIDI messages
- [ ] Create MidiIn operator
- [ ] Create MidiOut operator
- [ ] MIDI clock sync (receive/send)
- [ ] MIDI learn functionality

---

## Phase 12.8: OSC Support

Open Sound Control for network communication with other software.

### OSC Receiver

```cpp
class OscIn : public Operator {
public:
    OscIn& port(int port);  // UDP port to listen on
    OscIn& address(const std::string& pattern);  // OSC address pattern

    // Outputs based on address pattern
    // Example: address("/audio/level") outputs "level" as float
};
```

### OSC Sender

```cpp
class OscOut : public Operator {
public:
    OscOut& host(const std::string& hostname);
    OscOut& port(int port);

    void send(const std::string& address, float value);
    void send(const std::string& address, const std::vector<float>& values);
};
```

### Implementation Tasks

- [ ] Use oscpack library (public domain) or liblo
- [ ] UDP socket setup
- [ ] OSC message parsing
- [ ] OSC message creation
- [ ] Create OscIn operator
- [ ] Create OscOut operator
- [ ] Pattern matching for address filtering
- [ ] OSC bundle support

---

## Dependencies

| Library | Purpose | License |
|---------|---------|---------|
| KissFFT | FFT analysis | BSD |
| RtMidi | MIDI I/O | MIT |
| oscpack | OSC protocol | Public Domain |
| miniaudio | Audio I/O | Public Domain |
| libsoundio | Audio I/O (alternative) | MIT |

---

## Implementation Order

1. **Audio Input** — Platform audio capture with FFT
2. **Band Analysis** — Bass/mids/highs extraction
3. **Beat Detection** — Onset and tempo tracking
4. **MIDI Input** — Hardware controller support
5. **OSC Support** — Network communication
6. **Audio Synthesis** — Oscillators, envelopes, filters
7. **MIDI Output** — Control external hardware

---

## References

- [KissFFT](https://github.com/mborgerding/kissfft)
- [RtMidi](https://github.com/thestk/rtmidi)
- [oscpack](https://code.google.com/archive/p/oscpack/)
- [miniaudio](https://miniaud.io/)
- [Onset Detection Overview](https://www.dafx.de/paper-archive/2006/papers/p155.pdf)
- [Beat Tracking Survey](https://www.eecs.qmul.ac.uk/~siMDon/pub/2007/DaviesPlumbley07-taslp.pdf)
