# Introspection Reference

Complete field reference for Vivid's inspection data. Used by `vivid inspect` (CLI) and `inspect_chain` (MCP).

> **New to analysis tools?** See [ANALYSIS-TOOLS.md](ANALYSIS-TOOLS.md) for a guide on how AI assistants use these metrics and how to interpret the results.

## FrameAnalysis (Output Texture Analysis)

| Field | Type | Description | Healthy Range |
|-------|------|-------------|---------------|
| `meanBrightness` | float | Average luminance (0–1) | 0.2–0.8 |
| `contrast` | float | Std dev of luminance | 0.15–0.35 |
| `dominantColor` | [r,g,b] | Most prominent color | — |
| `dominantHue` | float | Hue (0–1) | — |
| `saturationAvg` | float | Average saturation | — |
| `histogram` | int[8] | 8-bucket luminance histogram | Even spread = good dynamic range |
| `regionBrightness` | float[9] | 3×3 spatial brightness grid (top-left → bottom-right) | Varies by composition |
| `textureEntropy` | float | Normalized Shannon entropy of 64-bin histogram (0=uniform, 1=max variety) | Noise/detail: >0.5. Flat fills: <0.2 |
| `edgeDensity` | float | Fraction of edge pixels (0–1) | High-detail: 0.1–0.3. Soft/blurred: <0.05 |
| `avgGradientMag` | float | Mean gradient magnitude | — |
| `clipBlackPct` | float | Fraction of pixels near pure black (lum < 0.005) | <0.05 unless intentional |
| `clipWhitePct` | float | Fraction of pixels near pure white (lum > 0.995) | <0.05 unless intentional |
| `headroom` | float | 1.0 − maxLuminance (room before white clip) | — |
| `rangeSpan` | float | maxLuminance − minLuminance (dynamic range) | Low = flat image |
| `sharpness` | float | Laplacian variance (higher = sharper) | Blurred: <0.01. Crisp: >0.05 |
| `noiseLevel` | float | Mean absolute Laplacian (high-frequency content) | Clean: <0.02. Noisy: >0.1 |
| `visualCenterX` | float | Brightness-weighted centroid X (0–1, 0.5=centered) | ~0.5 for centered |
| `visualCenterY` | float | Brightness-weighted centroid Y (0–1, 0.5=centered) | ~0.5 for centered |
| `colorTemperature` | float | 0=cool/blue, 0.5=neutral, 1=warm/red | 0.3–0.7 for neutral |
| `hueHistogram` | float[12] | 12-bin hue distribution (30° bins, indexed 0–11) | — |
| `uniqueHueCount` | int | Hue bins above 5% threshold (0–12) | — |
| `hueEntropy` | float | Normalized hue entropy (0=monochrome, 1=all hues equal) | — |
| `alphaOpaquePct` | float | Fraction fully opaque pixels | 1.0 for normal content |
| `alphaTransparentPct` | float | Fraction fully transparent pixels | — |
| `alphaPartialPct` | float | Fraction partially transparent pixels | — |
| `alphaMean` | float | Mean alpha value (0–1) | — |

## TemporalAnalysis (Multi-Frame Metrics)

Requires `--duration` for multi-sample inspect.

| Field | Type | Description | Interpretation |
|-------|------|-------------|----------------|
| `flickerScore` | float | High-frequency brightness oscillation (0=stable, 1=rapid flicker) | Smooth animation: <0.2. Alternating: >0.8 |
| `flickerFrequency` | float | Dominant flicker frequency in Hz | — |
| `frameDelta` | float | Most recent per-pixel change between frames | — |
| `convergenceScore` | float | 0=diverging, 0.5=stable, 1=fully converged | — |
| `isConverged` | bool | True when output has stabilized (delta < threshold for 8+ frames) | True for feedback loops |
| `motionMagnitude` | float | Average pixel displacement (0=still) | Animated: 0.01–0.2. Static: 0 |
| `regionMotion` | float[9] | 3×3 regional motion grid (indexed 0–8) | — |
| `frameDiversity` | float | Variance of frame deltas (0=frozen or steady) | — |
| `isFrozen` | bool | True when all recent frames are identical | False for animated content |
| `isLooping` | bool | True when repeating brightness pattern detected | — |
| `loopPeriodFrames` | int | Detected loop period in frames | — |
| `loopPeriodSeconds` | float | Detected loop period in seconds | — |
| `loopConfidence` | float | Autocorrelation confidence (>0.7 = reliable) | — |
| `noveltyScore` | float | Distance from current frame to keyframes (0=repetitive) | — |
| `noveltyTrend` | float | 0–1; >0.5=increasingly novel, <0.5=collapsing | — |
| `keyframeCount` | int | Number of stored keyframes | — |

## AudioAnalysis (Per Audio Operator)

| Field | Type | Description | Interpretation |
|-------|------|-------------|----------------|
| `rmsLevel` | float | Overall RMS level (0–1) | Music: 0.1–0.5. Silence: 0. Clipping: >0.9 |
| `peakLevel` | float | Overall peak amplitude (0–1) | — |
| `rmsLeft` / `rmsRight` | float | Per-channel RMS | — |
| `isSilent` | bool | True if RMS < 0.001 | — |
| `crestFactor` | float | Peak/RMS ratio (dynamics indicator) | High = percussive. Low = compressed |
| `spectrum` | float[6] | 6-band energy: subBass (<60Hz), bass (60–250), lowMid (250–500), mid (500–2k), highMid (2k–4k), high (4k+) | — |
| `duration` | float | Buffer duration in seconds | — |
| `dcOffset` | float | Mean sample value. 0=ideal | >0.01 problematic. >0.1 severe |
| `clippedSampleCount` | int | Samples at \|s\| >= 0.99 | — |
| `clippedSamplePct` | float | Fraction of clipped samples | 0 = clean. >0.01 = audible distortion |
| `zeroCrossingRate` | float | Zero crossings per second | Low freq: ~200. Noise: >10000 |
| `stereoCorrelation` | float | L/R Pearson correlation (-1 to +1) | 1 = identical L/R (mono) |
| `stereoWidth` | float | Side/mid RMS ratio (0=mono, higher=wider) | — |
| `spectralCentroid` | float | Brightness indicator in Hz | Low = dark/warm, high = bright/harsh |
| `spectralSpread` | float | Spectral bandwidth in Hz | — |
| `spectralFlux` | float | Mean spectral change (normalized) | 0 = static, higher = changing timbre |
| `spectralFluxMax` | float | Max spectral flux (onset strength) | — |
| `spectralFlatness` | float | 0=tonal, 1=noise-like | <0.3 for tonal content |
| `spectralRolloff` | float | 85th percentile frequency in Hz | Low = bass-heavy. High = bright |
| `onsetDensity` | float | Onsets per second | — |
| `onsetCount` | int | Total detected onsets | — |
| `integratedLUFS` | float | EBU R128 integrated loudness (null if silent) | -23 = broadcast standard |
| `shortTermLUFS` | float | Short-term loudness (3s window) | — |
| `momentaryLUFS` | float | Momentary loudness (400ms) | — |
| `truePeak` | float | True peak amplitude (4x oversampled) | — |
| `truePeakDBTP` | float | True peak in dBTP | < -1 dBTP for headroom |
| `loudnessRange` | float | LRA: 10th–95th percentile of short-term | — |
| `pitchHz` | float | Fundamental frequency in Hz (YIN) | — |
| `pitchConfidence` | float | Pitch detection confidence (0–1) | >0.8 = reliable |
| `pitchNote` | string | Note name (e.g. "A4") — informational, not assertable | — |
| `pitchCents` | float | Cents deviation from nearest note | — |
| `harmonicToNoiseRatio` | float | HNR in dB | Sine > 15. Noise < 5 |
| `dynamicRangeDB` | float | Max-min block RMS in dB | — |
| `dynamicRangeCoeffVar` | float | Coefficient of variation of block RMS | — |

## AudioVisualAnalysis (Cross-Domain Reactivity)

Multi-sample with audio chain present. Available via `vivid inspect --duration` or `analyze_av_reactivity` MCP tool.

| Field | Type | Description |
|-------|------|-------------|
| `avCorrelation` | float | Peak AV correlation (max of brightness/motion vs audio RMS) |
| `avCorrelationBrightness` | float | Pearson correlation: audio RMS vs brightness |
| `avCorrelationMotion` | float | Pearson correlation: audio RMS vs motion magnitude |
| `bandCorrelations` | object[6] | Per-band: `{correlation, drivesMetric, rawCorrelation}` for subBass/bass/lowMid/mid/highMid/high |
| `reactivityLatencyFrames` | int | Optimal lag frames for peak cross-correlation |
| `reactivityLatencyMs` | float | Latency in milliseconds |
| `reactivityPeakCorrelation` | float | Cross-correlation at optimal lag |
| `onsetResponseRate` | float | Fraction of audio onsets producing visual change (0–1) |
| `onsetResponseCount` | int | Number of responsive onsets |
| `totalOnsetsEvaluated` | int | Total onsets detected |
| `responseMagnitude` | float | Mean frame delta in post-onset windows |
| `responseMagnitudeRatio` | float | Post-onset delta / baseline delta (>1 = reactive) |
| `avgPostOnsetDelta` | float | Average frame delta after onsets |
| `avgBaselineDelta` | float | Average frame delta during non-onset periods |
| `avMutualInformation` | float | Normalized mutual information (0–1, non-linear dependency) |
| `avMutualInformationRaw` | float | Raw mutual information in bits |
| `sampleCount` | int | Number of AV samples used |
| `durationSeconds` | float | Duration of analysis window |
| `valid` | bool | False if insufficient data, silent audio, or static visual |

## JSON Examples

### Single-Frame Inspect

```json
{
  "frame": 0, "time": 0.0,
  "operators": {
    "noise": { "scale": 4.0, "speed": 0.5 },
    "feedback": { "decay": 0.95, "energy": 0.72, "pixel_change_pct": 18.3 },
    "bloom": { "threshold": 0.6, "bright_pixel_pct": 12.1 }
  },
  "output": {
    "meanBrightness": 0.48, "contrast": 0.22,
    "textureEntropy": 0.61, "edgeDensity": 0.14, "sharpness": 0.032,
    "clipBlackPct": 0.02, "clipWhitePct": 0.0, "rangeSpan": 0.91,
    "visualCenterX": 0.52, "visualCenterY": 0.48,
    "colorTemperature": 0.55, "uniqueHueCount": 4, "hueEntropy": 0.42,
    "histogram": [12, 45, 89, 120, 95, 40, 8, 3],
    "regionBrightness": [0.3, 0.4, 0.3, 0.5, 0.7, 0.5, 0.3, 0.4, 0.3]
  }
}
```

### Multi-Sample Envelope (`--duration 2 --samples 5`)

```json
{
  "project": "my-project", "duration": 2, "sampleCount": 5,
  "samples": [ {"frame": 0, "time": 0.0, "output": {"...": "..."}} ],
  "temporal": {
    "flickerScore": 0.05, "isConverged": true, "motionMagnitude": 0.03,
    "isFrozen": false, "noveltyScore": 0.12, "isLooping": false
  },
  "audioVisual": {
    "avCorrelation": 0.65, "avCorrelationBrightness": 0.58,
    "onsetResponseRate": 0.75, "reactivityLatencyMs": 33.3,
    "mutualInformation": 0.38, "valid": true
  }
}
```

### Per-Operator Analysis (`per_operator_analysis: true`)

```json
{
  "operators": {
    "noise": {
      "metrics": {"scale": 4.0, "speed": 0.5},
      "metadata": {"type": "Noise", "output_kind": "Texture"},
      "textureAnalysis": {"meanBrightness": 0.51, "contrast": 0.29, "...": "..."}
    },
    "bloom": {
      "metrics": {"threshold": 0.6},
      "metadata": {"type": "Bloom", "output_kind": "Texture"},
      "textureAnalysis": {"meanBrightness": 0.05, "contrast": 0.03, "...": "..."}
    }
  }
}
```

Useful for diagnosing where brightness/contrast drops occur in the chain.

## Comparison Tools

### `compare_frames`
No running instance needed. Returns:
- RMSE, per-channel diff, changed pixel count
- `analysis_diff` with FrameAnalysis-level diffs: `brightness_diff`, `contrast_diff`, `entropy_diff`, `sharpness_diff`, `noise_diff`, `temperature_diff`, `edge_density_diff`, `clip_black_diff`, `clip_white_diff`, `hue_count_diff`, `center_displacement`

Use `analysis_diff` fields to evaluate semantic impact of changes without reviewing pixels.

### `compare_audio`
No running instance needed. Returns:
- `rmsDiff`, `peakDiff`, `spectrumDiff`, `correlation`, `centroidDiff`, `spreadDiff`, `fluxDiff`, `flatnessDiff`, `widthDiff`, `correlationDiff_stereo`, `onsetDensityDiff`, `lufsDiff`, `pitchDiff`

### `sweep_param`
Capture frames across a parameter range. Find optimal values or verify smooth transitions.

### `sweep_param_audio`
Capture audio across a parameter range. Evaluate how audio changes with parameters.

## Visual Analysis Tools

Work on any PNG — no running instance needed.

### `analyze_color_harmony`
Extract 5-color palette via k-means clustering, score harmony against models.
Returns: `palette` (hex array), `harmonyScore` (0–1), `harmonyType` (`"complementary"`/`"analogous"`/`"triadic"`/`"split-complementary"`/`"none"`), `paletteContrast` (0–1).

### `analyze_symmetry`
Measure bilateral and rotational symmetry.
Returns: `horizontalSymmetry` (0–1), `verticalSymmetry` (0–1), `radialSymmetry` (0–1).

### `analyze_spatial_balance`
Rule-of-thirds and compositional balance.
Returns: `thirdsScore` (0–1), `horizontalBias` (-1 to +1), `verticalBias` (-1 to +1), `balanceScore` (0–1).

## Audio-Visual Analysis

### `analyze_av_reactivity` (MCP)
Requires running instance with audio. Parameters: `duration` (default 3.0s), `fps` (default 30).
Returns all AudioVisualAnalysis fields (see table above). Use to verify audio-reactive projects produce correlated visual output.

### Multi-sample inspect with audio
`vivid inspect --duration 2 --samples 30` includes `"audioVisual"` in the envelope JSON when an audio chain is present.
