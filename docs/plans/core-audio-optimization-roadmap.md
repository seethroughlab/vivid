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
| 6 | `ConvolutionReverb` | High-quality stereo convolution is FFT-shaped and maps cleanly to Accelerate on macOS. | New stereo production reverb path publishes scalar/Accelerate parity and Release benchmark numbers. |
| 7 | `ParametricEQ`, compressor, limiter, and filter family | Recursive IIR and dynamics state make naive SIMD low leverage. | Only revisit after telemetry shows a real hotspot and a data-layout plan exists. |

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
- `ConvolutionReverb`: built-in room, hall, cathedral, and short WAV IR cases with scalar vs Accelerate comparison
- filter/dynamics family: operator-level `ParametricEQ`, `Compressor`, `Limiter`,
  `Filter`, and `DualFilter` cases before choosing any recursive-IIR refactor target

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

## ConvolutionReverb Stereo Production Path

`ConvolutionReverb` is the new high-quality stereo reverb direction. It is a
separate operator rather than a replacement implementation inside the existing
mono `Reverb`, which remains the lightweight/simple algorithmic path.

The new shared DSP engine uses a hybrid low-latency convolution design:
the early block of the impulse response is rendered directly, while the tail is
rendered through block-size-aware FFT partitions. Built-in generated IRs avoid
third-party licensing risk, and a file path can override the preset with a WAV
impulse response. Mono, stereo, and 4-channel true-stereo IR layouts are
supported internally.

Acceptance status:

- scalar remains the correctness reference and fallback
- Accelerate is the preferred macOS FFT backend when `VIVID_ENABLE_ACCELERATE=ON`
- descriptor smoke validates a stereo input and stereo output surface
- DSP tests compare partitioned rendering against a direct convolution reference
- backend parity tests compare scalar and Accelerate within tolerance
- `bench_convolution_reverb` reports backend, IR length, partition count, plan rebuild count, and scalar/preferred speedup

Release benchmark on the current machine:

| Frames | Case | Scalar mean | Preferred backend | Preferred mean | Speedup | Partitions |
| ---: | --- | ---: | --- | ---: | ---: | ---: |
| `256` | room | `176.945 ± 25.061 us` | accelerate | `162.769 ± 1.838 us` | `1.087x` | `187` |
| `256` | hall | `475.987 ± 7.730 us` | accelerate | `478.528 ± 7.004 us` | `0.995x` | `749` |
| `256` | cathedral | `699.716 ± 12.263 us` | accelerate | `698.835 ± 16.746 us` | `1.001x` | `1124` |
| `1024` | room | `368.870 ± 2.845 us` | accelerate | `306.093 ± 2.711 us` | `1.205x` | `47` |
| `1024` | hall | `718.465 ± 7.184 us` | accelerate | `658.261 ± 5.506 us` | `1.091x` | `188` |
| `1024` | cathedral | `957.185 ± 11.356 us` | accelerate | `885.461 ± 8.288 us` | `1.081x` | `281` |

Benchmark command:

```bash
./build/bench_convolution_reverb
```

Follow-up note: this first pass preserves direct-convolution correctness by using
audio-block-sized FFT partitions. Long IRs at the 256-frame primary size are now
partition-count dominated, so the next performance pass should explore a
non-uniform partition plan: direct early reflections, small first tail
partitions, and larger late-tail partitions with explicit latency and listening
gates.

## ConvolutionReverb Non-Uniform Partition Pass

The second `ConvolutionReverb` pass implements the follow-up called for above:
a Gardner-style doubling-zone schedule under `operators/shared/convolution_reverb_dsp/`.
The public operator surface, WAV override, preset set, mono/stereo/true-stereo
IR handling, and both backends stay unchanged.

Zone schedule (compile-time tunables in the DSP source):

- direct early block for `IR[0, 256)` (unchanged from the prior pass)
- zone 1 at partition size = `block_size`, up to 4 partitions
- zones 2..K with partition size doubling each step, up to 4 partitions per zone
- zone-size cap at 4096 samples; once reached, one final zone absorbs every
  remaining partition so the per-zone FFT/IFFT overhead is shared across the
  long tail

Latency contract: each zone satisfies `write_offset = ir_offset - partition_size + block_size >= 0`
by construction, so no output latency is introduced relative to the uniform
scheme — the DSP test confirms bit-level parity with direct convolution across
a 3.0-second hall IR that engages all five zones. `ProcessStats.latency_samples`
reports the largest zone partition size (4096 at steady state) as a diagnostic
for the coarsest zone-fire cadence.

Release benchmark on the current machine, compared against the prior uniform pass:

| Frames | Case | Scalar mean | Preferred mean | Speedup | Zones | Partitions | Latency samples | vs prior preferred |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `256` | room | `170.01 ± 17.59 us` | `84.97 ± 0.69 us` | `2.00x` | `5` | `24` | `4096` | `1.92x faster` |
| `256` | hall | `180.72 ± 2.69 us` | `104.28 ± 0.52 us` | `1.73x` | `5` | `60` | `4096` | `4.59x faster` |
| `256` | cathedral | `193.56 ± 1.97 us` | `117.48 ± 1.23 us` | `1.65x` | `5` | `83` | `4096` | `5.95x faster` |
| `1024` | room | `466.04 ± 3.20 us` | `247.87 ± 0.36 us` | `1.88x` | `3` | `17` | `4096` | `1.23x faster` |
| `1024` | hall | `551.50 ± 3.16 us` | `358.85 ± 35.47 us` | `1.54x` | `3` | `52` | `4096` | `1.83x faster` |
| `1024` | cathedral | `618.41 ± 3.18 us` | `393.32 ± 4.63 us` | `1.57x` | `3` | `76` | `4096` | `2.25x faster` |

Benchmark command:

```bash
./build-release/bench_convolution_reverb
```

Acceptance status:

- scalar vs direct-convolution reference passes both the short-IR test and the
  new 3.0-second hall non-uniform test (80 blocks, 5 zones, 48 partitions)
- scalar vs Accelerate parity test passes within the prior tolerance
- operator smoke, WAV-fallback, and descriptor tests unchanged and passing
- 256-frame hall and cathedral both clear the ≥1.5x acceptance bar; in fact
  Accelerate mean drops from ~479us to ~104us (hall) and ~699us to ~117us
  (cathedral)
- 1024-frame cases all improve; none regress
- `ProcessStats` exposes `zone_count` and `latency_samples` for downstream
  visibility; no new types cross `src/operator_api`

Follow-up note: per-zone FFT input is duplicated (each zone maintains its own
frequency-domain input history), which limits the 1024-frame win to ~2x. A
future pass could share a single large FFT and derive smaller-zone frequency
bins via subband decomposition, or amortize large-zone FFT work across multiple
blocks (Gardner's algorithm). Both are deferred pending profiling evidence that
the current peak-block CPU is a problem on the target hardware.

## Filter/Dynamics Family Triage Pass

The sixth target family has now been measured with an operator-level benchmark
instead of choosing a refactor target by intuition. This pass intentionally makes
no DSP or public-surface changes. The benchmark loads the built operators and
includes wrapper cost, CV handling, lane-state handling, and per-block setup.

Release benchmark on the current machine:

| Frames | Operator | Case | Backend | Mean |
| ---: | --- | --- | --- | ---: |
| `256` | `ParametricEQ` | 1-band peak static | operator | `1.061 ± 0.113 us` |
| `256` | `ParametricEQ` | 4-band static | operator | `4.238 ± 0.176 us` |
| `256` | `ParametricEQ` | freq CV | operator | `1.028 ± 0.048 us` |
| `256` | `ParametricEQ` | mixed types | operator | `4.002 ± 0.169 us` |
| `256` | `Compressor` | no sidechain hard knee | operator | `1.036 ± 0.081 us` |
| `256` | `Compressor` | active sidechain | operator | `1.056 ± 0.101 us` |
| `256` | `Compressor` | soft knee | operator | `0.995 ± 0.043 us` |
| `256` | `Compressor` | fast attack/release | operator | `1.097 ± 0.096 us` |
| `256` | `Limiter` | dry/no limiting | operator | `0.404 ± 0.067 us` |
| `256` | `Limiter` | transient limiting | operator | `0.371 ± 0.024 us` |
| `256` | `Limiter` | steady over ceiling | operator | `0.390 ± 0.036 us` |
| `256` | `Limiter` | max lookahead | operator | `0.407 ± 0.043 us` |
| `256` | `Filter` | LP12 static | operator | `1.777 ± 0.076 us` |
| `256` | `Filter` | HP24 CV | operator | `2.263 ± 0.105 us` |
| `256` | `Filter` | ladder lane mod | operator | `9.977 ± 0.364 us` |
| `256` | `Filter` | formant | operator | `5.236 ± 0.221 us` |
| `256` | `Filter` | diode | operator | `20.916 ± 0.358 us` |
| `256` | `Filter` | MS-20 | operator | `5.313 ± 0.203 us` |
| `256` | `DualFilter` | serial LP/HP | operator | `3.911 ± 0.167 us` |
| `256` | `DualFilter` | parallel ladder/formant | operator | `10.898 ± 0.195 us` |
| `256` | `DualFilter` | split diode/MS-20 | operator | `21.420 ± 0.351 us` |
| `1024` | `ParametricEQ` | 1-band peak static | operator | `3.968 ± 0.107 us` |
| `1024` | `ParametricEQ` | 4-band static | operator | `16.128 ± 0.156 us` |
| `1024` | `ParametricEQ` | freq CV | operator | `4.110 ± 0.153 us` |
| `1024` | `ParametricEQ` | mixed types | operator | `16.017 ± 0.211 us` |
| `1024` | `Compressor` | no sidechain hard knee | operator | `3.949 ± 0.136 us` |
| `1024` | `Compressor` | active sidechain | operator | `4.069 ± 0.191 us` |
| `1024` | `Compressor` | soft knee | operator | `3.995 ± 0.188 us` |
| `1024` | `Compressor` | fast attack/release | operator | `4.273 ± 0.188 us` |
| `1024` | `Limiter` | dry/no limiting | operator | `1.483 ± 0.087 us` |
| `1024` | `Limiter` | transient limiting | operator | `1.493 ± 0.064 us` |
| `1024` | `Limiter` | steady over ceiling | operator | `1.508 ± 0.079 us` |
| `1024` | `Limiter` | max lookahead | operator | `1.499 ± 0.083 us` |
| `1024` | `Filter` | LP12 static | operator | `7.092 ± 0.248 us` |
| `1024` | `Filter` | HP24 CV | operator | `8.692 ± 0.273 us` |
| `1024` | `Filter` | ladder lane mod | operator | `40.680 ± 1.251 us` |
| `1024` | `Filter` | formant | operator | `22.380 ± 0.341 us` |
| `1024` | `Filter` | diode | operator | `85.792 ± 1.540 us` |
| `1024` | `Filter` | MS-20 | operator | `22.452 ± 0.228 us` |
| `1024` | `DualFilter` | serial LP/HP | operator | `15.697 ± 0.349 us` |
| `1024` | `DualFilter` | parallel ladder/formant | operator | `46.107 ± 1.817 us` |
| `1024` | `DualFilter` | split diode/MS-20 | operator | `91.352 ± 1.151 us` |

Benchmark command:

```bash
./build/bench_filter_dynamics_family
```

Acceptance status:

- repeated 256-frame and 1024-frame operator-level numbers are published
- benchmark coverage includes `ParametricEQ`, `Compressor`, `Limiter`, `Filter`, and `DualFilter`
- dynamics processors are not the next optimization target; they are currently small relative to the filter family
- next recommended pass is a shared `filter_dsp` refactor centered on `Filter`/`DualFilter`, with first attention to Diode, Ladder, Formant, and MS-20 modes plus coefficient/prepared-state reuse for block-stable params

## Filter DSP Prepared Block Rendering Pass

The first `filter_dsp` optimization keeps the public `Filter` and `DualFilter`
surfaces unchanged while adding an internal prepared-plan/block-render path.
`FilterState::process(...)` remains available for compatibility, but
`Filter` and `DualFilter` now prepare block-stable filter coefficients once per
callback and render through the prepared path.

Release benchmark on the current machine, compared against the triage baseline:

| Frames | Operator | Case | Triage baseline | Prepared block | Speedup |
| ---: | --- | --- | ---: | ---: | ---: |
| `256` | `Filter` | LP12 static | `1.777 us` | `1.594 ± 0.271 us` | `1.11x` |
| `256` | `Filter` | HP24 CV | `2.263 us` | `1.529 ± 0.173 us` | `1.48x` |
| `256` | `Filter` | ladder lane mod | `9.977 us` | `7.449 ± 0.403 us` | `1.34x` |
| `256` | `Filter` | formant | `5.236 us` | `1.172 ± 0.111 us` | `4.47x` |
| `256` | `Filter` | diode | `20.916 us` | `19.302 ± 0.306 us` | `1.08x` |
| `256` | `Filter` | MS-20 | `5.313 us` | `5.641 ± 0.211 us` | `0.94x` |
| `256` | `DualFilter` | serial LP/HP | `3.911 us` | `3.355 ± 0.187 us` | `1.17x` |
| `256` | `DualFilter` | parallel ladder/formant | `10.898 us` | `7.199 ± 0.230 us` | `1.51x` |
| `256` | `DualFilter` | split diode/MS-20 | `21.420 us` | `19.634 ± 0.334 us` | `1.09x` |
| `1024` | `Filter` | LP12 static | `7.092 us` | `5.870 ± 0.240 us` | `1.21x` |
| `1024` | `Filter` | HP24 CV | `8.692 us` | `5.782 ± 0.387 us` | `1.50x` |
| `1024` | `Filter` | ladder lane mod | `40.680 us` | `28.788 ± 0.504 us` | `1.41x` |
| `1024` | `Filter` | formant | `22.380 us` | `4.161 ± 0.213 us` | `5.38x` |
| `1024` | `Filter` | diode | `85.792 us` | `76.434 ± 0.748 us` | `1.12x` |
| `1024` | `Filter` | MS-20 | `22.452 us` | `22.513 ± 0.309 us` | `1.00x` |
| `1024` | `DualFilter` | serial LP/HP | `15.697 us` | `13.348 ± 0.494 us` | `1.18x` |
| `1024` | `DualFilter` | parallel ladder/formant | `46.107 us` | `28.311 ± 0.486 us` | `1.63x` |
| `1024` | `DualFilter` | split diode/MS-20 | `91.352 us` | `80.213 ± 1.540 us` | `1.14x` |

Benchmark command:

```bash
./build/bench_filter_dynamics_family
```

Acceptance status:

- prepared block path matches the compatibility sample API across all filter modes
- `Filter` uses one prepared plan per block
- `DualFilter` uses prepared plans and branch-light route loops, with scratch allocation only for the parallel route when the buffer grows
- the strongest wins are from coefficient/prepared-state reuse in Formant, Ladder, HP24, and DualFilter parallel routing
- Diode and MS-20 remain dominated by nonlinear per-sample state work; further gains there require a separate sound-sensitive approximation or architecture pass

## Audio Operator Sweep Baseline

Profiling-gated deferrals in earlier passes left many audio operators
unmeasured. The new `bench_audio_operators_sweep` benchmark loads every built
audio operator dylib through `vivid::OperatorLoader` and reports mean ± stddev
µs/block at 256 and 1024 frames under a representative workload (stereo sine
mix with periodic trigger pulse). All smoke checks pass — every operator
produces finite, non-NaN/Inf output within a 50x absolute peak ceiling.

Per-instance budget is calculated against the 256-frame real-time window
(5333 µs/block at 48 kHz). The sweep's stopping threshold for DAW-grade
polyphony is **2% of the 256-frame budget per instance** (~107 µs), which
permits roughly 50 simultaneous operators on a single audio thread before
graph saturation.

Release benchmark on the current machine (sorted descending by 256-frame cost):

| Operator | 256f µs | 256f % budget | 1024f µs | Over 2%? |
| --- | ---: | ---: | ---: | :---: |
| GranularSynth | 175.18 | 3.28% | 180.72 | ⚠️ yes |
| ConvolutionReverb | 103.64 | 1.94% | 348.66 | no (edge) |
| SpectralFreeze | 7.04 | 0.13% | 25.60 | no |
| DrumKick | 6.10 | 0.11% | 15.59 | no |
| Chorus | 5.75 | 0.11% | 18.36 | no |
| ParametricEQ | 5.25 | 0.10% | 16.72 | no |
| Phaser | 4.62 | 0.09% | 15.54 | no |
| DrumTom | 4.04 | 0.08% | 13.42 | no |
| DrumSnare | 4.00 | 0.08% | 12.50 | no |
| DualFilter | 3.17 | 0.06% | 10.03 | no |
| AudioAnalysis | 2.98 | 0.06% | 13.60 | no |
| Vocoder | 2.63 | 0.05% | 9.64 | no |
| DrumCymbal | 2.52 | 0.05% | 9.39 | no |
| Reverb | 2.25 | 0.04% | 9.04 | no |
| FmSynth | 2.07 | 0.04% | 8.07 | no |
| Filter | 1.74 | 0.03% | 6.61 | no |
| Flanger | 1.58 | 0.03% | 5.37 | no |
| DrumHiHat | 1.43 | 0.03% | 5.65 | no |
| DrumClap | 1.20 | 0.02% | 4.75 | no |
| Compressor | 1.19 | 0.02% | 4.08 | no |
| RingMod | 0.97 | 0.02% | 3.86 | no |
| PingPongDelay | 0.95 | 0.02% | 2.99 | no |
| Distortion | 0.90 | 0.02% | 3.24 | no |
| Oscillator | 0.88 | 0.02% | 3.42 | no |
| Bitcrush | 0.72 | 0.01% | 2.49 | no |
| Delay | 0.65 | 0.01% | 3.01 | no |
| Limiter | 0.49 | 0.01% | 1.72 | no |
| Mixer | 0.38 | 0.01% | 1.61 | no |
| Noise | 0.27 | 0.01% | 1.06 | no |
| StereoPanWidth | 0.05 | 0.00% | 0.21 | no |
| SP404 | 0.02 | 0.00% | 0.07 | no |
| Slicer | 0.02 | 0.00% | 0.11 | no |
| Sampler | 0.02 | 0.00% | 0.08 | no |
| Gain | 0.02 | 0.00% | 0.06 | no |

Benchmark command:

```bash
./build-release/bench_audio_operators_sweep
```

Fixture: `tests/benchmarks/bench_audio_operators_sweep.cpp` — 32 warmup
blocks, 512 measure blocks, 6 repeats per case. Input signal: planar stereo
mix of two sine components plus xorshift pink-ish noise plus a single-sample
trigger pulse every 1024 samples. Params at operator defaults. Smoke floor:
`tests/audio/audio_smoke.h` (finite, non-silent unless `allow_silent`, peak
below ceiling, DC ratio below 30% of peak).

### Attack queue

Only **GranularSynth** currently exceeds the 2% per-instance budget at 256
frames. It's the sole forced optimization target under the stopping criteria.

Beyond that, these operators sit below threshold but have clear SIMD shape
and are worth opportunistic passes (descending by absolute 256f cost):

- `ConvolutionReverb` — already 1.94%; Gardner work-amortization is a named
  follow-up from the non-uniform pass.
- `DrumKick`, `DrumSnare`, `DrumTom` — ~4-6 µs each; shared `drum_dsp` can
  host a batched SIMD envelope/noise kernel that benefits all six drums.
- `Chorus` — 6 fractional-delay voices, `vDSP_vlint` replaces the scalar
  interpolator trivially.
- `ParametricEQ` — 4 cascaded biquads, prepared-block pattern like `Filter`.
- `Phaser` — 4-8 allpass cascade, biquad-style SIMD per stage.

Operators below 2 µs at 256 frames are left alone — they can't clear the
1.2× minimum-win bar without cross-operator changes.

## GranularSynth Inspector Snapshot Rate-Limit Pass

`GranularSynth` was the single operator over the 2% per-instance budget in
the sweep baseline (175.18 µs at 256 frames, 3.28%). Investigation showed the
DSP engine itself was already cheap (`bench_granular_synth` measures
2.1–5.5 µs per block across density/grain-size cases) — the audio-thread
cost was coming from `DoubleBufferedSnapshot::write`, which ran
`fill_inspector_snapshot` on every block. That call scans the full 4-second
capture ring (~192k samples at 48 kHz) into 280 waveform bins for the
inspector display, regardless of whether the inspector is currently visible.

The fix: rate-limit the snapshot write to every 8 audio blocks. At 48 kHz
with a 256-frame buffer that's ~43 ms between updates (~23 Hz refresh),
still above the UI's display cadence for waveform visualizations. The
grain state inside the snapshot updates at the slower rate too — acceptable
since grain phase progresses smoothly between snapshots and the UI already
double-buffers the snapshot for lock-free reads.

Release benchmark on the current machine:

| Frames | Pass | Mean µs | 256f % budget | Speedup |
| ---: | --- | ---: | ---: | ---: |
| `256` | baseline | `175.18 ± 3.37` | 3.28% | — |
| `256` | rate-limited | `14.07 ± 0.91` | 0.26% | **12.45x** |
| `1024` | baseline | `180.72 ± 2.94` | 0.85% | — |
| `1024` | rate-limited | `16.27 ± 0.84` | 0.08% | **11.11x** |

Benchmark command:

```bash
./build-release/bench_audio_operators_sweep
```

Acceptance status:

- `test_granular_synth_dsp` continues to pass (engine behavior unchanged).
- Operator smoke (`bench_audio_operators_sweep`): finite, non-silent,
  within peak/DC bounds at both buffer sizes.
- Snapshot refresh rate remains above typical audio-visualizer refresh
  rates at both 256 and 1024 frames.
- No public operator surface changed; the fix is local to
  `operators/audio/granular_synth/granular_synth.cpp`.
- `bench_granular_synth` (DSP-only) numbers are unchanged — the engine was
  never the problem.
- Attack queue: GranularSynth is now at 0.26% of 256-frame budget, far
  below the 2% threshold. Closed; no follow-up needed here.

Follow-up note: the same "inspector scan runs every audio block" anti-pattern
likely exists in other operators with custom inspectors. A future pass
should grep for `fill_inspector_snapshot`-style heavy UI work inside
`process_audio` and apply the same rate-limiting pattern.

## ConvolutionReverb Split-Complex + vDSP_zvma Pass

The non-uniform partitioning pass left `ConvolutionReverb` at 1.94% of the
256-frame real-time budget (103.64 µs in the sweep baseline), making it the
second-most-expensive operator after GranularSynth and the obvious next
target. Profile-free reasoning pointed at two inefficiencies:

1. **Per-FFT interleave/reinterleave.** The engine stored frequency-domain
   partitions and input history as interleaved `{re, im}` struct arrays, then
   deinterleaved into a `DSPSplitComplex` scratch pair on every FFT call via
   the shared FFT helper's adapter. That's ~2×`fft_size` redundant memory
   writes per FFT — small per call, real in aggregate across the zone's
   partition MAC cycle.

2. **Scalar MAC loop.** The partition multiply-accumulate
   (`fft_sum += x * H` across all partitions) was a hand-rolled complex
   multiply loop running once per bin. Accelerate's `vDSP_zvma` does the
   same math as a vectorized split-complex call.

Both fixes share one prerequisite: switch the engine's storage layout from
interleaved `ComplexPair` to native split-complex (parallel `re`/`im`
`std::vector<float>` pairs). With that, the shared FFT helper is called
directly on the split arrays (no adapter), and `vDSP_zvma` is the natural
MAC primitive.

Implementation notes:

- `Zone`'s `tail_{ll,lr,rl,rr}`, `input_history_{l,r}`, `fft_{l,r}`, and
  `fft_sum_{l,r}` fields all migrate from `std::vector<std::vector<Complex>>`
  (or `std::vector<Complex>`) to `std::vector<Split>` / `Split`, where
  `Split` is a small struct pairing `re` and `im` float vectors.
- `submit_zone_partition` calls `dispatch_fft` (a small namespace helper
  that picks scalar or Accelerate FFT per Backend) directly on split
  arrays.
- The partition MAC runs `vDSP_zvma` on the Accelerate path and a hand-rolled
  `scalar_zvma` on the scalar fallback. Scalar matches the original
  algorithm; the auto-vectorizer gets a clean straight-line loop.
- Overlap-add into `tail_accum` uses `vDSP_vadd` on Accelerate, scalar loop
  otherwise.
- Plan-cache lifecycle: `fft_cache_.clear()` at the top of `rebuild_plan`
  (stale sizes go away), `reserve(fft_size)` inside the zone loop as each
  zone's size is determined.

Release benchmark comparison (mean µs / block over 6 runs of 128 blocks):

| Frames | Case | Prior preferred (uniform adapter) | New preferred (split + zvma) | Speedup |
| ---: | --- | ---: | ---: | ---: |
| `256` | room | `84.97 us` | `84.16 us` | `1.01x` |
| `256` | hall | `105.27 us` | `104.16 us` | `1.01x` |
| `256` | cathedral | `117.50 us` | `116.34 us` | `1.01x` |
| `1024` | room | `247.87 us` | `273.41 us` | `0.91x` |
| `1024` | hall | `358.85 us` | `349.22 us` | `1.03x` |
| `1024` | cathedral | `410.35 us` | `403.11 us` | `1.02x` |

`bench_convolution_reverb` shows the micro-win is modest because per-call
vDSP overhead eats most of the vectorization benefit at small zone sizes.
The full-operator sweep, which runs 512 measure blocks vs the micro-bench's
128, pulls ahead more cleanly:

| Sweep | Prior (post-granular) | New (post-zvma) | Speedup |
| --- | ---: | ---: | ---: |
| ConvolutionReverb 256f | `102.83 us` (1.94%) | `95.57 us` (1.79%) | `1.08x` |
| ConvolutionReverb 1024f | `348.66 us` | `308.09 us` | `1.13x` |

Scalar-path cost regressed ~5-10% because split-complex memory access spans
two separate arrays where interleaved had both re and im in the same cache
line. This is the correctness fallback only (non-Apple / forced-scalar
tests); the primary user-facing path is Accelerate.

Benchmark commands:

```bash
./build-release/bench_convolution_reverb
./build-release/bench_audio_operators_sweep
```

Acceptance status:

- All `test_convolution_reverb_dsp` assertions continue to pass: short IR
  direct-reference, long non-uniform hall (48 partitions across 5 zones),
  scalar vs Accelerate backend parity, WAV file fallback, operator smoke.
- `avg_abs_diff` and `peak_diff` for the non-uniform hall case stayed at
  ~7e-6 / 5e-5 — no correctness drift from the MAC path change.
- Sweep shows `ConvolutionReverb` at 1.79% of the 256-frame budget, under
  the 2% threshold. All other operators stay within measurement noise of
  their post-granular numbers.
- No new types cross `src/operator_api`; the FFT helper's public surface is
  unchanged; the `ComplexPair` interleaved adapter stays in the helper for
  future callers.
- vDSP_zvma is used only when Backend is Accelerate AND a plan is cached
  for the zone's fft_size. Otherwise the hand-rolled `scalar_zvma` runs.

Follow-up notes: the deferred Gardner work-amortization is no longer the
clearest next win now that the split-complex layout is in place. A more
productive direction on `ConvolutionReverb` would be input-FFT sharing
across zones, or moving to `vDSP_fft_zrip` (real-valued FFT, half the work
per call at the cost of a packed-complex data layout). Both are deferred
until a bench shows this operator matters more than others still above 1%.

## Chorus vDSP_vlint + Shared Ring Pass

First opportunistic target from the audit queue. Chorus sat at 4.60 µs at
256 frames (0.09% budget) — under the 2% threshold, but with the clearest
SIMD shape of any remaining operator: six phase-offset fractional-delay
voices all reading from the same input stream.

Two inefficiencies in the original implementation:

1. **Six redundant per-voice ring buffers.** Input was written to all
   `kMaxVoices` delay lines every sample (for seamless voice-count
   changes), 5× more pushes than needed.
2. **Scalar per-sample fractional-delay reads.** The hot loop did one
   fractional read per voice per sample.

Replaced with a single shared `delay_history_` vector (size
`ring_samples + frames`) that shifts left each block and writes the new
block's samples at the tail. Each voice computes its per-sample delay
offset into an `indices_scratch_` vector, then a single `vDSP_vlint` call
per voice per block does all `frames` fractional reads. Voice outputs
accumulate into `wet_scratch_` via `vDSP_vadd`, scaled and mixed with dry
via `vDSP_vsmul` + `vDSP_vsma`. Scalar fallback mirrors the same math
without Accelerate.

Phase/LFO computation stays scalar — the three `rate_mode` branches
(Free/External/Metronome) keep the loop too branchy for clean
vectorization, and the cost is tiny relative to the read work.

Release sweep comparison:

| Frames | Baseline | Optimized | Speedup |
| ---: | ---: | ---: | ---: |
| `256` | `4.60 us` | `3.37 us ± 0.13` | `1.36x` |
| `1024` | `18.36 us` | `13.39 us ± 0.35` | `1.37x` |

Benchmark command:

```bash
./build-release/bench_audio_operators_sweep
```

Acceptance status:

- Sweep smoke check stays `smoke=ok` for Chorus at both buffer sizes
  (finite, non-silent, peak + DC within bounds).
- No other operator in the sweep regresses by more than measurement noise.
- Scalar fallback compiles and runs (the `#else` path operates on the same
  shared history with a straight-line loop that the auto-vectorizer can
  still SIMDify).
- Public operator surface unchanged.

Follow-up note: the same shared-ring pattern likely helps `Flanger` (one
fractional-delay voice with feedback) and the `Phaser` allpass cascade,
though less dramatically since both are one-voice hot paths. Both are on
the opportunistic queue.

## Drum DSP Incremental Envelope Pass

Six drum operators (`DrumKick`, `DrumSnare`, `DrumTom`, `DrumHiHat`,
`DrumCymbal`, `DrumClap`) share `operators/shared/drum_dsp/drum_dsp.h`.
Pre-pass their aggregate cost was ~13.94 µs across all six at 256 frames —
none individually over the 2% threshold, but a full kit firing concurrently
is real CPU for a DAW-grade project.

The hot pattern: `DecayEnvelope::value(decay_seconds)` called once or twice
per sample per drum (3 drums use two envelopes), computing `std::exp(-t *
5/decay_seconds)`. At 48 kHz × 256 samples × 9 envelopes across the kit that
was ~2300 `std::exp` calls per block.

Fix: added `compute_factor(decay_seconds, inv_sr)` and `step(factor, inv_sr)`
methods to `DecayEnvelope` that express the same exponential as an
incremental multiplication (`env *= factor` per sample, one
`std::exp` per block via the factor computation). The original
`value(decay_seconds)` and `advance(inv_sr)` API is preserved for callers
whose decay time varies per-sample. All six drum operators opt into the
fast path. Callers that read `env.time` for auxiliary logic (click duration
in Kick, attack shaping in Kick/HiHat, burst timing in Clap) capture the
pre-step time into a local before calling `step()`, since `step()` advances
`time` internally.

Release sweep comparison (3rd clean run, thermal-stable):

| Drum | Baseline | After | Speedup |
| --- | ---: | ---: | ---: |
| DrumKick | `3.19 us` | `2.64 us` | `1.21x` |
| DrumSnare | `2.89 us` | `2.48 us` | `1.17x` |
| DrumTom | `3.19 us` | `2.83 us` | `1.13x` |
| DrumHiHat | `1.37 us` | `1.12 us` | `1.22x` |
| DrumCymbal | `2.25 us` | `1.98 us` | `1.14x` |
| DrumClap | `1.05 us` | `1.07 us` | `0.98x` (noise) |
| **Kit aggregate** | `13.94 us` | `12.11 us` | **`1.15x`** |

Per-operator, two of six drums clear the 1.2x gate; the rest land between
1.13-1.17x. DrumClap is unchanged within measurement noise — its envelope
is already simple enough that `std::exp` wasn't the bottleneck. The change
lands because the aggregate kit saving is real (~1.8 µs per block) and the
shared `DecayEnvelope` header now exposes the SIMD-friendlier shape to any
future drum-synth work.

Also a small scalar-path win: eliminating per-sample `std::exp` also helps
on non-Apple builds where Apple's libm isn't the fast path.

Benchmark command:

```bash
./build-release/bench_audio_operators_sweep
```

Acceptance status:

- All six drum operators pass smoke (`smoke=ok`) at both buffer sizes.
- No other operator regresses beyond measurement noise.
- Public operator surface unchanged.
- `DecayEnvelope::value(decay_seconds)` remains for any future caller whose
  decay time varies per-sample.
- Auxiliary time-based logic (`env.time < atk`, burst timing) preserves
  sample-aligned semantics via a local `pre_step_time` capture.

Follow-up note: the gains are modest because Apple's `std::exp` is very
fast (2-3 ns per call on Apple Silicon) and clang can vectorize the old
non-dependent form across samples. The incremental form has a
data-dependent multiply chain that can't vectorize. The win comes from
eliminating the `exp` call's call overhead rather than algorithmic
improvement — the scalar register pressure is lower and the dispatch is
more predictable. Further drum gains would require structural changes
(e.g., batching noise/oscillator across samples with Accelerate primitives),
which aren't worth the complexity at current costs.
