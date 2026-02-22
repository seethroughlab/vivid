# Assertions Reference

Complete reference for `vivid-assertions.json`. Evaluated by `vivid check` — exit code 0 = all pass, 1 = failure.

## Syntax

Each assertion is a JSON object on its own line:

```json
{"name": "label", "path": "output.meanBrightness", "op": ">", "value": 0.2, "message": "Not too dark"}
```

### Required Fields
- **`path`**: Dot-path to the metric (see Assertable Paths below)
- **`op`**: Comparison operator

### Optional Fields
- **`value`**: Comparison value (not needed for `exists`/`not_exists`)
- **`name`**: Human-readable label shown in verbose output and JSON
- **`message`**: Description shown on failure
- **`after_frame`**: Skip assertion if current frame < value (warmup periods)
- **`when_path`** / **`when_check`** / **`when_value`**: Conditional guard — assertion only evaluated when guard condition is met. Skipped assertions show as `SKIP` and don't affect pass/fail.

### Operators
`>`, `>=`, `<`, `<=`, `==`, `!=`, `between` (range with `[low, high]` inclusive), `exists`, `not_exists`

## Assertable Paths

| Prefix | Domain | Example |
|--------|--------|---------|
| `output.*` | Visual output (FrameAnalysis) | `output.meanBrightness`, `output.contrast` |
| `audio.*` | Audio analysis | `audio.rmsLevel`, `audio.spectrum.bass` |
| `temporal.*` | Multi-frame (requires `--duration`) | `temporal.isFrozen`, `temporal.flickerScore` |
| `av.*` | Audio-visual reactivity (auto-enables `--duration 2`) | `av.correlation`, `av.onsetResponseRate` |
| `operators.<name>.metrics.<key>` | Per-operator metrics | `operators.kick.metrics.is_playing` |
| `operators.<name>.textureAnalysis.<field>` | Per-operator texture (auto-enables GPU readback) | `operators.bloom.textureAnalysis.meanBrightness` |

### Audio Paths
All AudioAnalysis fields are assertable (except `pitchNote` which is a string): `audio.rmsLevel`, `audio.peakLevel`, `audio.spectrum.bass`, `audio.isSilent`, `audio.crestFactor`, `audio.dcOffset`, `audio.clippedSamplePct`, `audio.zeroCrossingRate`, `audio.stereoCorrelation`, `audio.stereoWidth`, `audio.spectralCentroid`, `audio.spectralFlatness`, `audio.spectralFlux`, `audio.onsetDensity`, `audio.integratedLUFS`, `audio.truePeakDBTP`, `audio.pitchHz`, `audio.pitchConfidence`, `audio.harmonicToNoiseRatio`, `audio.dynamicRangeDB`.

### AV Paths
Auto-enables multi-sample mode (2s, 60 samples): `av.correlation`, `av.correlationBrightness`, `av.correlationMotion`, `av.bandCorrelation.{subBass,bass,lowMid,mid,highMid,high}`, `av.reactivityLatencyFrames`, `av.reactivityLatencyMs`, `av.reactivityPeakCorrelation`, `av.onsetResponseRate`, `av.onsetResponseCount`, `av.totalOnsetsEvaluated`, `av.responseMagnitude`, `av.responseMagnitudeRatio`, `av.avgPostOnsetDelta`, `av.avgBaselineDelta`, `av.mutualInformation`, `av.mutualInformationRaw`, `av.sampleCount`, `av.durationSeconds`, `av.valid`.

### Temporal Paths
Requires `--duration` for multi-sample capture. Bool fields (`isConverged`, `isFrozen`, `isLooping`) resolve to 0.0/1.0: `flickerScore`, `flickerFrequency`, `frameDelta`, `convergenceScore`, `isConverged`, `motionMagnitude`, `regionMotion.N` (0–8), `frameDiversity`, `isFrozen`, `isLooping`, `loopPeriodSeconds`, `loopPeriodFrames`, `loopConfidence`, `noveltyScore`, `noveltyTrend`, `keyframeCount`.

### Per-Operator Texture Fields
All FrameAnalysis fields: `meanBrightness`, `contrast`, `dominantHue`, `saturationAvg`, `dominantColor.N`, `regionBrightness.N`, `histogram.N`, `textureEntropy`, `edgeDensity`, `avgGradientMag`, `clipBlackPct`, `clipWhitePct`, `headroom`, `rangeSpan`, `sharpness`, `noiseLevel`, `visualCenterX`, `visualCenterY`, `colorTemperature`, `hueHistogram.N` (0–11), `uniqueHueCount`, `hueEntropy`, `alphaOpaquePct`, `alphaTransparentPct`, `alphaPartialPct`, `alphaMean`.

## Examples

### Visual Output

```json
{"name": "brightness-ok", "path": "output.meanBrightness", "op": "between", "value": [0.2, 0.8]}
{"path": "output.contrast", "op": ">", "value": 0.15, "after_frame": 30, "message": "Contrast stabilizes after warmup"}
{"path": "output.textureEntropy", "op": ">", "value": 0.3, "message": "Not a flat fill"}
{"path": "output.clipBlackPct", "op": "<", "value": 0.5, "message": "Not mostly black"}
{"path": "output.sharpness", "op": ">", "value": 0.01, "message": "Has visible edges"}
{"path": "output.colorTemperature", "op": "between", "value": [0.3, 0.7], "message": "Neutral tones"}
{"path": "output.hueHistogram.0", "op": ">", "value": 0.1, "message": "Has red hues"}
{"path": "output.alphaOpaquePct", "op": "==", "value": 1.0, "message": "Fully opaque output"}
```

### Per-Operator Texture

```json
{"path": "operators.bloom.textureAnalysis.meanBrightness", "op": ">", "value": 0.1}
{"path": "operators.bloom.textureAnalysis.edgeDensity", "op": ">", "value": 0.01, "message": "Bloom has detail"}
```

### Audio

```json
{"path": "audio.rmsLevel", "op": ">", "value": 0.01, "message": "Audio not silent"}
{"path": "audio.spectrum.bass", "op": ">", "value": 0.05, "message": "Should have bass"}
{"path": "audio.peakLevel", "op": "<", "value": 0.95, "message": "No clipping"}
{"path": "audio.dcOffset", "op": "between", "value": [-0.01, 0.01], "message": "No DC offset"}
{"path": "audio.clippedSamplePct", "op": "<", "value": 0.01, "message": "Less than 1% clipping"}
{"path": "audio.stereoCorrelation", "op": ">", "value": 0.5, "message": "Mono-compatible stereo"}
{"path": "audio.spectralCentroid", "op": ">", "value": 500, "message": "Not too dull"}
{"path": "audio.spectralFlatness", "op": "<", "value": 0.3, "message": "Tonal content present"}
{"path": "audio.integratedLUFS", "op": "between", "value": [-24, -14], "message": "Broadcast loudness range"}
{"path": "audio.truePeakDBTP", "op": "<", "value": -1.0, "message": "True peak headroom"}
{"path": "audio.pitchHz", "op": "between", "value": [430, 450], "message": "A4 tuning", "when_path": "audio.pitchConfidence", "when_check": ">", "when_value": 0.7}
{"path": "audio.harmonicToNoiseRatio", "op": ">", "value": 10, "message": "Clean tonal signal"}
{"path": "audio.onsetDensity", "op": ">", "value": 2, "message": "Has rhythmic content"}
{"path": "audio.spectrum.bass", "op": ">", "value": 0.4, "when_path": "operators.kick.metrics.is_playing", "when_check": "==", "when_value": 1.0}
```

### Temporal (require `--duration`)

```json
{"path": "temporal.isFrozen", "op": "==", "value": 0, "message": "Animation is running"}
{"path": "temporal.flickerScore", "op": "<", "value": 0.3, "message": "No unwanted flicker"}
{"path": "temporal.isConverged", "op": "==", "value": 1, "message": "Feedback loop stabilized"}
{"path": "temporal.motionMagnitude", "op": ">", "value": 0.01, "message": "Has visible motion"}
{"path": "temporal.isLooping", "op": "==", "value": 1, "message": "Animation loops correctly"}
{"path": "temporal.noveltyScore", "op": ">", "value": 0.02, "message": "Visuals are evolving"}
```

### Audio-Visual Reactivity (auto-enables `--duration 2`)

```json
{"path": "av.correlation", "op": ">", "value": 0.3, "message": "Visuals respond to audio"}
{"path": "av.onsetResponseRate", "op": ">", "value": 0.5, "message": "Most beats produce visual change"}
{"path": "av.reactivityLatencyMs", "op": "<", "value": 100, "message": "Responsive within 100ms"}
{"path": "av.bandCorrelation.bass", "op": ">", "value": 0.4, "message": "Bass drives visuals"}
{"path": "av.mutualInformation", "op": ">", "value": 0.2, "message": "Non-linear AV relationship exists"}
{"path": "av.responseMagnitudeRatio", "op": ">", "value": 1.5, "message": "Onset response exceeds baseline"}
```
