# Core SIMD Follow-Ons

## Summary

This note ranks likely next adopters of the shared `Highway`-based SIMD foundation after the `WavetableLayer` migration foundation is in place. It is intentionally post-foundation and non-blocking for the wavetable redesign: `WavetableLayer` remains the primary delivery stream, and broader SIMD adoption is a follow-on benefit rather than a prerequisite for cutover.

## Ranked Candidates

### 1. `SpectralFreeze`

Strong SIMD target.

Why:

- dense windowing, FFT-adjacent numeric loops, overlap-add, and polar conversion work
- lots of contiguous buffer math with low control-flow complexity
- high likelihood of benefiting from shared vector kernels or a broader DSP-helper review

Caution:

- this operator may eventually justify a wider DSP-library decision in addition to `Highway`, because some of its cost is algorithmic and FFT-shaped rather than just elementwise math

### 2. `Vocoder`

Strong SIMD target.

Why:

- repeated per-band filter-bank work
- envelope-follower math over many similar lanes
- naturally vectorizable across bands once data layout is cleaned up

Caution:

- recursive filter state means layout matters; wins are good but depend on batching the bands cleanly

### 3. `GranularSynth`

Strong SIMD target.

Why:

- many similar grains with interpolation, windowing, and accumulation
- lots of repeated sample math that can benefit from batched kernels

Caution:

- grain scheduling and buffer-wrapping logic still need architecture review so SIMD lands on the hot numeric core, not the irregular control path

### 4. Shared gain/mix/pan kernels

Strong shared-infrastructure target.

Why:

- simple multiply/add kernels appear all over audio graphs
- not individually glamorous, but high leverage as reusable core helpers
- good candidate for a thin internal helper layer once repeated SIMD kernels emerge

Caution:

- this should stay an internal helper surface, not a public operator API commitment

### 5. `Reverb`

Moderate SIMD target.

Why:

- parallel comb sections can benefit from vector-style processing
- some repeated delay-line math is regular enough to optimize

Caution:

- recursive state and feedback paths make the payoff smaller and more design-sensitive than in a filter bank or wavetable renderer

### 6. `ParametricEQ`, compressor, limiter, and the filter family

Lower-priority targets until architecture review.

Why lower:

- recursive/stateful IIR and dynamics code usually gets smaller wins from SIMD alone
- architecture and state layout often matter more than simply adding vector intrinsics

When to revisit:

- after profiling shows a real hotspot
- after data layout or multi-channel batching makes the code more SIMD-friendly

## General Heuristic

Good SIMD candidates tend to have:

- dense parallel math
- repeated work across bands, voices, or grains
- contiguous buffer access
- low branch pressure in the hot loop

Lower-payoff candidates tend to have:

- recursive per-sample state
- heavy control-flow divergence
- modest total runtime share relative to engineering complexity

## Relationship To The WavetableLayer Migration

- `WavetableLayer` is still the first production consumer of the shared core SIMD foundation.
- Nothing in this note is a prerequisite for Phases 2-6 of the wavetable redesign.
- If repeated kernels emerge during or after the wavetable work, they may justify a thin internal SIMD helper layer in core, but that is a follow-on refinement, not part of the wavetable cutover gate.
