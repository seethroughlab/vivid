# Phase 3: Curated Graph A/V Review Worksheet

Reviewer: ____________________
Date: ____________________
Commit: e1f2c5b0
Audio device: ____________________
Listening level: ____________________

## Classification Key

- **ready** -- no issues
- **minor polish** -- small cosmetic/balance issues, not blocking
- **confusing but usable** -- works but could mislead a beginner
- **blocking** -- must fix or remove from beta surface
- **env skip** -- requires hardware/media not present, labeled correctly

## intro/ (9 graphs)

### 1. Hello Audio (`intro/audio_demo.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> The smallest audio patch: one oscillator through gain into the audio output.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 2. Audio-Reactive Visuals (`intro/audio_reactive_demo.json`)

**Type:** A/V | **Difficulty:** beginner | **Env:** — | **Packages:** —

> Audio level changes the scale of a noise texture so sound and image move together.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| Visual | | |
| A/V sync | | |
| **Result** | | |

### 3. Audio-Visual Sync (`intro/av_demo.json`)

**Type:** A/V | **Difficulty:** beginner | **Env:** — | **Packages:** —

> One shared LFO drives both visual scale and audio pitch so cross-domain wiring is immediately visible.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| Visual | | |
| A/V sync | | |
| **Result** | | |

### 4. Shared Metronome AV Sync (`intro/av_metronome_demo.json`)

**Type:** A/V | **Difficulty:** beginner | **Env:** — | **Packages:** —

> The graph metronome keeps separate audio and visual LFOs locked without extra clock wires.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| Visual | | |
| A/V sync | | |
| **Result** | | |

### 5. Getting Started (`intro/demo.json`)

**Type:** Visual | **Difficulty:** beginner | **Env:** — | **Packages:** —

> The smallest visual patch: clocked modulation changes a noise texture over time.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 6. Lanes: Repeat (`intro/lanes_intro_demo.json`)

**Type:** Visual | **Difficulty:** beginner | **Env:** — | **Packages:** —

> One animated value expands into lanes so several visual elements move from a shared source.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 7. Lanes: Stack (`intro/lanes_stack_demo.json`)

**Type:** Visual | **Difficulty:** beginner | **Env:** — | **Packages:** —

> Several LFOs are stacked into lanes so each visual element gets its own position.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 8. Welcome to Vivid (`intro/showcase_demo.json`)

**Type:** A/V | **Difficulty:** beginner | **Env:** — | **Packages:** —

> Three drums trigger three visual shapes so the beat can be seen and heard as one live patch.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| Visual | | |
| A/V sync | | |
| **Result** | | |

### 9. Hello Stereo (`intro/stereo_demo.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> A simple oscillator pans left and right to introduce stereo movement.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

## audio/ (15 graphs)

### 1. Transport-Synced Arpeggio (`audio/arpeggiator_metronome_demo.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> A metronome-locked chord progression and arpeggiator play a bright FM pattern that stays synced as the graph tempo changes.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 2. Transport-Locked Chorus (`audio/chorus_metronome_demo.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> A pulsing tone runs through chorus so tempo changes are easy to hear and the sweep stays locked to the shared transport.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 3. Sidechain Compression (`audio/compressor_demo.json`)

**Type:** Audio | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> A kick ducks the bass-and-noise layer for a classic pumping sidechain feel.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 4. Drum Stack Foundation (`audio/drum_stack_demo.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> A 16-step groove plays through the full drum kit so kick, snare, hats, clap, tom, and cymbal are all easy to inspect.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 5. Filter Sweep (`audio/filter_sweep.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> A resonant noise sweep pulses on the beat while the filter curve moves in the node preview.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 6. FM Synth Bell Tones (`audio/fm_synth_demo.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> Bell-like FM tones pulse on the beat while modulation depth slowly changes the timbre.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 7. Four on the Floor (`audio/four_on_the_floor.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** ConvolutionReverb | **Packages:** —

> A compact dance beat with kick, snare, hats, and clap arranged as a reliable first drum-machine patch.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 8. Complete Synth Voice (`audio/full_synth_patch.json`)

**Type:** Audio | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> A complete synth voice combines oscillator, noise, mixer, filter, envelope, and LFO modulation into one playable patch.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 9. Granular Texture Scanner (`audio/granular_synth_demo.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> A granular texture cloud scans through its source while pitch drift keeps the sound moving.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 10. Chromatic Sampler with Arpeggiator (`audio/sampler_chromatic_demo.json`)

**Type:** Audio | **Difficulty:** intermediate | **Env:** Sampler | **Packages:** —

> A chord progression drives an arpeggiated lap-steel sampler patch across chromatic notes.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 11. Step Sequencer Melody (`audio/sequencer_demo.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> An eight-step sequencer plays a simple melody that can be reshaped from the inspector.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 12. SP404 Drum Sequencer (`audio/sp404_demo.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> A sample-pad drum kit plays a tight sequenced groove with clear pad-to-note mapping.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 13. State Machine Music (`audio/state_machine_demo.json`)

**Type:** A/V | **Difficulty:** advanced | **Env:** — | **Packages:** —

> Three musical scenes switch drums, synth, and bloom visuals together from one state machine.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| Visual | | |
| A/V sync | | |
| **Result** | | |

### 14. Auto-Pan and Stereo Width (`audio/stereo_pan_width_demo.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> A simple tone pans left to right while stereo width breathes independently.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

### 15. Vocoder: Noise into Saw (`audio/vocoder_demo.json`)

**Type:** Audio | **Difficulty:** beginner | **Env:** — | **Packages:** —

> Noise shapes a saw carrier into a talking-robot texture with a slowly changing response speed.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| **Result** | | |

## gpu/ (11 graphs)

### 1. Bloom Glow (`gpu/bloom_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> A rotating star blooms into a soft glow, showing how bright shapes spill light into the frame.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 2. Composite Blend (`gpu/composite_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> An animated shape and noise texture blend together as a minimal compositing patch.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 3. Edge Blend (`gpu/edge_blend_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> A gradient demonstrates soft-edge blending for projection and multi-output alignment work.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 4. Video Feedback (`gpu/feedback_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> A star feeds back into itself to create spiraling video trails from a small graph.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 5. Instanced Shapes (`gpu/instanced_shapes_simple.json`)

**Type:** Visual | **Difficulty:** beginner | **Env:** — | **Packages:** —

> A minimal ring of instanced hexagons shows repeated geometry without extra graph clutter.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 6. Nyan Trail (`gpu/nyan_trail_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> A rainbow-cycling shape leaves a bright feedback trail across the frame.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 7. Particle Envelopes (`gpu/particle_envelope_demo.json`)

**Type:** Visual | **Difficulty:** beginner | **Env:** — | **Packages:** —

> Glowing particles fade in and out with per-particle envelopes.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 8. Path Animate AV (`gpu/path_animate_av_demo.json`)

**Type:** A/V | **Difficulty:** advanced | **Env:** — | **Packages:** —

> A tone drives the speed of a shape moving along a path, tying audio level to visual motion.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| Visual | | |
| A/V sync | | |
| **Result** | | |

### 9. Rich Text (`gpu/rich_text_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> Animated text floats over noise and bloom for a compact typography patch.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 10. Star Spin (`gpu/star_spin_demo.json`)

**Type:** A/V | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> A spinning star reacts to audio and leaves glowing feedback trails.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| Visual | | |
| A/V sync | | |
| **Result** | | |

### 11. Texture Analysis (`gpu/texture_analysis_demo.json`)

**Type:** A/V | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> Brightness and edge density from a visual texture drive pitch and filter cutoff.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| Visual | | |
| A/V sync | | |
| **Result** | | |

## filters/ (9 graphs)

### 1. CRT Monitor Effect (`filters/crt_effect_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** MovieFileAudio, MovieFileIn | **Packages:** —

> A video source is bent into a glowing CRT monitor look with curvature, scanlines, vignette, and color fringing.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 2. Displacement Map (`filters/displace_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> A clean shape is warped by animated noise so the displacement effect is easy to see.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 3. Dithering (`filters/dither_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> A smooth gradient is reduced into animated dithered color bands.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 4. Edge Detection (`filters/edge_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** MovieFileAudio, MovieFileIn | **Packages:** —

> Video edges are highlighted with a slowly changing strength control.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 5. Kaleidoscope Mirror (`filters/mirror_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** MovieFileAudio, MovieFileIn | **Packages:** —

> A video source becomes a rotating kaleidoscope with mirrored segments.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 6. Scanlines (`filters/scanlines_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** MovieFileAudio, MovieFileIn | **Packages:** —

> A video source gets a simple CRT-style scanline treatment after level adjustment.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 7. Audio Spirograph (`filters/spirograph_demo.json`)

**Type:** A/V | **Difficulty:** intermediate | **Env:** — | **Packages:** —

> A generated tone drives a morphing spirograph with feedback trails.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| Visual | | |
| A/V sync | | |
| **Result** | | |

### 8. Voronoi Cells (`filters/voronoi_cells_demo.json`)

**Type:** Visual | **Difficulty:** beginner | **Env:** — | **Packages:** —

> Colorful Voronoi cells drift and pulse as a compact generative visual patch.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

### 9. WGSL Filter Chain (`filters/wgsl_filters_demo.json`)

**Type:** Visual | **Difficulty:** intermediate | **Env:** MovieFileAudio, MovieFileIn | **Packages:** —

> A single video chain passes through several shader filters so their combined look is easy to explore.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

## media/movie_file/ (2 graphs)

### 1. Movie Playback: Audio Sync (`media/movie_file/mfi_av_sync_demo.json`)

**Type:** A/V | **Difficulty:** beginner | **Env:** MovieFileAudio, MovieFileIn | **Packages:** —

> A movie plays with its audio and video locked together, then sends the sound through reverb.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| Visual | | |
| A/V sync | | |
| **Result** | | |

### 2. Movie Playback: Video Only (`media/movie_file/mfi_video_only.json`)

**Type:** Visual | **Difficulty:** beginner | **Env:** MovieFileIn | **Packages:** —

> A minimal movie-input patch for loading a video without audio sync wiring.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Visual | | |
| **Result** | | |

## io/ (1 graphs)

### 1. OSC Loopback (`io/osc_av_loopback_demo.json`)

**Type:** A/V | **Difficulty:** intermediate | **Env:** OscIn, OscOut | **Packages:** —

> An LFO is sent out over OSC and received back into the same graph to drive sound and image.

| Check | Result | Notes |
|-------|--------|-------|
| First-load | | |
| Audio | | |
| Visual | | |
| A/V sync | | |
| **Result** | | |
