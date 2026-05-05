# Plan: Stereo-aware Mixer (per-input pan + gain)

**Status:** unstarted. Single PR, ~150 lines net of changes.

## Problem

`operators/audio/mixer/mixer.cpp` is hardcoded mono on every port:

```cpp
// collect_ports
pd.channels = 1;          // every input_X
out_port.channels = 1;    // output
```

Most audio in vivid is stereo: `WavetableLayer/output`, `WavetableOsc/output`, `Sampler/output`, `FmSynth/output`, `audio_out/input` — they all carry 2 channels. Routing any of those into Mixer collapses the stereo image to mono (the runtime sums L+R into a single channel at the port boundary), which is silently destructive — there is no warning, just a quietly mono'd output.

The runtime *does* preserve stereo when **multiple connections sum into a single sink port** (verified empirically via `analyze_stereo_image` on `audio_out/input` with two stereo synths fanned in: `correlation = 0.385`, `side_rms = 0.071` — true stereo at the mix). So users get correct stereo summing for "throw a few synths at audio_out", but they have **no first-class operator** for "stereo bus with explicit per-source gain knobs". The closest fits are:

- **Multi-connect to audio_out** — works, stereo-faithful, but no per-source gain.
- **Two parallel mono Mixers (one for L, one for R)** — would need a stereo-split operator that doesn't exist; routing a stereo signal into a mono Mixer port collapses to mono *before* you'd split.
- **`VoiceMixer`** (vivid-wavetable) — different abstraction (per-voice envelope routing inside a synth chain). Not a free-standing stereo bus.

## Goal

Make `Mixer` accept stereo inputs natively, with per-input gain *and* pan. Backwards-compatible at the param-name level (`gain_X` keeps its meaning) — existing graphs upgrade in place. Gain stays mono-friendly: connecting a 1-channel source still works, the mixer just duplicates that signal across both output channels with `pan_X` controlling the spread.

The result is a stereo bus that fits naturally between synths and `audio_out`, with no extra operators in between.

## Design

### Ports

```cpp
// 16 stereo inputs (channels=2 each)
for (int i = 0; i < kMaxInputs; ++i) {
    VividPortDescriptor pd{};
    pd.name             = port_names_[i];      // "input_0".."input_15"
    pd.type             = VIVID_PORT_AUDIO_BUFFER;
    pd.direction        = VIVID_PORT_INPUT;
    pd.transport        = VIVID_PORT_TRANSPORT_AUDIO_BUFFER;
    pd.channels         = 2;                   // ← was 1
    pd.repeat_group     = "input";
    pd.repeat_group_idx = static_cast<uint16_t>(i);
    out.push_back(pd);
}

// Stereo output
VividPortDescriptor out_port{};
out_port.name      = "output";
out_port.type      = VIVID_PORT_AUDIO_BUFFER;
out_port.direction = VIVID_PORT_OUTPUT;
out_port.transport = VIVID_PORT_TRANSPORT_AUDIO_BUFFER;
out_port.channels  = 2;                        // ← was 1
out.push_back(out_port);
```

### Params (per input × 16)

Each input keeps its existing `gain_X` (range `0..2`, default `1`) and gains a new sibling `pan_X` (range `-1..+1`, default `0` = center). Total params: 32 (was 16). Param-collection order is paired (`gain_0, pan_0, gain_1, pan_1, ...`) so the editor's `repeat_group("input", i)` hint groups them per row.

```cpp
struct InputParams {
    char gain_name[16];
    char pan_name[16];
    char gain_desc[80];
    char pan_desc[80];
    vivid::Param<float> gain{nullptr, 1.0f, 0.0f, 2.0f};
    vivid::Param<float> pan {nullptr, 0.0f, -1.0f, 1.0f};
};
```

Pan law: equal-power.

```cpp
inline void pan_gains(float pan, float& gl, float& gr) {
    // pan ∈ [-1, +1] → angle ∈ [0, π/2]
    float angle = (pan * 0.5f + 0.5f) * 0.5f * static_cast<float>(M_PI);
    gl = std::cos(angle);
    gr = std::sin(angle);
}
```

This matches `WavetableLayer`'s unison pan curve (`src/wavetable_layer.cpp:360-364`) and is the standard mixing-console law: at center, both channels get `cos(π/4) ≈ 0.707`; at hard left, L = 1, R = 0; at hard right, L = 0, R = 1. Power across the pan field stays constant (no bump or dip at center).

### Process loop

```cpp
void process_audio(const VividAudioContext* ctx) override {
    float* out_l = ctx->output_buffers[0];
    float* out_r = ctx->output_buffers[0] + ctx->buffer_size;  // planar stereo
    const uint32_t n = ctx->buffer_size;

    std::memset(out_l, 0, 2 * n * sizeof(float));   // both channels at once

    for (int i = 0; i < kMaxInputs; ++i) {
        const float* in = ctx->input_buffers[i];
        if (!in) continue;
        const float g = ip_[i].gain.value;
        if (g == 0.0f) continue;

        const uint32_t in_ch = ctx->input_channel_counts ? ctx->input_channel_counts[i] : 2u;

        float pl, pr;
        pan_gains(ip_[i].pan.value, pl, pr);
        const float gl = g * pl;
        const float gr = g * pr;

        if (in_ch >= 2) {
            // Stereo input: pan rotates the existing stereo image.
            // (gain_l = cos(angle), gain_r = sin(angle); at center both = 0.707)
            const float* in_l = in;
            const float* in_r = in + n;
            for (uint32_t s = 0; s < n; ++s) out_l[s] += in_l[s] * gl;
            for (uint32_t s = 0; s < n; ++s) out_r[s] += in_r[s] * gr;
        } else {
            // Mono input: same source goes to both legs, pan picks the split.
            for (uint32_t s = 0; s < n; ++s) {
                out_l[s] += in[s] * gl;
                out_r[s] += in[s] * gr;
            }
        }
    }
}
```

The mono branch matches the existing v1 behavior (mono-summing) when `pan = 0`: both `gl = gr = 0.707`, so a mono input fans equally to both output legs — same result a v1 graph would have produced if it had a stereo output downstream. For graphs that previously summed mono synths into the v1 mono mixer, the migration replays bit-equivalent on the L (or R) channel; `audio_out` then gets a duplicated dual-mono stream, indistinguishable from the v1 single-channel signal once the engine routes it.

The stereo branch preserves the source's own stereo image: `correlation` survives the trip through Mixer, so a wide synth stays wide.

### Edge cases

- **Disconnected input**: `in == nullptr` → skipped. Same as v1.
- **Gain = 0**: skipped. Same as v1.
- **`input_channel_counts == nullptr`**: assume stereo. Defensive default that matches the declared port topology.
- **1-channel input on a 2-channel port**: handled by the `in_ch < 2` branch above. The runtime *should* upcast 1→2 by duplication before delivery, but we don't rely on that — we read the explicit channel count and fan in code.
- **Sub-buffer aliasing** between mono and stereo paths within one call: not possible; the channel count is per-input and constant for the call.

## Files to modify

- `operators/audio/mixer/mixer.cpp` — entire change. ~50 lines added/changed.
  - Add `pan_X` param per input in `InputParams` struct.
  - Generate `pan_X` name + description in constructor.
  - Push pan params alongside gain in `collect_params` (`semantic_tag = "balance_-1_1"` if such a tag exists; otherwise omit).
  - Bump `pd.channels = 2` and `out_port.channels = 2` in `collect_ports`.
  - Rewrite `process_audio` to the loop above.
- `operators/audio/mixer/factory_presets.json` — only if presets exist (none today). Skip.

No changes to runtime, ABI, or other operators. The `repeat_group("input", i)` UI metadata still works for grow-on-connect — the editor sees both `gain_X` and `pan_X` in the same group.

## Migration

Saved graphs that already use Mixer will hot-load: the new `pan_X` params take their default of 0 (center), so any existing patch behaves exactly as before. Graphs that previously routed stereo into Mixer and accepted the silent mono collapse will now hear the stereo image they always intended.

The `Mixer.output` channel count change from 1 → 2 is the only ABI-shaped concern. If a downstream port expects exactly 1 channel and the runtime doesn't auto-collapse 2→1 with summing, those connections could change behavior. Survey of existing graphs that consume `Mixer/output`:

```bash
grep -rn '"from": "[^"]*/output"' graphs/ | grep -v build | xargs grep -l 'Mixer'
```

Run this as part of the PR. Any consumer expecting mono either needs to be promoted to stereo (most common — they're heading to `audio_out/input` or another stereo synth chain) or the graph stays mono by setting all `pan_X = 0` and reading channel 0 only (downstream's call). In practice almost every realistic Mixer use case wants the stereo upgrade.

## Verification

### Unit test — `tests/operators/test_mixer.cpp` (new)

```cpp
// Verify:
//   1. Port surface — 16 stereo inputs, 1 stereo output, 32 params
//      (gain_0..15, pan_0..15).
//   2. Single mono input panned hard left → only L channel non-zero.
//   3. Single stereo input pan=0 → output equals input (both legs).
//   4. Two stereo inputs at pan=±0.5 → side_rms (L−R RMS) > 0
//      (proves stereo image preservation).
//   5. Disconnected input contributes silence.
//   6. gain_X = 0 contributes silence.
```

Use the same `OperatorLoader` + `TestContext` pattern as `tests/operators/test_note_modulator.cpp` (vivid). The harness must declare the buffers as planar stereo (`float buf[2 * kFrames]`) and set `input_channel_counts[i] = 2` (or 1 for the mono input test).

Wire into `cmake/tests/30-ops-stability-domains.cmake` next to the other operator smoke tests.

### Integration test — graph round-trip

Add a fixture graph `graphs/audio/stereo_mixer_smoke.json`:
- Two `Oscillator` sources at different freqs, connected to `mixer/input_0` and `mixer/input_1`.
- `mixer/output → audio_out/input`.
- Defaults: pan_0 = -0.6, pan_1 = +0.6, gain_X = 0.5.

After load, MCP `analyze_stereo_image` on the final mix should report:
- `is_stereo = true`
- `correlation < 0.5`
- `side_rms > 0.05`

### Live verification scenario

The vivid-wavetable session that prompted this plan was:
```
chords ──→ arp ──→ nm ──→ wt        ──┐
                                      ├──→ audio_out  (multi-connect sum)
chords_bass ──→ wt_bass ──────────────┘
```

After this PR lands, that graph should rewire as:
```
chords ──→ arp ──→ nm ──→ wt        ──→ mixer/input_0
chords_bass ──→ wt_bass ──────────────→ mixer/input_1
mixer/output                          ──→ audio_out/input
```

…with `gain_0`, `gain_1`, `pan_0`, `pan_1` available as live-tweakable params on the Mixer node. `analyze_stereo_image` should still report stereo on the final mix, and `mixer/output` peak should track `gain_0 + gain_1` mixed contributions.

## Out of scope for v1

- **Per-input mute/solo** — useful but adds 32 more params; stays in a future PR.
- **Stereo width control per input** — collapse / widen the source's own L/R via mid/side rotation. Niche; users can put a `StereoPanWidth` (already exists, line 215 of `cmake/operators.cmake`) before the mixer for now.
- **VU / peak meters per input** — Mixer already exposes `rms` / `peak` / `waveform` analysis ports on the *output*. Per-input metering is doable (requires 16 more output ports) but bloats the operator surface for a feature most users won't wire.
- **Bus-style send/return** — a separate operator entirely.

## Critical files

- **Modify:** `operators/audio/mixer/mixer.cpp`
- **Create:** `tests/operators/test_mixer.cpp`, `graphs/audio/stereo_mixer_smoke.json`
- **Modify:** `cmake/tests/30-ops-stability-domains.cmake` (register the new test next to `test_note_modulator`)
- **Reference (do not modify):** `src/wavetable_layer.cpp:360-364` — pan-law canonical implementation to mirror.

## What this drops

Nothing. Mixer is currently the only mono-port summing operator in the audio domain; the v1 use case (sum mono audio sources to a mono bus) becomes a degenerate case of the v2 path with all `pan_X = 0`. No removed surface.
