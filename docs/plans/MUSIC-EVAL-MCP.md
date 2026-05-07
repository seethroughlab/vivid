# Music Evaluation — Implementation Plan

Adding a music-aware reflective layer to Vivid's MCP perception surface, backed by a Large Audio-Language Model (LALM) running in a sidecar inference service.

## Context

Vivid already exposes a comprehensive MCP surface through `mcp/vivid_mcp.py` (live runtime control) and `mcp/vivid_opdev_mcp.py` (operator authoring), and the perception system is organized into three layers — Introspection, Analysis, and Checks. Existing audio tools live in the Analysis layer:

- `analyze_output(mode="audio", window_seconds=1.0, ...)` — RMS, peak, spectrum, LUFS over a window
- `compare_outputs(mode, window_seconds_a, window_seconds_b, ...)` — A/B numeric diff
- `sample_node_outputs(node_id, duration_seconds, interval_ms, ...)` — time-series of port values

These are quantitative. They tell you _that_ the kick is loud, not _why_ the kick is fighting the bass; _that_ the spectrum is bright, not _that_ the chord progression has resolved or stalled. The "Reflective" LLM role described in `docs/LLM-INTEGRATION.md` §4.4 is currently underserved precisely because the perception surface lacks a tool that can reason about music as music.

This plan adds that tool surface. The model of choice is **Music Flamingo (NVIDIA, Nov 2025)**, an 8B LALM built on Audio Flamingo 3 with chain-of-thought + GRPO post-training specifically for music reasoning. The architecture is designed to be backend-pluggable so AF3, Qwen3-Omni, or Gemini 2.5 Pro can substitute without changing the MCP tool signatures.

## Goals

- Add semantic, music-aware audio evaluation to the existing perception layer.
- Match existing tool conventions: compact-by-default responses, `include_payload=true` for full output, `{ ok:false, error:{...} }` envelopes on failure.
- Keep model inference out of the MCP bridge process — bridges stay lightweight and stdio-fast.
- Make the backend pluggable from day one (local MF, local AF3, Qwen3-Omni, Gemini API).
- Support both live capture (current audio output) and reference-based comparison (current vs. intent or current vs. previous render).

## Non-goals

- Real-time per-frame audio evaluation. LALM inference is seconds-scale; this is a reflective tool, not a control-rate signal.
- Music generation. This is evaluation only. Generation lives elsewhere if it ever lands.
- Replacing existing metric-based audio analysis. The metric tools and the LALM tools complement each other — metrics find _where_, LALM explains _why_.
- Built-in chat. Same reasoning as Path 2 in the existing LLM integration doc — Claude Code/Cursor already cover this.

## Architecture

```
LLM client (Claude Code, Cursor)
        │  stdio (MCP)
        ▼
mcp/vivid_mcp.py  ──── new music_eval tool family
        │  HTTP                    │  HTTP
        ▼                          ▼
Vivid HTTP control server   music_eval_service (new)
   (127.0.0.1:9876)            (127.0.0.1:9877, configurable)
        │                          │
        ▼                          ▼
  running Vivid instance     loaded LALM (MF / AF3 / Qwen3-Omni / Gemini)
```

Three reasons to factor the inference service out rather than embedding it in `vivid_mcp.py`:

1. **Process model.** The MCP bridge is a stdio process spun up per-client. Loading an 8B model on every spawn is wrong; the model wants to be warm and resident.
2. **GPU coexistence.** Vivid is already using GPU heavily for WebGPU/Dawn rendering. The inference service should be able to run on a different device, a remote box, or fall back to CPU/cloud without touching the bridge.
3. **Backend swapping.** Replacing MF with Gemini API is a service-internal change. The MCP tool surface should not move.

The service is a small FastAPI app exposing a handful of endpoints:

```
POST /v1/evaluate         body: { audio_b64, sample_rate, mode, prompt }
POST /v1/compare          body: { audio_a_b64, audio_b_b64, intent, sample_rate }
GET  /v1/status           returns: { backend, model, device, ready, queue_depth }
POST /v1/configure        body: { backend, model_path?, api_key?, ... }
```

The service owns: model loading and lifecycle, audio resampling (Vivid's 48 kHz stereo → model's expected mono 16/24 kHz), tokenization, batching if multiple requests are queued, and license-acceptance gating for non-commercial models.

## How audio gets to the service

Vivid already exposes audio capture through the HTTP control server — `analyze_output(mode="audio", window_seconds=N)` returns metric-extracted audio. For LALM evaluation we need the raw waveform, not the metrics. Two options:

**Option A: extend the existing capture endpoint.** Add a `return_buffer=true` parameter (or a new `mode="audio_raw"`) that returns the captured float buffer alongside the metrics. The MCP tool implementation grabs the buffer from the control server, base64-encodes it, and POSTs to the music_eval service.

**Option B: have the service pull from Vivid directly.** The service knows the control server URL and pulls the buffer itself. Avoids round-tripping the buffer through the MCP bridge.

Recommend Option A. It keeps Vivid as the single source of audio truth, mirrors how `capture_image` already works for frames, and avoids creating a new direct dependency between the inference service and Vivid's runtime.

For longer windows (Music Flamingo handles up to 20 minutes), the same path works — the existing `window_seconds` parameter is already free-form. An additional path can write to a temp WAV for very long captures rather than serializing megabytes through HTTP.

## Tool surface (proposed)

Following existing naming conventions and the perception MCP response policy:

**`evaluate_audio_musically(window_seconds=30.0, node_id="", mode="caption", include_payload=false)`**
Captures a window of audio output and returns a music-aware analysis. `node_id` defaults to the audio sink; passing a specific audio operator solos that node's output. `mode` is one of:

- `"caption"` — short structured caption (key, tempo, instrumentation, mood, ~2 sentences)
- `"theory"` — theory-aware analysis (harmony, voice leading, rhythmic structure, form)
- `"reasoning"` — full chain-of-thought via `<think>` tags before the answer (uses MF's CoT post-training, slower)

Compact response: `{ ok, key, tempo_bpm, summary, mode }`. Full payload includes the raw model response, CoT trace if applicable, and timing.

**`compare_audio_to_intent(reference_path="", intent="", window_seconds=30.0, node_id="", include_payload=false)`**
Compares a captured window of current audio to either a reference clip, a free-text intent description, or both. This is the primary creative-iteration tool — "does what I'm rendering match what I was going for?" Returns a semantic diff: matched aspects, deviations, and a confidence score.

Compact response: `{ ok, match_score, key_deviations, summary }`. Full payload includes per-axis breakdowns (harmony match, rhythm match, timbre match, structure match) and the model's reasoning.

**`compare_audio_versions(window_seconds_a, window_seconds_b, focus="", include_payload=false)`**
A/B comparison of two captured windows from the same running graph. Mirrors the existing `compare_outputs` shape but with semantic rather than numeric diff. `focus` is an optional free-text axis ("rhythm tightness", "harmonic motion", "stereo image") to bias the analysis.

**`evaluate_audio_file(path, start_seconds=0.0, duration_seconds=30.0, mode="caption", include_payload=false)`**
Same as `evaluate_audio_musically` but reads from a file rather than capturing live. Useful for evaluating rendered exports, reference tracks the user has on disk, or stems exported from Vivid's `audio_out` to a file via existing render paths.

**`music_eval_status()`**
Returns service reachability, currently loaded backend, model name, device (`cuda:0`, `mps`, `cpu`), `ready` flag, and queue depth. Mirrors the shape of `runtime_status()`.

**`configure_music_eval_backend(backend, model_path="", api_key="", device="auto")`**
Switches the inference backend. Valid values: `"music_flamingo"`, `"audio_flamingo_3"`, `"qwen3_omni"`, `"gemini_api"`. The service handles loading/unloading; the bridge just forwards. License acceptance for non-commercial backends gates here — first call to MF or AF3 returns an error with the license URL until acceptance is recorded.

## Checks integration

The Checks layer (`docs/LLM-INTEGRATION.md` §9.2) is where intent gets persisted. Music-aware checks let the user say "this section should stay in F major and around 125 BPM" and have it validated automatically. Two new check types:

- **Theory-bound check:** asserts a captured window matches stated theoretical attributes — key, mode, tempo range, time signature. Implementation calls `evaluate_audio_musically(mode="theory")` and compares structured fields.
- **Intent-bound check:** asserts a captured window matches a stored intent description. Implementation calls `compare_audio_to_intent` with a stored reference + intent and asserts `match_score >= threshold`.

These checks are expensive (seconds-scale per evaluation) so they should only run on demand or as part of a CI pass, not every frame. The existing `run_checks(checks, include_payload)` surface accommodates this — checks already vary in cost and the response policy is built for it.

## Implementation phases

**Phase 0 — Spec freeze (1 day).** Pin tool signatures and response shapes against existing perception conventions. Get a `MUSIC-EVAL.md` doc in `docs/` describing the tool surface so the bridge and service can be developed in parallel.

**Phase 1 — Service skeleton with stub backend (2 days).** FastAPI app with all endpoints implemented, returning canned responses. Lets Phase 3 proceed without GPU. Add `music_eval_service` as a sibling Python project under `mcp/` or a top-level `services/music_eval/` directory. License gating logic, configuration loading, queue/lock for serial inference, structured logging.

**Phase 2 — Music Flamingo backend (3–5 days).** Real inference via `nvidia/music-flamingo-hf` through Hugging Face Transformers. Audio resampling pipeline (48 kHz stereo float → 16 kHz mono). Device detection (CUDA → MPS → CPU). Warm-load on first request, optional preload on service start via env var. Smoke test: feed Heaven's Gate render, get back a caption that mentions the actual key and tempo.

**Phase 3 — MCP tool surface in `vivid_mcp.py` (2 days).** New tools wired through to the service. Compact + full payload variants. Error envelopes consistent with existing perception responses. Update the bridge smoke runner (`scripts/mcp_bridge_smoke.py`) with a music-eval preset.

**Phase 4 — Audio capture path (1–2 days).** Extend Vivid's HTTP control server to return raw audio buffers when requested. Validate that the existing `analyze_output(mode="audio")` accepts an enlarged `window_seconds` (or add a new mode). For windows over a minute, switch to writing a temp WAV and passing the path. This phase touches Vivid C++ — small extension, not a new subsystem.

**Phase 5 — `compare_audio_to_intent` (2 days).** The creative-iteration tool. Two-clip prompt construction for MF, intent text injected as part of the prompt, structured diff output. This is the tool I'd expect to use most heavily during album work.

**Phase 6 — Checks integration (1–2 days).** Two new check types defined in JSON, validation logic in the service, wiring through `run_checks`. Probably worth seeding with one example check per check-type in the existing checks examples directory.

**Phase 7 — Pluggable backends (3 days, optional).** AF3 backend (same model family, broader sonic coverage for sound-design-heavy passages). Qwen3-Omni (Apache-licensed Qwen3 variants — verify the specific release before relying on this). Gemini 2.5 Pro via API (no local GPU required, audio-native, simplest fallback for client work where MF's NC license is a problem).

Total: roughly 2–3 weeks of focused work for everything through Phase 6, with Phase 7 as an as-needed extension.

## Open questions and risks

**Latency and UX.** A 30-second clip evaluated through MF on a single 4090 is roughly 5–10 seconds of inference. Clients should treat these tools as slow and async-friendly. Worth deciding whether the MCP tool blocks until done or returns a job ID immediately and exposes a `poll_music_eval_job` companion. Recommend blocking for v1 — simpler, matches how `analyze_output` already behaves — and revisit if the album workflow surfaces concrete pain.

**License gating.** MF is non-commercial. Vivid is source-available. The licenses don't conflict, but a user on a commercial project who installs MF and then ships work made with its evaluations is in murky territory. The `configure_music_eval_backend` call should display the relevant license at setup time and record acceptance, similar to how some packages handle EULAs. For commercial work, default to recommending the Gemini backend.

**Avant-garde / IDM coverage.** MF was trained heavily on songs and recognizable genres. For long-form drone, granular textures, and non-tonal experimentation — exactly the territory of the cult-themed album — it will produce generic descriptions when it loses its footing. AF3 may actually do better on these passages because it has broader sonic vocabulary even if it's less theory-aware. This is a real reason to want both backends available, not just a license fallback.

**GPU contention with Vivid.** Vivid is using the GPU continuously through Dawn. Spawning MF inference on the same device will cause stutters in rendering. Document the recommendation: separate device, or run the service remotely, or accept that evaluation pauses live work. Service config should expose `device` explicitly so users can pin MF to a second GPU if they have one.

**Audio routing for solo'd nodes.** Today's `analyze_output` operates on the master output. For evaluating a single audio operator's contribution (e.g., "is this synth voice working in this chord progression?"), the service needs that operator's output in isolation. Vivid already has solo mode mentioned in §9.2, so the MCP tool should accept `node_id` and route through solo capture rather than master capture.

**Long-term: own perception layer or augmentation.** Right now this fits cleanly inside Layer 2 (Analysis) of the existing perception system. If the LALM evaluation grows to include things like operator-level musicality scoring, automatic check generation from natural-language intent, or multi-pass refinement loops, it may eventually want its own layer. Not a Phase 1 concern, but worth flagging for the architecture doc.

## References

- Vivid LLM integration: `docs/LLM-INTEGRATION.md` (existing)
- Music Flamingo paper: arXiv 2511.10289 (Nov 2025)
- Music Flamingo on HF: `nvidia/music-flamingo-hf`
- Audio Flamingo 3: `research.nvidia.com/labs/adlr/AF3/`
- Qwen3-Omni technical report: arXiv 2509.17765
