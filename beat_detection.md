# Real-Time Beat Detector - C++ Implementation Specification

## Overview

This is a real-time audio beat detector that identifies three drum types (kick, snare, hi-hat) from audio input using FFT frequency analysis. It uses configurable frequency bins and adaptive thresholds to trigger beat events.

## Core Algorithm

### 1. FFT Configuration

```cpp
const int FFT_SIZE = 2048;           // FFT window size
const int BUFFER_LENGTH = FFT_SIZE / 2;  // 1024 frequency bins
const float SMOOTHING = 0.8f;        // Temporal smoothing (0.0-1.0)
```

The FFT produces `BUFFER_LENGTH` (1024) frequency bins. Each bin represents a frequency range from 0 to the Nyquist frequency (sample_rate / 2).

### 2. Frequency-to-Bin Conversion

```cpp
int freqToIndex(float freq, float sampleRate) {
    float nyquist = sampleRate / 2.0f;
    return (int)round(freq / nyquist * BUFFER_LENGTH);
}
```

Example at 44100 Hz sample rate:
- 100 Hz → bin ~4
- 5000 Hz → bin ~232
- 12000 Hz → bin ~557

### 3. Configurable Frequency Ranges (Bins)

Each drum type has a configurable frequency range in Hz:

| Type   | Default Low (Hz) | Default High (Hz) | Typical Use |
|--------|------------------|-------------------|-------------|
| Kick   | 20               | 150               | Bass drum   |
| Snare  | 150              | 500               | Mid-range   |
| Hi-Hat | 5000             | 12000             | Cymbals     |

**Data Structure:**
```cpp
struct FrequencyRange {
    float lowFreq;      // Low frequency bound in Hz
    float highFreq;     // High frequency bound in Hz
    float threshold;    // Detection threshold multiplier (1.0 - 2.0)
};

// Example initialization
FrequencyRange kick   = { 20.0f,   150.0f,  1.3f  };
FrequencyRange snare  = { 150.0f,  500.0f,  1.2f  };
FrequencyRange hihat  = { 5000.0f, 12000.0f, 1.15f };
```

### 4. Energy Calculation

Sum the magnitudes of all FFT bins within the frequency range, then average:

```cpp
float getEnergyInRange(float* fftData, float lowFreq, float highFreq, float sampleRate) {
    int lowIndex = freqToIndex(lowFreq, sampleRate);
    int highIndex = freqToIndex(highFreq, sampleRate);

    // Clamp indices to valid range
    lowIndex = std::max(0, std::min(lowIndex, BUFFER_LENGTH - 1));
    highIndex = std::max(0, std::min(highIndex, BUFFER_LENGTH - 1));

    float sum = 0.0f;
    for (int i = lowIndex; i <= highIndex; i++) {
        sum += fftData[i];  // FFT magnitude values (0-255 in original, or 0.0-1.0 normalized)
    }

    int binCount = highIndex - lowIndex + 1;
    return (binCount > 0) ? sum / binCount : 0.0f;
}
```

### 5. Beat Detection Algorithm (Key Logic)

This is the core triggering mechanism using **adaptive thresholding**:

```cpp
const int HISTORY_SIZE = 40;   // Rolling history window (~400-500ms at 60fps)
const int COOLDOWN_MS = 100;   // Minimum time between beats

struct BeatState {
    std::deque<float> history;  // Rolling energy history
    int64_t lastBeatTime;       // Timestamp of last detected beat (ms)
};

bool detectBeat(BeatState& state, float currentEnergy, float threshold, int64_t currentTimeMs) {
    // 1. Update history
    state.history.push_back(currentEnergy);
    if (state.history.size() > HISTORY_SIZE) {
        state.history.pop_front();
    }

    // 2. Need full history before detecting
    if (state.history.size() < HISTORY_SIZE) {
        return false;
    }

    // 3. Enforce cooldown period
    if (currentTimeMs - state.lastBeatTime < COOLDOWN_MS) {
        return false;
    }

    // 4. Calculate average energy from history
    float avgEnergy = 0.0f;
    for (float e : state.history) {
        avgEnergy += e;
    }
    avgEnergy /= state.history.size();

    // 5. TRIGGER CONDITION: current energy exceeds average * threshold
    if (currentEnergy > avgEnergy * threshold) {
        state.lastBeatTime = currentTimeMs;
        return true;  // Beat detected!
    }

    return false;
}
```

### 6. Threshold Meaning

The threshold is a **multiplier** applied to the rolling average energy:

| Threshold | Meaning | Sensitivity |
|-----------|---------|-------------|
| 1.0       | Trigger at average level | Very sensitive (many false positives) |
| 1.15      | 15% above average | High sensitivity (good for hi-hats) |
| 1.2       | 20% above average | Medium sensitivity (good for snares) |
| 1.3       | 30% above average | Lower sensitivity (good for kicks) |
| 2.0       | 100% above average | Very low sensitivity |

**Why different defaults?**
- Kick drums produce strong, distinct transients → higher threshold (1.3)
- Snares are mid-range but still distinct → medium threshold (1.2)
- Hi-hats are rapid and have less dynamic range → lower threshold (1.15)

### 7. Complete Processing Loop

```cpp
void processFrame(float* fftData, float sampleRate, int64_t currentTimeMs) {
    // For each drum type
    for (auto& [type, range, state] : drumTypes) {
        // Calculate energy in this frequency band
        float energy = getEnergyInRange(fftData, range.lowFreq, range.highFreq, sampleRate);

        // Check for beat
        bool beatDetected = detectBeat(state, energy, range.threshold, currentTimeMs);

        if (beatDetected) {
            // Trigger callback/event for this drum type
            onBeatDetected(type, energy);
        }
    }
}
```

### 8. FFT Smoothing (Optional but Recommended)

The original uses temporal smoothing on FFT data to reduce noise:

```cpp
// Applied before beat detection
smoothedValue = SMOOTHING * previousValue + (1.0 - SMOOTHING) * currentValue;
```

With `SMOOTHING = 0.8`:
- 80% of the previous frame's value
- 20% of the current frame's value
- Reduces jitter but adds slight latency

## Key Constants Summary

```cpp
// FFT
const int FFT_SIZE = 2048;
const int BUFFER_LENGTH = 1024;  // FFT_SIZE / 2
const float SMOOTHING = 0.8f;

// Beat Detection
const int HISTORY_SIZE = 40;
const int COOLDOWN_MS = 100;

// Default Ranges
const float KICK_LOW = 20.0f, KICK_HIGH = 150.0f, KICK_THRESHOLD = 1.3f;
const float SNARE_LOW = 150.0f, SNARE_HIGH = 500.0f, SNARE_THRESHOLD = 1.2f;
const float HIHAT_LOW = 5000.0f, HIHAT_HIGH = 12000.0f, HIHAT_THRESHOLD = 1.15f;

// Threshold bounds (user-adjustable)
const float MIN_THRESHOLD = 1.0f;
const float MAX_THRESHOLD = 2.0f;
```

## C++ Libraries to Consider

For FFT in C++:
- **FFTW** - Fast, industry standard
- **KissFFT** - Simple, public domain
- **pffft** - SIMD optimized

For audio input:
- **PortAudio** - Cross-platform
- **RtAudio** - C++ focused
- **miniaudio** - Single header, easy to integrate
