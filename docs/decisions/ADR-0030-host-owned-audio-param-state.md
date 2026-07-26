# ADR-0030: Host-Owned Audio Param State

Status: accepted

Date: 2026-07-26

Implementation: Phase 1 (host-owned base cache for plugin nodes — decision points 1, 2, 4, 6 for
plugins) landed on branch `adr-0030-plugin-host-base`. Phase 2 (non-destructive bridge delivery,
decision point 5) and Phase 3 (the follow-up test matrix + TSan coverage) are tracked separately.

Extends [ADR-0022](ADR-0022-session-audio-graph.md), [ADR-0028](ADR-0028-one-source-id-language.md),
and [ADR-0017](ADR-0017-every-edit-is-reversible.md).

## Context

ADR-0022 settled the right param model for modulation: every drivable param has a **base** value
owned by the user and a **resolved** value heard by DSP. The base is what the knob, persistence, and
undo read. The resolved value is base plus live modulation, computed at the point of use.

That model is correct for native audio operators because Vivid owns the base in `AudioOp::pvals`.
It is not yet complete for plugin nodes. VST3 and CLAP params are currently read from plugin-owned
state, so the host cannot reliably answer "what did the user author?" separately from "what is the
plugin currently hearing?" ADR-0022 names this as the P2 prerequisite for safe plugin modulation.

There is a second, related gap in the audio-visual bridge. Frame-side mappings that target audio
params currently deliver values by calling the normal param setter every frame. That writes the
authored base. Disconnecting the mapping has nothing to restore; an unrelated undo snapshot can
capture a value the user did not author.

Both problems are one ownership problem: **a host surface that can automate or modulate a param must
not mutate the authored base unless the user explicitly edits the base.**

## Decision

Make Vivid the owner of authored audio param state for every audio-graph node family: native, VST3,
CLAP, and sampler.

1. **Every graph-node param has host-owned base state.** Native ops keep `pvals`. Plugin nodes gain
   a host-side base cache keyed by stable node identity plus plugin param id. Loading a plugin seeds
   the cache from restored project data when present, otherwise from the plugin's current value.

2. **Setters mean authored base.** `session_audio_graph_node_param_set` and the session-global
   `session_graph_node_param_set` update the host base. They also enqueue delivery to the plugin,
   because a user-authored base edit should be heard.

3. **Resolved values are delivered as overrides.** Modulation and bridge mappings produce per-block
   or frame-published overrides. They do not write host base state and they do not overwrite plugin
   state as the document's authored value.

4. **Persistence and undo save base only.** Project JSON, undo projection, and MCP "base" fields
   serialize the host base. Plugin binary state remains the plugin patch; it must not be used as the
   only source of truth for Vivid-authored automation/modulation values.

5. **The bridge gets a non-destructive delivery path.** `apply_audio_param_mappings` keeps today's
   mapping math but changes delivery: frame-side mapping output is published to an audio-thread-safe
   override channel, then consumed when the target node processes. Disconnecting a mapping removes
   the override and the knob returns to the host base.

6. **MCP exposes base, resolved, and wired consistently.** Audio graph param JSON mirrors the visual
   graph's established split: `base` is authored, `value` is resolved, `wired` says whether a live
   source affects it.

## Alternatives Considered

- **Let plugin state remain authoritative.** Rejected. It cannot distinguish authored values from
  automation/modulation delivery, and it makes save/undo vulnerable to snapshot poisoning.
- **Use plugin automation APIs as the document model.** Rejected. VST3/CLAP automation delivery is
  the right transport to the plugin, not the right place to store Vivid's authored base.
- **Leave bridge mappings destructive and document the caveat.** Rejected. A professional tool
  cannot let a mapping silently rewrite the user's patch.

## Consequences

- **Positive:** Plugin modulation becomes safe to build; mapped audio params stop destroying knob
  values; undo and save remain stable under moving modulation.
- **Positive:** Native, VST3, CLAP, and sampler nodes share one param contract, which simplifies UI,
  MCP, persistence, and tests.
- **Tradeoff:** The host now has to reconcile plugin UI edits, preset loads, and plugin state restore
  back into the base cache. That reconciliation must be explicit and tested.
- **Follow-up:** Add tests for plugin-node base persistence, bridge disconnect restoring base, undo
  under a moving modulator, and save/load while mapped or modulated.

