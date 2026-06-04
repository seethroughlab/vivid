# Plan — Dynamic modulatable plugin-param targets

*(Formerly "plugin-param 8-slot macro ceiling.")*

## Context

A node hosting a VST3/CLAP/AU plugin can have at most **8** of its plugin parameters driven by modulation (LFO, envelope, sequencer, wired control signal, or continuous MCP modulation) at once. This is a real, current ceiling.

### Correction to an earlier reading
An earlier investigation concluded this was "already resolved" by the MCP direct-param queue. **That was wrong.** Two distinct mechanisms exist:

- `macro_0..macro_7` are `Param<float>` (`operators/audio/vst3_instrument/vst3_instrument.cpp:45–60`) — i.e. **wire/modulation-drivable** numeric params, each mapped to a plugin param by name via `macro_N_id`, backed by `MacroEntry macro_map_[8]` (line 114). An operator can modulate at most 8 plugin params.
- `_vst3_direct_params` (`vst3_instrument.cpp:67`) is a **string** param feeding `DirectParamQueue`. It handles *static one-shot* MCP sets with no ceiling — but a string param is **not** a modulation target.

So the direct queue removed the cap on *static value sets only*. The deferred-work ceiling — simultaneous *modulatable* plugin params — is still 8, identical across VST3/CLAP/AU.

We are free to break ABI / drop backwards compatibility.

## Correct outcome
Remove the fixed-8 limit so any number of plugin parameters can be modulation/wire destinations on a single plugin node. Static MCP `set_*_param` (direct queue) is orthogonal and stays as-is.

## Chosen design — Option B: variadic macro repeat-group

Replace the fixed 8 macros with a **repeat-group** of macro ports that grows on demand, reusing `VividPortDescriptor`'s existing `repeat_group` / `repeat_group_idx` fields (`src/operator_api/types.h:148–150`) — the established variadic-port pattern. This keeps the "macro = named indirection" model (a labeled, modulatable knob mapped to a plugin param by name — a useful UI affordance) while removing the hard cap, and it leaves the modulation core untouched.

*(Rejected alternative — Option A: delete macros and make plugin params first-class modulation destinations directly. Cleaner conceptually but requires extending the modulation/destination system to accept a dynamic, operator-supplied destination set. Chose B for correctness with least blast radius and to preserve the named-macro UX.)*

### Implementation
Apply symmetrically to `operators/audio/vst3_instrument/vst3_instrument.cpp`, `operators/audio/clap_instrument/clap_instrument.cpp`, `operators/audio/au_instrument/au_instrument.cpp`:

- Replace the 8 hardcoded `macro_N` / `macro_N_id` `Param` pairs and `MacroEntry macro_map_[8]` with a **dynamic `std::vector<MacroEntry>`** plus a variadic macro port group. Each assigned/wired macro allocates the next index.
- `collect_params()` / `collect_ports()` emit the macro pair for each active slot plus one trailing empty slot to accept the next assignment (standard repeat-group behavior). The `_id` text param continues to carry the plugin-param name; resolution to the plugin param ID is unchanged.
- Audio thread: the per-macro event build (`build_macro_events`) iterates the dynamic vector instead of `[8]`; keep the per-entry atomics model. **Pre-size the vector on the main thread** (in the sync / `update_macro_map` path) so the audio thread never allocates.
- Inspector: macro UI lists active slots + an "add" affordance; drop the fixed 8-row layout.
- `_vst3_direct_params` / `DirectParamQueue` stays as-is for static MCP sets.

### Real-time safety
The audio callback must never allocate or lock. All growth of `macro_map_` happens on the main thread during param sync; the audio thread only reads the current size and per-entry atomics. Verify no path grows the vector from the audio callback.

## Files
`operators/audio/vst3_instrument/vst3_instrument.cpp`, `operators/audio/clap_instrument/clap_instrument.cpp`, `operators/audio/au_instrument/au_instrument.cpp`; possibly the shared `operators/shared/plugin_common/` helpers; inspector UI for the macro section.

## Verification
1. Build the three instrument dylibs (background).
2. Drive >8 distinct plugin params on one node simultaneously via mod assignments / wires (multiple LFOs, envelopes); `inspect_node` + `list_mod_destinations` show all targets.
3. Audio capture confirms each modulated param moves independently and continuously (not just one-shot).
4. Static MCP `set_vst3_param` still works unchanged.
5. `detect_dropouts` under load → no new xruns from macro-vector growth.
