# ADR-0026: Evaluation Asserts Quality Against Intent, Not Liveness

Status: proposed (2026-07-20)

## Context

Building the showcase demos exposed a hole in how we verify creative output. The demos were
declared "verified — full arrangements" on the strength of these checks, run at every step:

- `analyze_audio` → **level > 0** ("it sounds")
- `summarize_visual_output` → **brightness > 0**, an activity number, `"MOVING"` ("it renders")
- `get_audio_graph` → the intended nodes exist ("structure is correct")
- a few **static `capture_frame` glances**

Every one of those is a **liveness / smoke check** — *something is happening without erroring*.
None measures whether the music is good, whether a part declared a "lead" has a melody, whether the
arrangement is full, or whether a visual's reactivity actually reads. The gap shipped real defects:
`signal`'s bass was the root note struck sixteen times — a monotone drone — and it passed as a
"verified arrangement" because a level number cannot tell a drone from a bassline. The failure was
**treating "produces output" as "is good,"** and reporting confidence that the checks had not earned.

The tooling makes this easy to fall into, in two ways:

1. **The trunk has no semantic audio evaluation.** It has quantitative tools (`analyze_audio`
   level/3-band/transient, `analyze_spectrum`, `analyze_audio_file`) and — from ADR-0024 — a decent
   *visual* perception suite (`analyze_visual_motion`, `compare_variations`, `explain_tradeoffs`,
   `analyze_frame`, `compare_frames`). But **`vivid-classic`'s `music_eval` layer — a LALM that
   actually listens — was never ported.** So a level number is genuinely the best audio signal the
   trunk offers.
2. **`vivid-classic` already solved this and encodes the discipline we lacked.** Its `music_eval`
   family (see `git show vivid-classic:docs/MUSIC-EVAL.md`) routes live output through an audio LLM
   and returns real musical content — and it ships a guardrail aimed straight at our failure: *"the
   stub backend returns convincing-looking values not derived from the audio — don't trust musical
   judgment without a real backend."* It also has a graded **LLM-MCP eval harness**
   (`scripts/llm_mcp_eval/`) wired into CI. The trunk even carries a **dangling `llm-mcp-evals.yml`
   workflow with no harness behind it.**

We benchmark against classic (ADR-0011). We adopt its evaluation loop — the tool *shapes*, the
intent-comparison, and the stub-is-fake guardrail — but change two things classic settled differently:
the **backend is Google Gemini** (a hosted, audio-capable LLM — currently the only viable one, and
hosted so no local GPU / model weights / non-commercial license are needed), and evaluation runs
**inside the Vivid app** rather than as an external sidecar loading a local model.

## Decision

**An artifact is "verified" only when a real evaluator asserts it satisfies an explicit, declared
intent. Liveness metrics are necessary preconditions, never the verdict. When no real evaluator is
available, the artifact is "unverified" — we fail closed and say so.**

Concretely:

### 1. Every authored artifact declares an explicit, machine-readable INTENT

No demo (and no MCP-authoring task we want to grade) is evaluable without a declared intent. The
intent is the artifact's own thesis, structured:

```
intent:
  genre: "glitch / IDM"          # free text the LALM can match against
  tempo_bpm: 90
  voices:                        # each declared part + its ROLE
    - {role: drums,  desc: "shredded 808 kit"}
    - {role: bass,   desc: "rolling acid bassline, melodic"}
    - {role: stabs,  desc: "off-beat minor chord stabs"}
  mood: "dark, mechanical, urgent"
  visual: {elements: [ShapeGrid, chromatic-split, feedback], reactivity: "tears on the beat"}
```

The loop asserts the output *against this*, not against "not silent."

### 2. The loop asserts in three tiers; passing a lower tier only licenses the next

**Tier 0 — Liveness (gate, not verdict).** Necessary preconditions that let the real evaluators run
at all: audio not silent (level over floor), output not black and fed to `Output`, intended
nodes/edges/mappings present, no MCP errors, MCP↔control parity holds. **Passing Tier 0 is not
"verified" and must never be reported as such.**

**Tier 1 — Content (does the artifact contain what the intent names).** Deterministic, from the
document itself — no model needed:

- **Audio:** read the actual clip/generator notes (`get_clip` / generator params). Assert a part
  whose role is `lead`/`bass`/`melody` has **pitch variety over time** (≥ K distinct pitches, real
  motion) — a held or repeated single pitch **fails** unless the intent says `role: drone`. Assert
  the number of concurrently sounding voices matches the intent (no "arrangement" with one real part).
- **Visual:** assert each declared visual element is present, and that `analyze_visual_motion` shows
  motion that **varies over the window** (reactivity), not merely brightness > 0.

**Tier 2 — Semantic / intent match (the real judgment; via the LALM).** Ported from classic:

- `evaluate_audio_musically` (or `evaluate_audio_file` on a rendered bounce) returns detected key,
  tempo, **instrumentation**, and mood. The loop asserts these align with the intent — intent says
  "melodic lead" → the model must report a lead/melody; intent says techno 128 → detected tempo ≈ 128.
- `compare_audio_to_intent(intent=<the declared genre + description>)` returns a `match_score` with
  `harmony` / `rhythm` / `timbre` / `structure` sub-scores and ranked deviations. The loop asserts
  `match_score ≥ threshold` and surfaces the deviations. **This is the assertion that would have
  caught the drone:** a "melodic acid bass" intent vs. a monotone root scores low on harmony/structure
  and names the deviation.

### 3. Guardrails (the discipline, made mechanical)

- **Fail closed.** If no Gemini key is configured, or Gemini is unreachable, or the backend is the
  stub (`music_eval_status` → not `ready`), the audio Tier-2 assertion is **UNMET**, and the verdict
  is `unverified (no evaluator)` — never `verified`. A convincing-looking stub response is treated as
  no response.
- **Liveness is never a stand-in.** A Tier-0 proxy may not satisfy a Tier-1/2 assertion. "Sounds" /
  "renders" / "verified" may be reported only for the tier actually passed.
- **Honest verdicts.** The loop (and anyone reporting from it) states the **tier reached and what was
  not asserted** — e.g. "Tier-1 pass (has a real bassline); Tier-2 not run (no LALM backend)" — not a
  blanket "verified."
- **Intent is mandatory.** No declared intent → the artifact cannot reach Tier 2 and cannot be
  "verified."

### 4. What we build — evaluation is a first-class in-app capability, Gemini-backed

Unlike classic (an external sidecar loading a local audio LLM), evaluation runs **inside the Vivid
app**, backed by **Google Gemini** — hosted, so it needs only a user-supplied API key, no GPU/model:

1. **In-app key entry + evaluation (the headline).** A settings surface lets the user paste a
   **Gemini API key**, stored securely (Keychain / app config, never in a project file). The app
   captures its own audio output (the transport capture ring buffer already backing `analyze_audio`),
   packages it with the declared intent, calls the Gemini API directly (audio in), and **surfaces the
   result in the app UI** — detected key/tempo/instrumentation/mood, the intent `match_score` +
   harmony/rhythm/timbre/structure sub-scores, and ranked deviations. Vivid can grade its own music.
2. **The same path drives the agent + CI.** The `music_eval` MCP tools (`evaluate_audio_musically`,
   `compare_audio_to_intent`, `evaluate_audio_file`, `music_eval_status`,
   `configure_music_eval_backend`) map onto that in-app Gemini call — same code, same key — so the
   tiered loop and CI grade exactly what the user sees. We keep classic's tool *shapes* and its
   stub-is-fake guardrail (`configure_music_eval_backend(backend="gemini"|"stub")`,
   `music_eval_status` reporting reachability/readiness); parity-matched in `mcp/vivid_mcp.py`
   ([[reference_mcp_parity_guard]]).
3. **A render-to-file bounce** so Tier 2 can run offline on a rendered `.wav` — for material longer
   than the live-capture window and because, as ADR-0025's demo work showed, headless live audio is
   fragile.
4. **An `intent` block** on every demo + a small schema, and the **tiered assertion loop** that emits
   a per-tier verdict, failing closed.
5. **Revive `scripts/llm_mcp_eval/`** (the CI workflow already expects it) with classic's deterministic
   graders, plus a **demo-authoring case** whose grader asserts the produced graph clears Tiers 1–2.

## Consequences

- **We can no longer report "verified" from level/brightness.** The bar for that word becomes a passed
  Tier-1 (content) or Tier-2 (intent-match) assertion; otherwise the honest verdict is "unverified" or
  "Tier-N only."
- **Every demo needs an intent**, and a full grade needs a **Gemini key** — a per-user secret, a
  network round-trip, and a per-eval API cost. Tier 2 in CI needs a CI-held key (or is skipped, which
  fails closed → "unverified", not a false pass). The loop is slower and heavier than a curl to
  `analyze_audio`; that is the price of a judgment that means something.
- **Evaluation becomes a user-facing product feature**, not just an agent tool: any user with a key
  can have Vivid critique its own arrangement against an intent — the same capability the eval loop
  uses. No local model, GPU, or license; it runs anywhere the app runs.
- **The dangling eval CI gets a real harness**, and demo quality becomes a regression net rather than a
  one-shot vibe.
- The trunk closes a real gap against the classic benchmark: the *visual* half of semantic evaluation
  exists (ADR-0024); this ADR restores the *audio* half — re-backed on Gemini and pulled in-app — and
  the honesty rules around both.

## Alternatives Considered

- **Keep quantitative-only + human audition.** Doesn't scale, isn't in the automated loop, and the
  authoring agent cannot hear — the exact conditions that produced the drone.
- **LLM-judge over the transcript only** (grade what the agent *said* it did). Misses the actual
  artifact; an agent can narrate a great arrangement it did not produce. Classic deliberately grades
  the *audio* (LALM) and the *transcript* (deterministic), not an LLM vibe-check.
- **Local audio-LLM sidecar (classic's Music Flamingo).** The mature design, but it needs a GPU,
  multi-GB weights, and a non-commercial license, and it lives in a separate service — it can't be a
  one-key in-app feature that runs on any user's machine. Hosted Gemini (key only, audio-capable) can.
  We keep classic's tool interface and discipline, not its backend or deployment.
- **Trust the stub / trust proxies.** The status quo that shipped the drone. Rejected on its face — it
  is the failure this ADR exists to prevent.
