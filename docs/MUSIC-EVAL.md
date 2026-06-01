# Music Evaluation — Specification

Canonical reference for the `music_eval` tool family and inference service. Both `mcp/vivid_mcp.py` and `services/music_eval/` are written against this document. Neither side should define protocol shapes independently.

## Overview

The music eval surface adds a semantic, music-aware perception layer on top of Vivid's existing quantitative audio tools. It routes LALM inference through a persistent sidecar service (`services/music_eval/`) rather than loading a model into the MCP bridge process.

> **The stub backend is the default.** Until `configure_music_eval_backend` selects a
> real backend, `evaluate_audio_musically` and friends return canned placeholder values
> — convincing-looking but not derived from the audio. Enable a real backend before
> trusting any musical judgment, and treat the detected key *letter* as unreliable even
> then. See §1.6.

```
LLM client (Claude Code, Cursor)
        │  stdio (MCP)
        ▼
mcp/vivid_mcp.py  ──── music_eval tool family
        │  HTTP (9876)           │  HTTP (9877)
        ▼                        ▼
Vivid control server       music_eval service
   (running graph)         (loaded LALM backend)
```

**Service default port:** 9877, configurable via `VIVID_MUSIC_EVAL_PORT` env var.  
**Bridge target URL:** `http://127.0.0.1:9877`, configurable via `VIVID_MUSIC_EVAL_URL` env var.

---

## 1. MCP Tool Surface

### 1.1 Common Conventions

All tools follow the existing perception layer conventions from `mcp/vivid_mcp.py`:

- Return type is always `str` (JSON-serialized)
- All tools are `async def` with `@mcp.tool()` decorator
- `include_payload: bool = False` — compact response by default; full payload on request
- Error envelope: `{ "ok": false, "error": { "code": "<string>", "message": "<string>" } }`
- Success envelope always includes `"ok": true`
- When the service is unreachable: `{ "ok": false, "error": { "code": "service_unavailable", "message": "music_eval service not reachable at <url>" } }`

**Compact vs. full payload:** Compact responses strip the raw model output and timing data. Full responses (`include_payload=true`) add `"payload"` with `model_response`, `cot_trace` (when applicable), `timing_ms`, and per-axis scores. This mirrors how `analyze_output` handles its `include_payload` parameter.

---

### 1.2 `evaluate_audio_musically`

```
evaluate_audio_musically(
    window_seconds: float = 30.0,
    node_id: str = "",
    mode: str = "caption",
    include_payload: bool = False
) -> str
```

Captures a window of live audio output and returns a music-aware analysis.

**Parameters:**
- `window_seconds` — capture duration. Capped at 60s for live capture (ring buffer limit); use `evaluate_audio_file` for longer material.
- `node_id` — reserved for future isolated capture support. Must be empty for now; live v1 evaluates the final mix only.
- `mode` — analysis depth:
  - `"caption"` — short structured description: key, tempo, instrumentation, mood. Fastest.
  - `"theory"` — theory-aware analysis: harmony, voice leading, rhythmic structure, form.
  - `"reasoning"` — full chain-of-thought via `<think>` tags before the answer. Slowest (uses CoT post-training where available).

**Compact response:**
```json
{
  "ok": true,
  "key": "F minor",
  "tempo_bpm": 124.0,
  "summary": "Sparse kick-driven arrangement with a stacked minor seventh chord...",
  "mode": "caption"
}
```
`key` and `tempo_bpm` may be `null` if the model cannot determine them (e.g., atonal material).

**Full payload** (additional fields under `"payload"`):
```json
{
  "payload": {
    "model_response": "<full raw model text>",
    "cot_trace": "<think>...</think>",
    "timing_ms": 4820,
    "backend": "music_flamingo",
    "window_seconds": 30.0,
    "node_id": ""
  }
}
```

---

### 1.3 `compare_audio_to_intent`

```
compare_audio_to_intent(
    reference_path: str = "",
    intent: str = "",
    window_seconds: float = 30.0,
    node_id: str = "",
    include_payload: bool = False
) -> str
```

Compares a captured window of current audio to a reference clip, a free-text intent description, or both. At least one of `reference_path` or `intent` must be non-empty; passing both combines reference matching with intent alignment.

**Parameters:**
- `reference_path` — absolute path to a reference audio file (`.wav`, `.aiff`, `.mp3`, `.flac`). Optional.
- `intent` — free-text description of the intended sound. Optional.
- `window_seconds` — capture duration for the current audio. Capped at 60s for live capture.
- `node_id` — reserved for future isolated capture support. Must be empty for now; live v1 evaluates the final mix only.

**Compact response:**
```json
{
  "ok": true,
  "match_score": 0.73,
  "key_deviations": [
    "Reference is in D minor; current output is in F minor",
    "Tempo is slower by ~8 BPM"
  ],
  "summary": "Harmonic character largely matches the intent; rhythm is sparser than reference."
}
```
`match_score` is 0–1 (higher = closer match). `key_deviations` is an ordered list, most important first.

**Full payload** (additional fields under `"payload"`):
```json
{
  "payload": {
    "harmony_match": 0.81,
    "rhythm_match": 0.65,
    "timbre_match": 0.74,
    "structure_match": 0.70,
    "model_response": "<full raw model text>",
    "timing_ms": 7340,
    "backend": "music_flamingo",
    "had_reference": true,
    "had_intent": false
  }
}
```

---

### 1.4 `evaluate_audio_file`

```
evaluate_audio_file(
    path: str,
    start_seconds: float = 0.0,
    duration_seconds: float = 30.0,
    mode: str = "caption",
    include_payload: bool = False
) -> str
```

Same analysis as `evaluate_audio_musically` but reads from a file rather than capturing live. Supports rendered exports, reference tracks, and stems.

**Parameters:**
- `path` — absolute path to audio file (`.wav`, `.aiff`, `.mp3`, `.flac`).
- `start_seconds` — start offset within the file.
- `duration_seconds` — how much to analyze from `start_seconds`. Music Flamingo supports up to 20 minutes; practically, 30–120s is the useful range.
- `mode` — same as `evaluate_audio_musically`.

**Compact response:** Same shape as `evaluate_audio_musically` compact.

**Full payload:** Same shape as `evaluate_audio_musically` full, with `path`, `start_seconds`, and `duration_seconds` added under `"payload"`.

---

### 1.5 `music_eval_status`

```
music_eval_status() -> str
```

Returns service reachability and backend state. Mirrors `runtime_status()` shape.

**Response (service reachable):**
```json
{
  "ok": true,
  "reachable": true,
  "bridge_managed": true,
  "pid": 12345,
  "backend": "music_flamingo",
  "model": "nvidia/music-flamingo-hf",
  "device": "cuda:0",
  "ready": true,
  "queue_depth": 0
}
```
`bridge_managed` is `true` when the MCP bridge launched the service process. `pid` is the service process ID when bridge-managed, otherwise `null`.

**Response (service unreachable):**
```json
{
  "ok": false,
  "reachable": false,
  "error": {
    "code": "service_unavailable",
    "message": "music_eval service not reachable at http://127.0.0.1:9877"
  }
}
```

**`ready` semantics:** `true` when the backend is loaded and no inference is running. `false` during model load or mid-inference (queue_depth > 0 implies busy but technically ready after current job).

---

### 1.6 `configure_music_eval_backend`

```
configure_music_eval_backend(
    backend: str,
    model_path: str = "",
    api_key: str = "",
    device: str = "auto",
    accept_license: bool = False
) -> str
```

Switches the inference backend. The service handles loading/unloading; the bridge forwards the request.

**Parameters:**
- `backend` — one of: `"music_flamingo"` or `"stub"`.
- `model_path` — local path or HF repo ID override. Empty uses the default for the backend.
- `api_key` — reserved for future API-backed backends. Ignored for current backends.
- `device` — `"auto"`, `"cuda:0"`, `"mps"`, `"cpu"`. `"auto"` applies CUDA → MPS → CPU priority.

**Success response:**
```json
{
  "ok": true,
  "backend": "music_flamingo",
  "model": "nvidia/music-flamingo-hf",
  "device": "cuda:0",
  "message": "Backend switched. Model will load on first inference request."
}
```

**License-required error** (acceptance not yet recorded):
```json
{
  "ok": false,
  "error": {
    "code": "license_required",
    "license": "CC-BY-NC-4.0",
    "message": "Music Flamingo is non-commercial. Review the license before proceeding.",
    "url": "https://huggingface.co/nvidia/music-flamingo-hf",
    "accept_param": "accept_license=true"
  }
}
```
Re-call with `accept_license=true` appended to the body (bridge surfaces this as a note in the error message) to record acceptance and proceed.

---

## 2. Service API

### 2.1 `POST /v1/evaluate`

Request body:
```json
{
  "audio_b64": "<base64-encoded WAV>",
  "audio_path": "",
  "sample_rate": 48000,
  "channels": 2,
  "mode": "caption",
  "prompt": "",
  "start_seconds": 0.0,
  "duration_seconds": null
}
```

- `audio_b64` — float32 WAV, RFC 4648 base64. Mutually exclusive with `audio_path`; `audio_b64` takes precedence when both are set.
- `audio_path` — absolute path to an audio file on the service host. Used by `evaluate_audio_file`; empty for live captures.
- `sample_rate` — source sample rate (Vivid always produces 48000).
- `channels` — channel count (Vivid final mix is always 2).
- `mode` — `"caption"`, `"theory"`, or `"reasoning"`.
- `prompt` — optional free-text hint injected into the model prompt. Empty string means no hint.
- `start_seconds` — start offset when loading from `audio_path`. Ignored for `audio_b64`.
- `duration_seconds` — how many seconds to load from `start_seconds`. `null` means read to end of file. Ignored for `audio_b64`.

Response (success):
```json
{
  "ok": true,
  "key": "F minor",
  "tempo_bpm": 124.0,
  "summary": "...",
  "mode": "caption",
  "model_response": "...",
  "cot_trace": "",
  "timing_ms": 4820,
  "backend": "music_flamingo"
}
```

Response (error):
```json
{
  "ok": false,
  "error": { "code": "model_not_loaded", "message": "..." }
}
```

---

### 2.2 `POST /v1/compare`

Request body:
```json
{
  "audio_a_b64": "<base64-encoded WAV>",
  "audio_b_b64": "<base64-encoded WAV>",
  "intent": "",
  "sample_rate": 48000,
  "channels": 2
}
```

- `audio_b_b64` — may be empty string when comparing against intent only (no reference clip).
- `intent` — free-text description. May be empty when comparing two concrete clips.

Response (success):
```json
{
  "ok": true,
  "match_score": 0.73,
  "key_deviations": ["..."],
  "summary": "...",
  "harmony_match": 0.81,
  "rhythm_match": 0.65,
  "timbre_match": 0.74,
  "structure_match": 0.70,
  "model_response": "...",
  "timing_ms": 7340,
  "backend": "music_flamingo"
}
```

---

### 2.3 `GET /v1/status`

No request body.

Response:
```json
{
  "backend": "music_flamingo",
  "model": "nvidia/music-flamingo-hf",
  "device": "cuda:0",
  "ready": true,
  "queue_depth": 0
}
```

---

### 2.4 `POST /v1/configure`

Request body:
```json
{
  "backend": "music_flamingo",
  "model_path": "",
  "api_key": "",
  "device": "auto",
  "accept_license": false
}
```

Response: same as `configure_music_eval_backend` success/error shapes above, with the `accept_param` hint omitted (that's a bridge-layer affordance for MCP callers).

---

## 3. Audio Wire Format

Vivid produces 48 kHz stereo float32 PCM. The service receives this as a base64-encoded WAV and owns all resampling to the backend's required format:

| Backend | Required rate | Required channels | Notes |
|---------|--------------|-------------------|-------|
| Music Flamingo | 16 kHz | mono | NVIDIA AF-family standard |
| Stub | passthrough | passthrough | Deterministic development/test backend |

**WAV format:** RIFF container, IEEE float32 PCM (format tag 3), little-endian. Same encoding produced by `encode_wav_float32()` in `capture_coordinator.cpp`.

**Base64:** RFC 4648 standard alphabet, no line breaks.

**Size limits:** A 30-second stereo 48 kHz float32 WAV is approximately 11.5 MB uncompressed, ~15.4 MB base64-encoded. HTTP bodies up to 100 MB are expected; the service should not impose a lower limit.

Live capture is capped at 60 seconds. For longer material, use `evaluate_audio_file(...)`.

---

## 4. Error Codes

| Code | Meaning |
|------|---------|
| `service_unavailable` | Service not reachable (bridge-side, before HTTP) |
| `license_required` | NC model requested without license acceptance |
| `model_not_loaded` | Backend configured but model not yet loaded |
| `oom` | CUDA out-of-memory during inference |
| `invalid_mode` | Unknown `mode` value |
| `invalid_backend` | Unknown `backend` value |
| `invalid_window` | Requested live/file analysis window is invalid |
| `unsupported_feature` | Requested capability is not implemented by the current live-capture path |
| `audio_too_short` | Captured buffer is shorter than minimum required |
| `audio_decode_error` | WAV decode failed |
| `inference_error` | Model threw an exception during forward pass |
| `no_audio_source` | `audio_b64` and `audio_path` both missing |
| `reference_not_found` | `reference_path` does not exist or is not readable |

---

## 5. License Gating

Music Flamingo carries a CC-BY-NC-4.0 license. Commercial use of evaluations produced by this model is in murky territory.

- First call to `/v1/evaluate` or `/v1/compare` with Music Flamingo as the active backend returns `license_required` without running inference.
- Acceptance is recorded to `~/.config/vivid/music_eval_license.json` (created by the service, not the bridge).
- Once recorded, subsequent calls proceed without the gate check.
- `"stub"` is not gated.
- Only `"music_flamingo"` is gated in v1. Future backends may add their own gates.

---

## 6. Deferred Surfaces

These surfaces are intentionally not part of the current v1 contract:

- live `compare_audio_versions(...)`
- music-aware `run_checks(...)` / `validate_checks(...)` integration
- long-window isolated `node_id` capture

They may return in a later phase once the runtime has a cleaner isolated-capture primitive and the product contract is less ambiguous for reflective model output.
