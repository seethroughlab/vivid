# New Audio Operators

Proposed operators for IDM, ambient, and EDM production, organized into logical implementation groups.

---

## Group 1 — Dynamics

Core gain-staging and loudness control. Foundation for mix-ready output and sidechain pumping effects common in EDM.

### Compressor

Dynamics processor with optional sidechain input for ducking effects.

- **Parameters:** threshold, ratio, attack, release, knee, makeup gain, sidechain source
- **Genre relevance:** EDM sidechain pumping, IDM transient shaping, ambient level control

### Limiter

Brickwall limiter for final-stage loudness maximization and peak protection.

- **Parameters:** ceiling, release, lookahead
- **Genre relevance:** Master bus limiting across all genres

---

## Group 2 — Modulation Effects

Time-varying filters and delays that add movement and texture. Share a common LFO-modulated delay line architecture.

### Chorus

Multi-voice detuning effect for thickening signals.

- **Parameters:** rate, depth, voices, mix
- **Genre relevance:** Ambient pad widening, EDM synth thickening

### Phaser

All-pass filter chain with LFO sweep for sweeping notch effects.

- **Parameters:** rate, depth, stages, feedback, mix
- **Genre relevance:** IDM timbral motion, ambient evolving textures

### Flanger

Short modulated delay with feedback for metallic/jet sweep effects.

- **Parameters:** rate, depth, feedback, mix
- **Genre relevance:** EDM risers and transitions, IDM metallic textures

---

## Group 3 — Granular & Spectral

Sample-level decomposition and resynthesis. Enables the complex timbral manipulation central to IDM and ambient.

### Granular Synth

Decomposes audio into grains for time-stretching, pitch-shifting, and textural transformation.

- **Parameters:** grain size, density, position, pitch shift, randomization, window shape
- **Genre relevance:** Ambient soundscapes, IDM glitch textures, EDM vocal manipulation

### Spectral Freeze

Captures and sustains a single spectral frame indefinitely.

- **Parameters:** freeze trigger, blend, spectral smoothing, FFT size
- **Genre relevance:** Ambient drones, IDM transitional textures

---

## Group 4 — Spatial

Stereo field placement and spatial echo effects for depth and width.

### Stereo Pan/Width

Panning and stereo width control with mid/side processing.

- **Parameters:** pan, width, mid/side balance
- **Genre relevance:** Mix spatialization across all genres

### Ping-Pong Delay

Stereo delay with alternating left/right taps for rhythmic spatial effects.

- **Parameters:** delay time, feedback, ping-pong spread, filter, mix
- **Genre relevance:** EDM rhythmic echoes, IDM polyrhythmic delay patterns, ambient space

---

## Group 5 — Synthesis & Transformation

Sound generation and radical timbral reshaping. These operators produce or heavily transform audio rather than applying effects.

### FM Synth

Frequency modulation synthesizer for complex harmonic and inharmonic timbres.

- **Parameters:** carrier freq, modulator freq, modulation index, envelope, operators
- **Genre relevance:** IDM metallic/bell tones, EDM bass and lead synthesis

### Ring Modulator

Multiplies two signals to produce sum and difference frequencies.

- **Parameters:** carrier freq, mix, carrier waveform
- **Genre relevance:** IDM inharmonic textures, ambient metallic drones

### Vocoder

Spectral envelope transfer between a modulator (voice) and carrier (synth).

- **Parameters:** bands, carrier source, envelope follow speed, mix
- **Genre relevance:** EDM vocal effects, IDM robotic textures

### Parametric EQ

Multi-band parametric equalizer for surgical frequency shaping.

- **Parameters:** band count, frequency, gain, Q, filter type per band
- **Genre relevance:** Mix correction and tonal shaping across all genres
