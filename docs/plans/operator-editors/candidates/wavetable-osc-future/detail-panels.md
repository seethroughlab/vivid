# Detail panels for deferred clusters

## What they are

Three param clusters from the operator's 27-param surface live today in the default inspector (post-v1 retirement of inspector-grid widgets). Each could — eventually — get its own editor sub-panel for richer visualization, but none is cramped enough in the default list to justify an afternoon's work right now.

Bundled together because they're similar-shaped follow-ups; land individually on demand.

## Phase cluster

**Params**: `phase_reset_mode`, `start_phase`, `phase_random`, `stereo_phase_offset`.

**Why a detail panel could help**: the four params interact — `phase_reset_mode` governs whether `start_phase` and `phase_random` are used at all, and `stereo_phase_offset` only makes sense relative to them. A small clock-face-style visualization (circles around a phase wheel, one per voice position) would make the relationships legible.

**Scope**: ~2 hours. New panel region (~220px wide, 160px tall) with a phase-wheel clock visualization + 4 sliders. Click on the clock to set `start_phase` directly.

**Recommended trigger**: ship when someone asks "what does phase_reset_mode do?" If no one asks, it's fine in the inspector.

## Drift cluster

**Params**: `drift_amount`, `drift_rate_hz`.

**Why a detail panel could help**: honestly, it probably doesn't. Two params, simple semantics. A small dedicated panel would mostly just be two knobs with a slightly nicer visualization (maybe a sine wave showing drift rate + amount).

**Recommended trigger**: skip unless someone demands it. Keep in inspector.

## Interaction cluster

**Params**: `interaction_mode`, `interaction_depth`, `interaction_input_gain`, `interaction_tracking`.

**Why a detail panel could help**: the interaction modes (FM / PM / RM / AM) do dramatically different things. A small "what happens" diagram showing "carrier × modulator → output" for each mode would help users pick the right one without experimenting randomly.

**Scope**: ~3 hours. Mode-specific diagrams (4 of them, one per interaction mode, picked by the current mode selector). `interaction_depth` slider as the primary dial; `input_gain` and `tracking` as secondary.

**Recommended trigger**: ship if interaction modes start getting more exposure in example graphs or presets.

## Common infrastructure

If two or more detail panels ship, the side-panel region probably needs **tabs** (Core | Phase | Drift | Interaction) rather than stacking everything. That tab widget doesn't exist in the toolkit today — would be built for the first such adopter and factored into `editor_ui.h` if a second adopter uses it.

## Interactions

- None meaningful between detail panels themselves.
- All three interact with [audition](audition.md) — these clusters shape the sound, so hearing what they do is the real validation.

## Scope cuts for each panel

Shared across all three:
- **MIDI learn** for cluster params: defer indefinitely.
- **Preset recall** within a cluster: handled by the operator's top-level preset system.
- **A/B compare** within a cluster: useful for sound design but adds UI surface and param state. Defer.

## Test plan

Each detail panel gets its own test file following the established pattern:
- Pure-logic: visualization math (phase wheel positions, interaction-mode diagram coordinates).
- End-to-end: keyboard/mouse flows emit the correct set_param for the panel's cluster.

No integration across panels is needed — they're independent.
