# Lesson 05: Audio-Reactive

This lesson introduces audio analysis for driving visual parameters.

## Lesson Objectives

1. Capture audio with AudioIn
2. Analyze amplitude with Levels
3. Separate frequencies with BandSplit
4. Use analysis values to drive visual parameters

## Key Concepts

- **AudioIn**: Captures audio from microphone/line input
- **Levels**: RMS (average) and peak amplitude analysis
- **BandSplit**: Separates audio into frequency bands (bass, mid, high)
- **Smoothing**: Reduces jitter for smoother visual response

## What the Code Demonstrates

- Basic audio capture and analysis
- Mapping audio values to visual parameters
- Creating responsive, dynamic visuals

## Suggested Modifications

1. **Add beat detection**:
   ```cpp
   auto& beat = chain.add<BeatDetect>("beat");
   beat.input("audio");
   beat.sensitivity = 1.5f;

   // In update():
   if (beat.beat()) {
       // Trigger something on the beat!
   }
   ```

2. **Use different frequency bands**:
   ```cpp
   float subBass = bands.subBass();  // 20-60 Hz (sub bass)
   float lowMid = bands.lowMid();    // 250-500 Hz
   float highMid = bands.highMid();  // 2000-4000 Hz
   ```

3. **Add FFT spectrum visualization**:
   ```cpp
   auto& fft = chain.add<FFT>("fft");
   fft.input("audio");
   fft.setSize(512);

   // In update():
   for (int i = 0; i < fft.binCount(); i++) {
       float value = fft.bin(i);
       // Use for spectrum visualizer
   }
   ```

4. **Adjust smoothing for different feels**:
   - `0.5f` = Quick, punchy response
   - `0.9f` = Smooth, flowing motion
   - `0.95f` = Very smooth, ambient feel

## Audio Analysis Quick Reference

| Operator | Purpose | Key Methods |
|----------|---------|-------------|
| `AudioIn` | Microphone capture | `.volume`, `.setMute()` |
| `Levels` | Amplitude | `.rms()`, `.peak()` |
| `BandSplit` | Frequency bands | `.bass()`, `.mid()`, `.high()` |
| `BeatDetect` | Beat/onset detection | `.beat()`, `.intensity()` |
| `FFT` | Full spectrum | `.bin(i)`, `.binCount()` |

## Common Issues

- **No audio response**: Check your microphone is connected and not muted
- **Too sensitive**: Reduce the multipliers in update()
- **Too jittery**: Increase smoothing value
- **Delayed response**: Decrease smoothing value

## Next Lesson

06-video-input: Using video and webcam as texture sources
