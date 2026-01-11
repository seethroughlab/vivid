# Audio-Reactive

Demonstrates audio analysis operators driving visual effects.

## Operators Used

- **AudioFile** - Load and play audio files
- **AudioIn** - Microphone/line input
- **Levels** - RMS and peak amplitude analysis
- **BandSplit** - Frequency band separation (bass/mid/high)
- **BeatDetect** - Beat/onset detection
- **FFT** - Full spectrum analysis
- **AudioOutput** - Play audio to speakers

## Key Concepts

### Audio Sources
```cpp
// Audio file playback
auto& audioFile = chain.add<AudioFile>("audioFile");
audioFile.file("path/to/audio.wav")
         .loop(true)
         .volume(0.8f);
audioFile.play();
audioFile.pause();
audioFile.seek(10.0);  // Seconds

// Microphone input
auto& mic = chain.add<AudioIn>("mic");
mic.volume(1.0f);
mic.mute(false);
```

### Amplitude Analysis
```cpp
auto& levels = chain.add<Levels>("levels");
levels.input("audioFile");
levels.smoothing(0.85f);  // 0 = instant, 1 = very smooth

float rms = levels.rms();    // Average amplitude (0-1)
float peak = levels.peak();  // Peak amplitude (0-1)
```

### Frequency Bands
```cpp
auto& bands = chain.add<BandSplit>("bands");
bands.input("audioFile");
bands.smoothing(0.9f);

float subBass = bands.subBass();  // 20-60 Hz
float bass = bands.bass();         // 60-250 Hz
float lowMid = bands.lowMid();     // 250-500 Hz
float mid = bands.mid();           // 500-2000 Hz
float highMid = bands.highMid();   // 2000-4000 Hz
float high = bands.high();         // 4000-20000 Hz
```

### Beat Detection
```cpp
auto& beat = chain.add<BeatDetect>("beat");
beat.input("audioFile");
beat.sensitivity(1.5f);  // Higher = more sensitive
beat.decay(0.92f);       // How fast intensity decays

bool isBeat = beat.beat();         // True on beat frame
float intensity = beat.intensity(); // Current beat intensity (0-1)
float energy = beat.energy();       // Overall energy level
```

### FFT Spectrum
```cpp
auto& fft = chain.add<FFT>("fft");
fft.input("audioFile");
fft.setSize(512);       // FFT size (power of 2)
fft.smoothing = 0.7f;

// Get spectrum data
const float* bins = fft.bins();    // Raw FFT bins
int binCount = fft.binCount();     // Number of bins
float bin = fft.bin(10);           // Single bin value

// Frequency to bin conversion
float freq = 440.0f;
int bin = fft.frequencyToBin(freq);
```

### Audio Output
```cpp
auto& out = chain.add<AudioOutput>("out");
out.input("audioFile");
out.volume(0.8f);
chain.audioOutput("out");  // Register as chain's audio output
```

## Common Patterns

### Bass-Reactive Effect
```cpp
// Background color shifts with bass
float bassLevel = bands.bass();
gradient.colorA(0.05f + bassLevel * 0.2f, 0.02f, 0.1f);
```

### Beat-Triggered Flash
```cpp
if (beat.beat()) {
    flash.trigger(beat.intensity());
}
```

### Spectrum Visualization
```cpp
for (int i = 0; i < fft.binCount(); i++) {
    float value = fft.bin(i);
    // Draw bar at position i with height value
}
```

### Amplitude-Controlled Size
```cpp
float rms = levels.rms();
shape.size(0.2f + rms * 0.3f);
```

## Controls

- **M**: Toggle Microphone/File input
- **1-3**: Switch audio files
- **SPACE**: Pause/Play (file mode only)
- **TAB**: Open parameter controls
