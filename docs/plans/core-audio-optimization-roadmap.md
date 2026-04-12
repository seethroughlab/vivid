# Core Audio Optimization Roadmap

## Summary

This roadmap applies the `WavetableLayer` performance lessons to Vivid's built-in
audio operators. The policy is measure first, optimize around backend-friendly
data layout, keep scalar behavior as the correctness reference, use Highway as
the portable SIMD baseline when a custom vector kernel is a good fit, and use
Accelerate on macOS only when a benchmark proves it wins.

No public operator, graph, runtime, or control-server API changes are planned for
this stream. Backend choices are internal implementation details and must not
expose Highway or Accelerate types through `src/operator_api`.

## Target Order

| Rank | Target | Why | First acceptance |
| --- | --- | --- | --- |
| 1 | `SpectralFreeze` | Hand-rolled FFT, scalar windowing, polar conversion, phase reconstruction, and overlap-add. | Release benchmark publishes a repeatable win against the current scalar implementation at 256 frames. |
| 2 | `GranularSynth` | Many similar grains with interpolation, windowing, gain, and accumulation. | Active-grain renderer is separated from scheduling and beats the current per-grain scalar loop. |
| 3 | `Vocoder` | Repeated filter-bank and envelope-follower work across bands. | Band state is laid out for batch work and benchmarked before any SIMD cutover. |
| 4 | Shared gain/mix/pan kernels | Common graph glue with contiguous multiply/add work. | `Gain`, `Mixer`, and `StereoPanWidth` route through a shared internal kernel seam with unchanged behavior. |
| 5 | `Reverb` | Some parallel comb work, but feedback paths reduce SIMD upside. | Profiled hot path justifies an architecture review before implementation. |
| 6 | `ParametricEQ`, compressor, limiter, and filter family | Recursive IIR and dynamics state make naive SIMD low leverage. | Only revisit after telemetry shows a real hotspot and a data-layout plan exists. |

## Backend Contract

- Scalar remains the reference implementation and fallback.
- Highway is the portable SIMD path for custom kernels where lane shape and memory
  access make it a good fit.
- Accelerate is macOS-only, optional, and benchmark-gated.
- Shared helpers live outside `src/operator_api`; operators may call them, but no
  public API may expose backend-specific types.
- Release builds are required for performance acceptance; Debug builds are only
  correctness smoke.

## Benchmark Fixtures

Each operator optimization must publish:

- build type and backend configuration
- buffer size, with 256 frames as the primary gate and 1024 frames as secondary tracking
- repeated-run mean and variance
- per-node `ema_block_us`, graph `audio_load`, and `xruns`
- baseline comparison against the pre-change operator behavior

Initial fixture backlog:

- `SpectralFreeze`: frozen and blended STFT cases across 256, 512, and 1024 FFT sizes
- `GranularSynth`: low, medium, and max active-grain density cases
- `Vocoder`: 8, 16, and 32 band cases with stable carrier/modulator input
- shared kernels: gain, four-input mix, and stereo pan/width chains

## Current First Pass

The first implementation pass establishes a small shared buffer-kernel seam and
routes the lowest-risk graph-glue operators through it. This is intentionally not
the final optimization story for `SpectralFreeze`, `GranularSynth`, or `Vocoder`;
it creates a reusable landing zone before those larger architecture changes.

Acceptance for the first pass:

- shared audio-kernel correctness tests pass
- `Gain`, `Mixer`, and `StereoPanWidth` behavior remains unchanged
- no backend headers or types enter `src/operator_api`
- the roadmap records the heavier operator order and benchmark requirements

## Shared Audio Kernel Benchmark Pass

The shared buffer-kernel seam now has scalar-reference correctness coverage and
a Release benchmark target. `Gain`, `Mixer`, and `StereoPanWidth` continue to use
the shared helper layer without public surface changes. The selected benchmark
backend is `accelerate`; `StereoPanWidth` still uses the shared scalar helper
inside that seam while avoiding audio-thread scratch allocation.

Release benchmark on the current machine:

| Frames | Kernel | Mean |
| ---: | --- | ---: |
| `256` | clear | `0.0097 ± 0.0018 us` |
| `256` | scale unity | `0.0094 ± 0.0009 us` |
| `256` | scale atten | `0.0093 ± 0.0006 us` |
| `256` | scale silence | `0.0094 ± 0.0007 us` |
| `256` | mix4 one input | `0.0204 ± 0.0026 us` |
| `256` | mix4 two inputs | `0.0406 ± 0.0076 us` |
| `256` | mix4 four inputs | `0.0675 ± 0.0123 us` |
| `256` | stereo identity | `0.0401 ± 0.0016 us` |
| `256` | stereo mono | `0.0423 ± 0.0044 us` |
| `256` | stereo wide | `0.0439 ± 0.0037 us` |
| `256` | stereo hard left | `0.0469 ± 0.0127 us` |
| `256` | stereo hard right | `0.0414 ± 0.0048 us` |
| `1024` | clear | `0.0310 ± 0.0025 us` |
| `1024` | scale unity | `0.0360 ± 0.0037 us` |
| `1024` | scale atten | `0.0405 ± 0.0094 us` |
| `1024` | scale silence | `0.0340 ± 0.0031 us` |
| `1024` | mix4 one input | `0.0802 ± 0.0066 us` |
| `1024` | mix4 two inputs | `0.1325 ± 0.0221 us` |
| `1024` | mix4 four inputs | `0.2359 ± 0.0210 us` |
| `1024` | stereo identity | `0.1695 ± 0.0110 us` |
| `1024` | stereo mono | `0.1670 ± 0.0108 us` |
| `1024` | stereo wide | `0.1681 ± 0.0135 us` |
| `1024` | stereo hard left | `0.1672 ± 0.0118 us` |
| `1024` | stereo hard right | `0.1639 ± 0.0106 us` |

Benchmark command:

```bash
./build/bench_audio_kernels
```

Acceptance status:

- shared kernels compare against test-local scalar references
- coverage includes clear, scale, null-input mix, four-input mix, stereo identity, mono-width collapse, wide-side behavior, and pan extremes
- `test_spatial_ops` continues to validate `StereoPanWidth` operator behavior
- no backend-specific types enter `src/operator_api`

## SpectralFreeze Accelerate Pass

The first serious target is `SpectralFreeze`. It now has an internal shared DSP
engine with scalar and macOS Accelerate backends. The public operator surface is
unchanged, and scalar remains the reference/fallback path.

Release benchmark on the current machine:

| FFT size | Scalar mean | Accelerate mean | Speedup |
| --- | ---: | ---: | ---: |
| 256 | `25.239 ± 0.635 us` | `9.761 ± 0.184 us` | `2.586x` |
| 512 | `26.154 ± 1.107 us` | `8.395 ± 0.224 us` | `3.116x` |
| 1024 | `30.928 ± 3.315 us` | `7.655 ± 0.064 us` | `4.041x` |

Benchmark command:

```bash
./build/bench_spectral_freeze
```

Acceptance status:

- scalar vs Accelerate equivalence test passes within tolerance
- operator smoke covers all FFT sizes and phase modes
- Accelerate is preferred only when compiled on macOS with `VIVID_ENABLE_ACCELERATE=ON`
- non-Apple and scalar-only builds continue to use the scalar backend

## GranularSynth Scalar-Fast Renderer Pass

The second target is `GranularSynth`. It now has an internal shared DSP engine
under `operators/shared/granular_dsp/` that separates capture/scheduling,
active-grain compaction, grain render/accumulation, dry/wet mix, and inspector
snapshot data. The public operator surface and inspector behavior are unchanged.

This pass intentionally keeps scalar as the renderer backend while measuring the
new factoring point before adding a wider SIMD or Accelerate grain kernel. The
main hot-loop cleanup is precomputed window lookup tables plus compact active
grain iteration instead of scanning all 32 grain slots in the innermost loop.

Release benchmark on the current machine:

| Case | Density | Grain size | Backend | Mean |
| --- | ---: | ---: | --- | ---: |
| low | `2.0` | `60.0 ms` | scalar | `2.140 ± 0.157 us` |
| medium | `20.0` | `80.0 ms` | scalar | `2.748 ± 0.319 us` |
| high | `60.0` | `120.0 ms` | scalar | `5.106 ± 0.196 us` |

Benchmark command:

```bash
./build/bench_granular_synth
```

Acceptance status:

- shared DSP test covers low, medium, and high density cases
- window coverage includes Hann, Hamming, Blackman, and Triangle
- pitch and position modulation cases produce finite, non-silent output
- operator smoke loads `granular_synth.dylib` and verifies finite, non-silent output
- next pass can decide whether a dedicated SIMD/Accelerate grain kernel is worth the added complexity

## Vocoder Scalar Refactor Pass

The third target is `Vocoder`. It now has an internal shared DSP engine under
`operators/shared/vocoder_dsp/` with structure-of-arrays band state and cached
band coefficients. The public operator surface and mono vocoder behavior are
unchanged.

This pass intentionally keeps scalar as the renderer backend. The immediate
optimization is avoiding repeated per-block logarithmic band spacing, `pow`,
`sin`, Q, and normalization setup when sample rate and band count are stable.
SIMD/Accelerate remains deferred until this recursive SVF filter-bank baseline
shows a clear backend opportunity.

Release benchmark on the current machine:

| Frames | Bands | Speed | Backend | Mean | Coeff rebuilds |
| ---: | ---: | ---: | --- | ---: | ---: |
| `256` | `8` | `80.0 ms` | scalar | `3.632 ± 0.368 us` | `1` |
| `256` | `16` | `50.0 ms` | scalar | `4.076 ± 0.313 us` | `1` |
| `256` | `32` | `30.0 ms` | scalar | `5.208 ± 0.205 us` | `1` |
| `1024` | `8` | `80.0 ms` | scalar | `13.594 ± 0.259 us` | `1` |
| `1024` | `16` | `50.0 ms` | scalar | `15.553 ± 0.144 us` | `1` |
| `1024` | `32` | `30.0 ms` | scalar | `20.484 ± 0.220 us` | `1` |

Benchmark command:

```bash
./build/bench_vocoder
```

Acceptance status:

- shared DSP test compares the factored engine against a test-local copy of the original algorithm
- coverage includes 8, 16, and 32 band cases, slow and fast envelope speeds, partial and full wet mix, dry passthrough, and nonzero speed CV
- operator smoke loads `vocoder.dylib` and verifies finite, non-silent output
- coefficient rebuild count stays at one across steady benchmark runs

## Reverb Scalar Refactor Pass

The fifth target is `Reverb`. It now has an internal shared DSP engine under
`operators/shared/reverb_dsp/` with the same Freeverb-style comb/allpass
algorithm as the original inline operator. The public operator surface, factory
presets, graph behavior, and mono output are unchanged.

This pass intentionally keeps scalar as the renderer backend. The immediate goal
is testability and a measured baseline for the recursive feedback topology, not
a SIMD/Accelerate cutover. The benchmark confirms the wet preset cases have
nearly fixed cost because they share the same 8-comb + 4-allpass structure.

Release benchmark on the current machine:

| Frames | Case | Backend | Mean | Init count |
| ---: | --- | --- | ---: | ---: |
| `256` | small room | scalar | `3.928 ± 0.355 us` | `1` |
| `256` | large hall | scalar | `3.781 ± 0.283 us` | `1` |
| `256` | plate | scalar | `3.643 ± 0.209 us` | `1` |
| `256` | cathedral | scalar | `3.657 ± 0.209 us` | `1` |
| `256` | tight slap | scalar | `3.580 ± 0.155 us` | `1` |
| `256` | dry passthrough | scalar | `3.625 ± 0.204 us` | `1` |
| `256` | impulse tail | scalar | `2.205 ± 0.098 us` | `1` |
| `1024` | small room | scalar | `14.369 ± 0.135 us` | `1` |
| `1024` | large hall | scalar | `14.401 ± 0.136 us` | `1` |
| `1024` | plate | scalar | `14.419 ± 0.142 us` | `1` |
| `1024` | cathedral | scalar | `14.426 ± 0.110 us` | `1` |
| `1024` | tight slap | scalar | `14.451 ± 0.197 us` | `1` |
| `1024` | dry passthrough | scalar | `14.481 ± 0.084 us` | `1` |
| `1024` | impulse tail | scalar | `8.971 ± 0.232 us` | `1` |

Benchmark command:

```bash
./build/bench_reverb
```

Acceptance status:

- shared DSP test compares the factored engine against a test-local copy of the original algorithm
- coverage includes small room, large hall, plate, high damping, low damping, dry passthrough, impulse tail, and sample-rate reinitialization
- operator smoke loads `reverb.dylib` and verifies finite, non-silent output
- SIMD/Accelerate remains deferred until a deeper architecture pass is justified
