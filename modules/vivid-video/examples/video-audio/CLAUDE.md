# Video Audio

Extract and process audio from video files with real-time effects.

## Requirements

- Video file with audio track (H.264/HEVC/HAP + AAC/MP3)
- Place in `assets/videos/sample.mov`

## Operators Used

- **VideoPlayer** - Video playback
- **VideoAudio** - Extract audio from video
- **FFT** - Frequency analysis for visualization
- **Levels** - Audio metering
- **Delay** - Delay/echo effect
- **Reverb** - Room reverb effect
- **AudioGain** - Master volume control

## Key Concepts

### Video with Audio

```cpp
// Load video
auto& video = chain.add<VideoPlayer>("video");
video.setFile("assets/videos/movie.mov");
video.setLoop(true);
video.play();

// Extract audio
auto& videoAudio = chain.add<VideoAudio>("videoAudio");
videoAudio.setSource("video");  // Connect to VideoPlayer by name

// Route to output
auto& output = chain.add<AudioOutput>("out");
output.input("videoAudio");

// Set outputs
chain.output("video");        // Visual
chain.audioOutput("out");     // Audio
```

### Audio Processing Chain

```cpp
// Video audio can be processed like any audio source
chain.add<VideoAudio>("va").setSource("video");
chain.add<Delay>("delay").input("va");
chain.add<Reverb>("reverb").input("delay");
chain.add<AudioOutput>("out").input("reverb");
```

### Disabling Internal Audio

When VideoAudio is connected, it automatically disables the VideoPlayer's internal audio playback to prevent double playback:

```cpp
// Explicit control (usually automatic)
video.setInternalAudioEnabled(false);  // Disable built-in playback
// Now audio only plays through VideoAudio → AudioOutput path
```

### Audio Analysis

```cpp
// FFT for visualization
auto& fft = chain.add<FFT>("fft");
fft.input("videoAudio");

float bass = fft.band(0);
float mids = fft.band(5);
float highs = fft.band(10);

// Level metering
auto& levels = chain.add<Levels>("levels");
levels.input("videoAudio");

float left = levels.rmsLeft();
float right = levels.rmsRight();
```

## Playback Control

```cpp
// Basic controls
video.play();
video.pause();
video.restart();

// Seeking
video.seek(10.5f);  // Seek to 10.5 seconds

// State queries
bool playing = video.isPlaying();
float time = video.currentTime();
float duration = video.duration();
bool hasAudio = video.hasAudio();

// Speed control (audio muted when != 1.0)
video.setSpeed(0.5f);   // Half speed
video.setSpeed(2.0f);   // Double speed

// Volume
video.setVolume(0.8f);  // 80% volume
```

## Audio-Reactive Video Effects

```cpp
// Modulate video effects with audio
float level = levels.rmsLeft();
float bass = fft.band(20.0f, 250.0f);  // Bass frequency range in Hz

// Pulse vignette with audio
vignette.intensity = 0.2f + level * 0.5f;

// Bloom on bass
bloom.intensity = 0.5f + bass * 2.0f;

// Chromatic aberration on hits
if (bass > 0.8f) {
    chromatic.amount = 0.01f;
} else {
    chromatic.amount *= 0.95f;  // Decay
}
```

## Supported Formats

### Video Codecs
- **HAP** (recommended) - Direct GPU texture, lowest CPU
- **H.264** - Universal, good compression
- **HEVC/H.265** - Better compression, higher CPU
- **ProRes** (macOS) - High quality, larger files

### Audio Codecs
- **AAC** - Common, good quality
- **MP3** - Universal
- **PCM** - Uncompressed (in MOV/AVI)

### Container Formats
- **.mov** - QuickTime (recommended)
- **.mp4** - MPEG-4
- **.avi** - Windows AVI

## Controls

- **Space** - Play/Pause
- **R** - Restart from beginning
- **Left/Right** - Seek 5 seconds
- **TAB** - Toggle Audio Effects panel

### ImGui Audio Effects Panel

Press TAB to show/hide the effects panel with real-time controls:

**Master**
- Volume (0-2x)

**Delay**
- Time (0-1000ms)
- Feedback (0-95%)
- Mix (dry/wet)

**Reverb**
- Room Size (small to large)
- Damping (high frequency absorption)
- Width (stereo spread)
- Mix (dry/wet)

**Presets**
- Dry - No effects
- Small Room - Tight reverb
- Hall - Large space reverb
- Slapback - Quick echo
- Echo - Rhythmic delay
- Dub - Heavy delay + reverb

## Tips

1. **HAP videos** use less CPU but larger files
2. Audio sync is automatic with VideoAudio
3. Speed changes mute audio (no pitch correction)
4. Use FFT for real-time visualizations
5. VideoAudio auto-disables internal playback
6. Always set both `chain.output()` and `chain.audioOutput()`

## Common Issues

### No audio
- Check that video file has audio track: `ffprobe video.mov`
- Ensure AudioOutput is connected and set as `chain.audioOutput()`

### Audio/video out of sync
- Use VideoAudio instead of internal playback for processing
- Avoid long delay chains (adds latency)

### Audio glitches
- Reduce processing chain complexity
- Check CPU usage
- Try HAP codec for video (lower decode overhead)
