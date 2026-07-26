# ADR-0034: Modulation Reaches Plugin Params

Status: accepted

Date: 2026-07-26

Implementation:
- Phase 1 (#159) — a complete CLAP vertical slice: atomic base mirror + capture-on-wire + CLAP
  resolve/deliver, tested end to end with the in-tree `vivid_test_clap` fixture and TSan-gated.
- Phase 2 — VST3 delivery. Investigating it surfaced a pre-existing bug: the VST3 process-time param
  path (`Vst3ParamChanges`) never carried values — `drain_params` used `addParameterData` +
  `addPoint`, but `SinglePointQueue::addPoint` is a no-op and the value-setting `add()` was unused, so
  the queue held only the param id. VST3 knob edits worked only via `setParamNormalized` (which drives
  the DSP directly on single-component/JUCE plugins). Verified against a real plugin (CHOWTapeModel):
  with `setParamNormalized` disabled, no param reached the DSP until `drain_params` was fixed to use
  `pc.add(id, value)`. That fix is a general VST3 automation-correctness win and the prerequisite for
  modulation, which is delivered by injecting the resolved points into `Vst3ParamChanges` after the
  drain (raising `kMaxParams` 8 → 64). Confirmed end to end: an LFO on a VST3 param swings the output.
  No VST3 fixture (SDK lacks `public.sdk`), so CI covers the base mirror via `test_plugin_param_base`;
  the delivery is manually verified.
- Phase 3 hardens precedence and restore-on-disconnect across both formats.

Extends [ADR-0022](ADR-0022-session-audio-graph.md), [ADR-0030](ADR-0030-host-owned-audio-param-state.md),
and [ADR-0029](ADR-0029-concurrency-model-is-tsan-gated.md).

## Context

ADR-0022 made control edges a first-class signal: a modulator (LFO, envelope) publishes a 0..1
control value, and a driven param's effective value is `control_resolve(base, source)`, computed at
the point of use while the authored base is never written. That works end to end for native ops —
`resolve_control_inputs` reads the op's atomic `pvals` on the audio thread and hands the resolved
value to `audio_op_process` as an override.

It does nothing for plugin nodes. `resolve_control_inputs` and the cross-track control block bail when
`nb.op` is null, so VST3 and CLAP nodes never see modulation. ADR-0022 named this the P2 gap and
ADR-0030 built its prerequisite — a host-owned base the host can resolve against — but stopped short
of delivering resolved values to plugins on the audio thread.

The rest of the machinery is already in place. A user can draw a control edge onto a plugin param
today; it persists and compiles into the plan's `control_in` for the plugin node. Each plugin format
already has a per-block param-delivery path the audio thread uses (`drain_params` → VST3
`IParameterChanges`; `clap_flush_params` → CLAP events). The one thing missing is a base the audio
thread can read: the plugin base cache from ADR-0030 (`host_base`/`has_base`) is main-thread-only,
non-atomic memory, so the audio thread cannot resolve against it.

## Decision

Deliver control-edge modulation to plugin params on the audio thread, reusing the compiled plan and
each format's existing per-block param delivery.

1. **Plugin params gain an audio-thread-readable atomic base.** Alongside the main-thread
   `host_base`/`has_base`, each plugin handle holds a fixed-size atomic base (normalized for VST3,
   plain for CLAP), allocated once when params are cached and written wherever the main thread authors
   base (a param set, a plugin-GUI `performEdit`, the bridge's capture-on-first-deliver). The audio
   thread reads it to compute `control_resolve`. This is a new audio↔UI channel and is TSan-gated per
   ADR-0029.

2. **Wiring a modulator captures a base anchor.** Connecting a control edge onto a plugin param that
   has no authored base captures the plugin's current value as the authored base at connect time (on
   the main thread, where reading plugin state is safe). Modulation then has a stable value to swing
   around, and disconnect has a value to restore — the same capture-once pattern ADR-0030's bridge
   delivery uses.

3. **Resolution is un-gated for plugin nodes.** `resolve_control_inputs` and the cross-track control
   block gain plugin branches that read `nb.handle`/`nb.clap`, use the plugin param's range (VST3
   0..1; CLAP `min`/`max`) and the atomic base, and produce a resolved value per driven param —
   `ci.param` already indexes the plugin param table exactly as the setter does.

4. **Delivery reuses each format's per-block param path.** Resolved values are injected right after
   the existing drain — into the VST3 `IParameterChanges` and the CLAP input event list — so the
   plugin hears them for that block. No new transport to the plugin is introduced.

5. **A wired param's modulation is the authoritative per-block value.** Within a block, the resolved
   value for a param driven by a control edge overrides any coincident authored/bridge value arriving
   through the plugin's own param queue. Modulation resolves from the base, which already reflects the
   latest authored set, so this is consistent with the native model where the override sits on top of
   the base.

6. **Removing the last edge restores the base.** When a param's last control edge is disconnected,
   the host delivers the authored base to the plugin once, so it returns to the user's value — the
   mirror of ADR-0030's `_override_clear`.

## Alternatives Considered

- **Resolve plugin base on the audio thread by calling the plugin.** Rejected. VST3
  `getParamNormalized` and CLAP `get_value` are main-thread-only; calling them from the render thread
  violates both specs and the concurrency model.
- **Publish a per-block base snapshot from the frame thread.** Rejected as more machinery than needed:
  an atomic base mirrors how native `pvals` already works and needs no per-frame publish step.
- **Deliver modulation through the existing `param_q` (SPSC) from a frame-side resolver.** Rejected.
  Resolving on the frame thread and pushing per block reintroduces the one-block latency and the
  render-order coupling ADR-0022 removed for native modulation; resolution belongs on the audio
  thread next to the modulator's published output.

## Consequences

- **Positive:** plugin params become modulatable by the same LFOs/envelopes as native params, with no
  new UI — the front-end and persisted plan already support the edges.
- **Positive:** the atomic plugin base is reusable beyond modulation (any future audio-thread reader of
  an authored plugin value).
- **Tradeoff:** a new audio↔UI channel (the atomic base) must be TSan-gated and kept in the
  `thread-safety.md` inventory; the render primitives gain a per-block resolve step for plugin nodes
  that carry control edges (empty and skipped for those that don't).
- **Follow-up:** CLAP delivery is covered end to end by the in-tree `vivid_test_clap` fixture (a
  modulator wired to its gain, asserted after render). VST3 delivery relies on the handle unit test
  plus a manual pass, as ADR-0030's fixture rationale established. Precedence and restore-on-disconnect
  get dedicated tests.
