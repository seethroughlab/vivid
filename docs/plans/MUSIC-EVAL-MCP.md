# Music Evaluation — Architecture Revision Plan

This document evaluates and revises the music-eval architecture before further implementation work. The canonical tool/service contract lives in `docs/MUSIC-EVAL.md`; this plan is the engineering decision record for what the architecture should be and what scope belongs in v1.

## Summary

The core direction is right:

- music-aware evaluation belongs in the perception layer
- model inference should stay out of `mcp/vivid_mcp.py`
- a persistent sidecar service is the right place for heavyweight model lifecycle

The original plan was unsound in three places:

1. it picked the wrong capture seam by leaning on `analyze_output(...)` instead of the raw-audio capture path
2. it treated long-window `node_id` capture as if Vivid already had a clean isolated-audio API for that
3. it treated reflective LALM output as if it were stable enough to become hard CI checks immediately

This revised plan narrows v1 to the parts that fit Vivid's current architecture cleanly and defers the ambiguous parts until the runtime has the right capture primitive.

## Current State

Some of this system already exists in the repo:

- `mcp/vivid_mcp.py` already has a `music_eval` tool family and service launcher
- `services/music_eval/` already exists with a FastAPI app, stub backend, Music Flamingo backend, and license gating
- `docs/MUSIC-EVAL.md` already acts as the protocol spec
- the control server already exposes raw audio extraction endpoints such as `capture_audio` and `capture_node_audio`

So the problem is no longer "should we add music eval at all?" The problem is "what should the stable architecture be before we keep building on top of it?"

## Architectural Decisions

### 1. Keep the sidecar split

The sidecar service remains the correct top-level design:

```text
LLM client
   -> MCP stdio
   -> mcp/vivid_mcp.py
   -> Vivid control server (live capture, graph ownership)
   -> music_eval service (model ownership)
```

Why this stays:

- the MCP bridge is short-lived and should not own an 8B model lifecycle
- Vivid's runtime already has enough GPU pressure without mixing in model startup concerns
- backend swapping is cleaner when the bridge only speaks a small HTTP protocol

### 2. Use raw audio capture as the seam, not `analyze_output`

This is the most important correction.

Vivid's current architecture already treats the C++ side as a raw capture provider and the Python side as the analysis layer. Music eval should follow that same seam:

- the control server owns waveform capture
- the MCP bridge owns marshaling that waveform into the service request
- the music-eval service owns resampling, prompting, inference, and response shaping

Do not make music eval depend on `analyze_output(mode="audio")` or a special `audio_raw` analysis mode. That would re-couple semantic evaluation to a metrics endpoint when the repo is already moving the opposite direction.

**Preferred live-capture primitives:**

- final mix: `capture_audio`
- file-based evaluation: `evaluate_audio_file`
- future isolated capture: a dedicated long-window audio capture endpoint, not an overload of `analyze_output`

### 3. Treat long-window per-node capture as unresolved, not solved

The old plan assumed `node_id` could mean "analyze this operator's isolated musical contribution over 30 seconds." That is not a stable capability today.

Today there are two different concepts:

- `capture_node_audio`: a tiny per-node waveform ring snapshot, useful for debug plots but not long-window musical evaluation
- temporary solo during deferred analysis capture: useful internally, but it mutates session-wide solo state and is not yet a clean public contract for long-window isolated audio capture

So for architecture purposes, v1 should choose one of these explicitly:

- **Preferred:** live music-eval tools operate on the final mix only
- **Optional experimental path:** `node_id` is allowed but documented as session-mutating and not suitable for deterministic automation

This plan recommends the first option. If isolated long-window capture matters, add a dedicated runtime feature for it rather than pretending the current solo path is already a finished contract.

### 4. Keep the service pure: it evaluates audio, it does not own runtime capture

The service should not call Vivid directly.

Reasons:

- Vivid remains the sole owner of live graph/session state
- the service stays reusable for live captures, rendered files, and reference files
- we avoid a second client talking to the control server with its own lifecycle and configuration rules

That means the service API should stay focused on:

- `POST /v1/evaluate` with inline audio or file path
- `POST /v1/compare` with current audio plus reference and/or intent
- `GET /v1/status`
- `POST /v1/configure` only if we are comfortable with its ownership semantics

### 5. Make service ownership explicit

The current service shape is a singleton with mutable global backend/device state. That is acceptable only if we document the ownership model clearly.

For v1, the safest rule is:

- `music_eval_status()` is always safe
- backend selection is primarily startup-time configuration
- `configure_music_eval_backend(...)` is allowed only as an explicit global side effect and should be treated as "reconfigure the shared service," not "set my session preference"

If we want per-session backend choice later, that requires a different service model:

- one sidecar per bridge-managed session, or
- a multi-tenant service with explicit session IDs

Neither should be smuggled into v1 accidentally.

### 6. Checks should start advisory, not authoritative

Music-eval output is excellent for reflective critique and creative iteration. It is much weaker as a hard regression oracle unless the model response is constrained into a stable schema.

The immediate problem areas are:

- key/tempo extraction derived from free-form model text
- semantic match scores derived from prompt-following rather than calibrated metrics
- backend variance across MF / AF3 / Gemini / Qwen

So v1 checks should be framed as:

- manual review aids
- non-critical installation monitoring
- optional warnings in `run_checks`

Hard CI gating should wait until we either:

- get a backend that emits structured fields directly, or
- add a strict post-processing layer with format validation and confidence thresholds

## Recommended v1 Scope

The architecture is sound if v1 is narrowed to these capabilities:

### In scope

- sidecar service with stub backend and one real backend
- `music_eval_status()`
- `evaluate_audio_file(...)`
- `evaluate_audio_musically(...)` on final mix
- `compare_audio_to_intent(...)` against intent text, reference file, or both
- bridge-managed service startup and clear error envelopes
- explicit docs for latency, license gating, and GPU contention

### Out of scope for v1

- long-window isolated `node_id` capture
- `compare_audio_versions(...)` as a durable "before/after" semantic oracle for live generative material
- critical CI checks based on model-generated theory or match scores
- multiple backends with equal support quality
- async job queues and polling APIs

## Tool Surface Revisions

### `evaluate_audio_musically`

Keep the tool, but v1 should operate on the final mix unless and until a real isolated-capture API exists.

If `node_id` remains in the signature for compatibility, document it as one of:

- ignored for now, or
- experimental and session-mutating

Do not promise clean isolated operator evaluation until the runtime has that capture primitive.

### `compare_audio_to_intent`

This should be the primary creative tool in v1.

Why it survives intact:

- it aligns with the reflective role in `docs/LLM-INTEGRATION.md`
- it works well with either a reference file, an intent string, or both
- it does not require the service to infer a "before/after edit" story from two arbitrary live windows

### `compare_audio_versions`

De-scope this from the architectural core of v1.

Sequential live captures from a running graph are ambiguous for:

- generative graphs
- evolving patches
- tempo drift
- non-looping arrangements

If we want version comparison later, it should prefer stable artifacts:

- rendered files
- saved references
- explicit "before render" and "after render" inputs

### `configure_music_eval_backend`

Keep only if the global nature is clearly documented.

If that feels too footgun-prone, reduce v1 to:

- environment-based backend selection
- `music_eval_status()` for discovery

and postpone runtime reconfiguration.

## Implementation Plan

### Phase 0 — Architecture realignment

- Treat `docs/MUSIC-EVAL.md` as the protocol source of truth
- Revise that spec where it promises more than the runtime can cleanly support
- Document the service ownership model explicitly
- Decide whether `node_id` remains in the public live-capture tools for v1

### Phase 1 — Service baseline

- Keep `services/music_eval/` as the home for the sidecar
- Keep the stub backend for deterministic tests and development without GPU
- Land one real backend well rather than several partially supported ones
- Keep the service focused on audio-in, JSON-out

### Phase 2 — Bridge integration on the raw-audio seam

- Fetch live audio through raw capture endpoints, not `analyze_output(...)`
- Marshal short captures as inline WAV base64
- Marshal longer captures as temp WAV paths
- Normalize service failures into the standard MCP `{ ok:false, error:{...} }` envelope

### Phase 3 — Final-mix evaluation workflows

- `evaluate_audio_file(...)`
- `evaluate_audio_musically(...)` on final mix
- `compare_audio_to_intent(...)`
- smoke coverage in `scripts/mcp_bridge_smoke.py`

This is the first point where the system is genuinely useful for album work.

### Phase 4 — Optional runtime support for isolated long-window capture

Only do this if the workflow truly needs per-operator musical evaluation.

If we do it, add a dedicated runtime primitive whose contract is explicit about:

- whether capture is post-mix, pre-mix, or solo-isolated
- whether it mutates session state
- how it behaves with upstream dependencies
- how it interacts with live playback and automation

Do not hide this behind `analyze_output(...)`.

### Phase 5 — Advisory checks

- add music-aware check types as warnings first
- keep them out of critical CI paths by default
- revisit stronger gating only after structured-output stability is proven

## Risks

### Latency

Inference is slow enough that blocking MCP calls are acceptable for v1 but should be treated as reflective operations, not rapid inner-loop controls.

### GPU contention

Running the model on the same GPU as Vivid may cause visible stutter. The architecture should preserve these deployment options:

- same machine, same GPU
- same machine, different GPU
- CPU fallback
- remote inference host

### Model variance

Music-theory interpretation, genre vocabulary, and semantic match scores will vary by backend. That is acceptable for critique and iteration, but dangerous for hard regression gates.

### Session coupling

If the service remains a singleton with mutable backend configuration, one client can affect another. That is manageable only if documented clearly and surfaced honestly in the tools.

## Recommendation

Proceed with the music-eval feature, but only on the narrower architecture above:

- raw audio capture, not analysis-endpoint overloading
- sidecar inference service, not bridge-local models
- final-mix live capture first
- file/reference workflows first
- advisory checks first

That gives Vivid a strong reflective music-analysis path without forcing the runtime, the bridge, or the checks system into contracts they are not ready to support yet.
