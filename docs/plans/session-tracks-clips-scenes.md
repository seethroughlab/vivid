# Session, Tracks, Clips, and Scenes

## 1. Mental Model

Vivid is not only an audio tool, and it is not only a visual tool. It is an audiovisual performance environment where sound, image, control, and interaction can all be authored in the same graph. The Session model is the part of Vivid that turns that graph into something playable.

A Session is the performance layer above the graph. It gives names and structure to the parts of a piece: the verse, chorus, bridge, drop, quiet section, build, reset, blackout, or any other moment the artist wants to perform or revisit.

The core model is:

```txt
Session
  Tracks
    Clips
  Scenes
```

A Session contains Tracks. Tracks contain Clips. Scenes launch Clips across Tracks.

This model should be the main way a user thinks about performance authoring in Vivid. Lower-level concepts such as presets and state machines remain useful, but they should not compete with the Session model in the primary workflow. Whole-graph Variations should be removed as a primary product concept.

### Current Problem

Vivid's current performance surface is incoherent because three snapshot mechanisms were built
independently:

```txt
Variations            = whole-graph raw parameter snapshots
Presets               = single-node raw parameter snapshots
State-preset mappings = StateMachine states that recall node presets
```

Each mechanism has a different scope, trigger, UI surface, and mental model. They do not compose into
one clear answer for common performance authoring tasks such as verse -> chorus -> drop. A user must
decide whether a section should be a Variation, a group of node Presets, a StateMachine state, or some
combination of all three.

That confusion is the reason this refactor is necessary. Vivid needs one primary performance model
that can explain section-level changes, per-Track independence, manual overrides, queued launches,
and dirty state without making the user learn raw graph snapshots or state-index plumbing.

### Greenfield Architecture Principle

This plan should be treated as a product architecture reset, not an incremental feature layered on top
of the old performance mechanisms.

Design the Session model as if Vivid were starting from scratch:

```txt
Session
  Tracks
    Clips
  Scenes
```

Existing features are implementation material, not compatibility constraints. Reuse old code only when
it supports the new hierarchy cleanly. Delete old concepts when they keep the product incoherent. Do
not preserve old conceptual seams just because the current implementation has them.

The central question for the primary workflow is:

> How do I perform verse -> chorus -> drop?

The answer must be:

> Launch Scenes that launch Track Clips.

It must not be:

```txt
recall a Variation
force a StateMachine state
bind a target node preset to a state index
```

Those lower-level actions can remain available only when they are still useful as advanced tools. They
should not be equal front doors in the main Session View.

### Session

A Session is the whole playable state of a Vivid graph.

It answers:

> What are the performable parts of this piece, and how do I move between them?

A Session might represent a song, a live audiovisual set, an installation state map, a generative score, a theater cue sheet, or a collection of visual/audio looks for improvisation.

Examples:

```txt
Live Set
Installation Day Cycle
Album Visualizer
Performance Patch
Interactive Wall States
```

The Session View is the surface where these states are organized and launched.

### Track

A Track is an independent performance channel.

The word Track is deliberately broad. It does not mean only an audio track. A Track can represent any responsibility in the performance: audio, visuals, control, interaction, lighting, text, camera, simulation, or a hybrid of several domains.

Examples of audio Tracks:

```txt
Bass
Drums
Lead
Chords
Vocal FX
Noise Bed
```

Examples of visual Tracks:

```txt
Particles
Camera
Color Grade
Typography
Feedback
Lighting
Geometry
Compositor
```

Examples of control or hybrid Tracks:

```txt
Gesture Input
Crowd Reactivity
Beat Mapping
Scene Input
MIDI Control
OSC Control
```

A Track owns the graph nodes that belong to that responsibility.

For example, a Bass Track might own:

```txt
Wavetable synth
Filter
Distortion
Compressor
Bass pattern control
```

A Particles Track might own:

```txt
Particle generator
Force controls
Noise field
Color controls
Particle render/composite nodes
```

A Camera Track might own:

```txt
Camera transform
Orbit control
Shake control
Lens/focus controls
```

The important point is that a Track is not just a label. It defines the boundary of responsibility for Clip capture and recall. When a Clip on the Bass Track is launched, it changes only the nodes owned by Bass. It does not touch Particles, Camera, Drums, or any unassigned node.

This gives the performer independence. One Track can change while the others keep running.

### Clip

A Clip is a named performable state of one Track.

It answers:

> What should this Track be doing right now?

Examples:

```txt
Bass
  Groove
  Filtered
  Distorted Drop
  Off

Particles
  Sparse Drift
  Dense Swarm
  Burst
  Frozen

Camera
  Locked Wide
  Slow Orbit
  Crash Zoom
  Handheld Shake

Color Grade
  Monochrome
  Warm Low
  Acid
  Strobe White
```

Saving a Clip captures the current state of the Track's owned nodes. Launching a Clip recalls that state for only that Track.

For a traditional audio workflow, a Clip might feel like a pattern, sound, or instrument state. For visuals, a Clip might be a look, behavior, gesture, camera move, simulation mode, or compositing state. For control, a Clip might be a mapping, modulation behavior, or input response mode.

The model should not require the user to think about implementation details such as:

```txt
StateMachine state 2
state-preset mapping
target node preset
force_state
```

A user should be able to think:

> I saved a Clip called Burst on the Particles Track.

That is the level of abstraction the Session View should preserve.

### Scene

A Scene is a named moment or section of the performance.

It answers:

> What should the whole performance be doing during this section?

A Scene stores Clip assignments across Tracks. It does not store raw graph parameters directly.

Example:

```txt
Scene: Verse
  Bass        -> Groove
  Drums       -> Tight Kit
  Particles   -> Sparse Drift
  Camera      -> Locked Wide
  Color Grade -> Monochrome

Scene: Chorus
  Bass        -> Filtered
  Drums       -> Open Kit
  Particles   -> Dense Swarm
  Camera      -> Slow Orbit
  Color Grade -> Warm Low

Scene: Drop
  Bass        -> Distorted Drop
  Drums       -> Break
  Particles   -> Burst
  Camera      -> Crash Zoom
  Color Grade -> Strobe White
```

Scenes are the right abstraction for verse, chorus, bridge, drop, build, reset, blackout, and other section-level authoring.

Because a Scene launches Clips rather than raw parameters, it composes with per-Track performance. The user can launch a full Scene, then manually change one Track, then launch another Scene later. The Session View can always explain what is happening because every visible state belongs to the same hierarchy.

### The Session View

The Session View is the main performance surface.

It should be presented as a grid:

```txt
              Bass          Drums         Particles      Camera
Verse         Groove        Tight Kit     Sparse Drift   Locked Wide
Chorus        Filtered      Open Kit      Dense Swarm    Slow Orbit
Bridge        Off           Sparse        Frozen         Locked Wide
Drop          Distorted     Break         Burst          Crash Zoom
```

Rows are Scenes. Columns are Tracks. Cells are Clips.

Clicking a Scene row launches the full section. Clicking a cell launches only that Track's Clip.

The Session quantize setting controls when launches happen:

```txt
Off
Beat
Bar
4Bar
```

The same quantize setting applies to Scene launches and individual Clip launches. A Scene launch should queue all assigned Clips against the same quantize boundary, so the section change lands together.

The Session View should show:

```txt
Active Clip per Track
Queued Clip per Track
Active Scene when all assigned Clips match
Partial or mixed Scene when only some Tracks match
Dirty state when live Track settings differ from the saved Clip
Missing assignments or missing Clips clearly
```

The active Scene is only active if the current Clip on every assigned Track matches that Scene. If the user launches Drop, then manually changes Camera to Slow Orbit, Drop should no longer appear fully active. It should appear partial or mixed.

This is important because performance interfaces must be honest. The user should never have to guess whether the visible Scene name still describes the actual running state.

### Presets

Presets remain useful, but they are not the Session performance model.

A Preset is a reusable low-level setting for a node or module:

```txt
Filter preset: Open
Filter preset: Dark
Reverb preset: Big Hall
Synth preset: Warm Bass
```

A Clip is a performable state of a Track:

```txt
Bass Clip: Groove
Particles Clip: Burst
Camera Clip: Crash Zoom
```

A Scene is a section of the whole performance:

```txt
Scene: Chorus
Scene: Drop
```

Presets can help build Clips, and the runtime may continue to use preset-related machinery internally. But users should not need to create per-node presets and bind them to StateMachine states just to author a chorus or drop.

### ParamSets

`ParamSet` is the shared low-level implementation concept behind recallable parameter state.

It is not a user-facing performance concept. It is the internal vocabulary that prevents Vivid from
growing another incompatible snapshot mechanism.

Conceptually, a ParamSet stores:

```txt
node_id -> param_name -> float
node_id -> param_name -> string
```

Different product concepts use ParamSets at different scopes:

```txt
Preset = ParamSet over one node or module
Clip = ParamSet over one Track's owned nodes
Scene = Track-to-Clip assignments, not a ParamSet
```

This is the unifying rule:

> Capture/apply behavior should be shared; product meaning should come from scope.

The implementation should factor capture/apply behavior into reusable machinery instead of copying
the current variation, preset, and state-preset logic into a fourth system.

### State Machines

StateMachine and state-preset mapping are useful lower-level mechanisms. They support quantized transitions, crossfades, and automatic state-driven recall. They should remain available for advanced workflows.

But for normal Session authoring, StateMachine should be implementation detail, not the front door.

The user-facing language should be:

```txt
Track
Clip
Scene
Session
```

not:

```txt
StateMachine
state index
state-preset mapping
ensure_state_mapping
```

The Session system may still use the same runtime ideas underneath: queued transitions, quantize boundaries, crossfades, and state tracking. The difference is that the user interacts with named performance objects instead of anonymous state indices.

### Session Transitions

Session should own its transition policy. StateMachine crossfades can remain available for advanced
workflows, but normal Clip and Scene launches should not require hidden StateMachine nodes.

The Session data model should include transition fields from the beginning, even if the first runtime
implementation only supports hard cuts:

```txt
Track.default_transition
  mode: cut | fade
  duration_bars

Clip.transition_override
  mode: cut | fade
  duration_bars
```

Version 1 may implement only `cut`. If a graph requests `fade` before Session-native fades are
implemented, the runtime should either diagnose and fall back to `cut`, or gate fade support behind a
clearly marked feature flag.

When Session-native fades are implemented:

```txt
numeric params interpolate over the transition duration
string/file params switch at the launch boundary
operator-authored state follows the operator's declared snapshot/apply policy
```

Scene launches should apply each Track's transition policy while still sharing one quantize boundary
for the whole Scene.

### Removing Variations

The old Variation system is a whole-graph snapshot system. It captures raw parameter values across the graph and recalls them all at once.

That behavior competes with the new Session model. Because Vivid is still pre-alpha, this plan should
make a clean break instead of preserving a permanent Legacy Snapshot workflow.

The old model is:

```txt
Variation = whole graph snapshot
StateMachine = per-track state
Preset = node snapshot
```

The new model is:

```txt
Scene = section of the performance
Track = independent audio/visual/control channel
Clip = performable state of one Track
Preset = reusable low-level node/module setting
```

Existing Variation code can be mined for ParamSet capture/apply behavior, but Variations should not
remain as a user-facing Session feature.

The central product rule is:

> The Session View must never show two competing ways to do the same musical or visual thing.

If Scenes and Variations sit side by side as peers, the confusion remains. Scenes should replace Variations as the default performance concept.

### Authoring Flow

The intended authoring flow is:

```txt
1. Add Track: Bass
2. Assign Bass-related nodes to the Bass Track
3. Dial in the Bass sound and behavior
4. Save Clip: Groove
5. Change Bass parameters
6. Save Clip: Filtered
7. Repeat for Drums, Particles, Camera, Color Grade
8. Put each Track into the desired Clip for a section
9. Save Scene: Verse
10. Change Tracks into another combination
11. Save Scene: Chorus
12. Perform by launching Scenes and individual Clips
```

For visuals, the same flow applies:

```txt
1. Add Track: Particles
2. Assign particle nodes to the Track
3. Dial in a sparse visual state
4. Save Clip: Sparse Drift
5. Dial in a dense visual state
6. Save Clip: Dense Swarm
7. Dial in a burst state
8. Save Clip: Burst
9. Assign those Clips to Scenes
```

The workflow should feel like organizing performable ideas, not wiring infrastructure.

## 2. Proposed UI Design

The Session View should be a dedicated primary workspace mode, not just a taller replacement for the
old bottom variation strip. The graph remains the structural editor, but Session View is where the
piece becomes playable.

The full Session workspace is the main editing surface. A compact collapsed launcher can remain
available for performance, quick status, or reopening the workspace, but it should not be the primary
authoring surface.

### Workspace Layout

The main surface is a Scene x Track grid:

```txt
              Bass          Drums         Particles      Camera
Verse         Groove        Tight Kit     Sparse Drift   Locked Wide
Chorus        Filtered      Open Kit      Dense Swarm    Slow Orbit
Bridge        Off           Sparse        Frozen         Locked Wide
Drop          Distorted     Break         Burst          Crash Zoom
```

Rows are Scenes. Columns are Tracks. Cells are Clip assignments.

Track headers should show:

```txt
Track name
owned node count / ownership summary
active Clip
queued Clip, if any
dirty state, if live params differ from active Clip
quick actions: assign selected nodes, save Clip, update Clip, rename, reorder, remove
```

Scene headers should show:

```txt
Scene name
exact active / partial / queued state
launch action
update from current active Clips
rename, reorder, remove
```

Cells should show the assigned Clip name, plus active, queued, dirty, missing, or empty state. Empty
cells mean the Scene leaves that Track unchanged.

### Inline Authoring

Most Session authoring should happen directly in the grid.

The UI should support:

```txt
Add Track from the grid
Rename/reorder/remove Track from the Track header
Assign selected graph nodes to a Track from the Track header

Save Clip from the current Track state
Update Clip from the current Track state
Rename/reorder/remove Clip from Track or cell controls
Assign Clip to Scene from a cell menu or chooser

Save Scene from current active Clips
Update Scene from current active Clips
Rename/reorder/remove Scene from the Scene header

Click a Scene row to launch the full Scene
Click a cell to launch only that Track's Clip
```

Common authoring should not require the user to open StateMachine controls, create node presets, or
understand state indices. Those tools can still exist, but the grid should handle the normal flow from
empty Session to playable arrangement.

### Visual State Language

The grid should make performance state honest at a glance:

```txt
Exact active Scene        all assigned Track Clips match the Scene
Partial/mixed Scene      some assigned Track Clips match, but not all
Queued Scene             assigned Clips are waiting for a quantize boundary
Active Clip              current Clip for a Track
Queued Clip              pending Clip for a Track
Dirty Track/Clip         live owned-node params differ from the active Clip
Missing assignment       Scene has no Clip for this Track
Missing/deleted Clip     assignment points to a Clip that no longer exists
```

Exact, partial, queued, and dirty states should be visually distinct. The UI should never imply a
Scene is fully active after the user has manually overridden one Track.

### Empty States

The empty Session workspace should guide the first useful action instead of showing a blank grid.

Recommended progression:

```txt
No Tracks      -> Add Track
No owned nodes -> Assign selected graph nodes to Track
No Clips       -> Save Clip from current Track state
No Scenes      -> Save Scene from active Clips
```

Each empty state should use the same vocabulary as the model: Track, Clip, Scene, Session.

### No Legacy Snapshot Area

The new Session workspace should not include a permanent Legacy Snapshot area. The old Variation
strip should be removed as a primary UI surface.

## 3. High-Level Implementation Plan

This is a product hierarchy change, not a rename. The implementation should introduce first-class Session data and make the Session View consume that data directly.

The first implementation slice should establish the schema and headless runtime behavior before
rebuilding the UI. The UI should consume a proven Session model rather than shaping the model around
the old variation strip.

### Data Model

Add serialized Session data to the graph.

Conceptually:

```txt
session
  tracks
    id
    name
    owned_node_ids
    default_transition
    clips
      id
      name
      transition_override
      params
      string_params
  scenes
    id
    name
    assignments
      track_id -> clip_id | null
```

Track ids and Clip ids should be stable. Display names can change without breaking Scene assignments.

Scene ids should also be stable. Track, Clip, and Scene names are display labels only; they are never
the identity used by assignments, active state, queued state, or persistence.

A node belongs to at most one Track. This prevents ambiguous Clip recall. Unassigned nodes are not affected by Clip or Scene launches.

Clip parameter storage should use the shared ParamSet shape:

```txt
node_id -> param_name -> float
node_id -> param_name -> string
```

The difference is scope. A Variation captures the graph. A Clip captures only nodes owned by one Track.

Scenes should store assignments, not raw params:

```txt
scene.assignments[track_id] = clip_id | null
```

Scene assignments distinguish an unresolved missing assignment from an intentional "leave unchanged":

```txt
absent track key = missing assignment; show as incomplete in edit mode
null assignment = intentionally leave this Track unchanged
clip id = launch this Clip for this Track
```

If a Track assignment is `null`, launching that Scene leaves the Track unchanged. If a Track key is
absent, launching may still leave the Track unchanged, but the UI should show that the Scene has not
been fully authored for that Track.

The active Scene should not be persisted as independent truth. It should be derived from current
active Clips:

```txt
exact active Scene = every assigned Track is currently in that Scene's assigned Clip
partial/mixed Scene = at least one assigned Track matches, but not all assigned Tracks match
no active Scene = no Scene assignment set describes the current Track Clip combination
```

### Runtime Behavior

Implement Clip capture using shared ParamSet capture semantics, scoped to Track-owned nodes.

When saving or updating a Clip:

```txt
For each node owned by the Track:
  capture non-default numeric params
  capture non-default string/file params
  capture persisted operator state
  skip wire-driven params
  skip unsupported params using the same rules as variations
```

Clip capture should store author-settable state, not computed values. A param should be skipped when
it has `PARAM_LOCK_WIRES` or an active incoming param connection. This prevents a Clip from freezing a
live modulation value that should continue to come from the graph.

For operators with richer authored content, the rule is:

```txt
capture persisted operator state
do not capture transient editor/runtime state
```

For example, a `MidiClip` Track Clip should capture persisted content such as `pattern_data`, file
path, loop region, length, playback settings, transpose, and velocity scale. It should not capture
playhead position, editor selection, undo stacks, or transient cursor state unless those are
deliberately persisted by the operator. If future operators need state that cannot fit primitive
params, add an operator-authored snapshot payload hook instead of special-casing one operator type.

When launching a Clip:

```txt
For each node owned by the Track:
  reset recallable params to defaults
  apply captured Clip values
  apply captured persisted operator state
  respect existing preset/session/wire param locks
  mark touched nodes dirty
```

Launching a Clip must not touch:

```txt
nodes owned by other Tracks
unassigned nodes
Scene assignments
unrelated Session state
```

Implement Scene launch as coordinated Clip launch.

When queueing a Scene:

```txt
Resolve each Scene assignment to a Track and Clip
Ignore missing assignments with diagnostics
Queue all valid Track Clip launches for the same quantize boundary
Apply them together when the boundary is reached
```

The launch should be grouped as one logical operation: a Scene should not partially apply because
each Track independently computed a slightly different beat boundary.

The same quantize modes should be used for Clips and Scenes:

```txt
instant
beat
bar
4bar / four_bar
```

The runtime should track:

```txt
active clip per Track
queued clip per Track
queued Scene, if any
active Scene derived from active Track Clips
partial/mixed Scene state when some assigned Clips match
active transition per Track, when Session-native fades are implemented
```

### UI Behavior

Replace the current primary Session surface with the Scene x Track grid.

The grid should render:

```txt
columns = Tracks
rows = Scenes
cells = assigned Clips
```

The UI should support:

```txt
Add Track
Rename Track
Reorder Track
Remove Track
Assign selected nodes to Track

Save Clip from current Track state
Update Clip from current Track state
Rename Clip
Reorder Clip
Remove Clip

Save Scene from current active Clips
Update Scene from current active Clips
Rename Scene
Reorder Scene
Remove Scene
Queue Scene
Queue individual Clip cell
```

Visual states should distinguish:

```txt
active
queued
partial/mixed
dirty
missing assignment
missing/deleted Clip
```

The collapsed Session affordance should summarize Scene/Track state rather than Variation count:

```txt
active Scene name, if exact
partial/mixed state, if not exact
queued Scene, if any
Track count
dirty state
```

Variations should not move into a secondary product area. They should be removed from the primary
workflow entirely.

The UI rollout should follow the data model:

```txt
1. Build and test Session graph/runtime data first
2. Extend snapshots so UI can see Tracks, Clips, Scenes, active state, and queued state
3. Replace the primary Session strip with the Scene x Track grid
4. Remove the old variation strip UI
```

### Runtime and Control API

Add runtime/control commands for first-class Session authoring:

```txt
create_track(name)
rename_track(track_id, new_name)
remove_track(track_id)
move_track(track_id, to_index)
assign_nodes_to_track(track_id, node_ids)
unassign_nodes_from_track(track_id, node_ids)

save_clip(track_id, name)
update_clip(track_id, clip_id)
rename_clip(track_id, clip_id, new_name)
remove_clip(track_id, clip_id)
move_clip(track_id, clip_id, to_index)
queue_clip(track_id, clip_id, quantize)

save_scene(name)
update_scene(scene_id)
rename_scene(scene_id, new_name)
remove_scene(scene_id)
move_scene(scene_id, to_index)
set_scene_assignment(scene_id, track_id, clip_id)
clear_scene_assignment(scene_id, track_id)
queue_scene(scene_id, quantize)
```

Existing preset and advanced StateMachine APIs can continue to work:

```txt
save_preset
recall_preset
set_state_preset
ensure_state_mapping
queue_state_transition
```

But the docs and MCP descriptions should steer normal users toward:

```txt
Tracks
Clips
Scenes
```

and describe state-preset mappings as advanced tools. Variation APIs should be removed or replaced by
Session-native commands.

### Graph Snapshot for UI

Extend the UI snapshot with Session data.

The UI needs:

```txt
tracks
  id
  name
  owned_node_ids
  clips
    id
    name

scenes
  id
  name
  assignments

active_clip_by_track
queued_clip_by_track
active_scene derived from active clips
queued_scene
scene_match_state
dirty_track_clip_state
```

The old `variations` and `clip_machines` snapshot fields should not remain as product-facing Session
data. The Session View should be driven by the new Session snapshot.

### Clean Break

This refactor should make a clean break. Vivid is pre-alpha, so old graphs and old Session/Variation
workflows do not need product-level compatibility guarantees.

The recommended cleanup behavior is:

```txt
Remove Variation as a primary graph/session concept
Remove the old variation strip UI
Remove variation-oriented MCP/tool descriptions
Keep Presets as low-level node/module settings
Keep StateMachine/state-preset mapping only as advanced machinery if still useful
Reject old variation JSON with a clear pre-alpha schema message, or ignore it during load
```

### Phased Implementation Plan

This should land foundation-first. Build the durable model and headless behavior before replacing the
primary UI.

**Phase 1: Foundation: Session Schema**

Add graph data for Session, Tracks, Clips, Scenes, stable ids, node ownership, and Scene assignments.
Include transition policy fields and the distinction between missing assignment and explicit
leave-unchanged assignment. Save and load the new JSON shape. Remove Variation from the new Session
schema instead of preserving it as a parallel performance model.

**Phase 2: ParamSet Refactor**

Factor shared capture/apply behavior out of the existing variation and preset paths where useful.
Use the shared ParamSet machinery for Clip capture and recall. Keep Preset behavior compatible, but
replace Variation behavior with Session-native Clips and Scenes. Make Clip capture skip wire-driven
params and capture only persisted operator state.

**Phase 3: Headless Runtime And Control API**

Implement Track CRUD, node assignment, Clip CRUD and launch, Scene CRUD and launch, and quantized
Scene/Clip queueing. Track active and queued Clips per Track. Derive exact/partial Scene state from
active Clips. Missing nodes, missing Clips, and stale assignments should produce diagnostics, not
crashes. Implement `cut` transitions first; accept `fade` schema only with clear diagnostics or a
feature gate until Session-native fades exist.

**Phase 4: Snapshot And MCP Surface**

Extend UI/control snapshots with Tracks, Clips, Scenes, active state, queued state, dirty state, and
Session diagnostics. Remove or replace Variation-oriented snapshot fields and MCP/tool descriptions
with Session-native Tracks, Clips, and Scenes.

**Phase 5: Dedicated Session Workspace UI**

Build the Scene x Track grid as the primary Session authoring surface. Implement inline Track, Clip,
and Scene authoring; launch actions; exact/partial/queued/dirty state display; empty states; and a
clean removal of the old variation strip UI.

**Phase 6: Cleanup**

Remove dead variation code paths, stale docs, old UI state, old command descriptions, and tests that
only exist to preserve old Variation behavior.

### Tests

Graph serialization tests:

```txt
Session data saves and loads
Stable Track ids survive rename and save/load
Stable Clip ids survive rename and save/load
Stable Scene ids survive rename and save/load
Track transition policy survives save/load
Clip transition override survives save/load
Track order survives save/load
Clip order survives save/load
Scene order survives save/load
Scene assignments survive save/load
Explicit leave-unchanged Scene assignments survive save/load
Node ownership survives save/load
Old variation JSON is ignored or rejected with a clear pre-alpha schema message
```

Runtime tests:

```txt
Saving a Clip captures only Track-owned nodes
Launching a Clip changes only Track-owned nodes
Launching a Clip resets owned nodes before applying captured values
Clip capture skips wire-driven params
Clip capture includes persisted operator state
Clip capture excludes transient editor/runtime state
Launching a Scene queues all assigned Clips together
Quantized Scene launch applies all assigned Clips at one shared boundary
Launching a Scene leaves unassigned Tracks unchanged
Launching a Scene treats explicit leave-unchanged separately from missing assignment
Manual Clip launch changes active Scene to partial/mixed
Active Scene is derived from active Clips rather than persisted independently
Param locks are respected
Fade transition requests before fade support diagnose or fall back predictably
Missing node ids produce diagnostics, not crashes
Missing Clip ids produce diagnostics, not crashes
```

UI/control tests:

```txt
Session snapshot includes Tracks, Clips, Scenes, active Clips, queued Clips
Session snapshot does not expose Variations as Session data
Create/rename/remove/reorder commands work for Tracks
Save/update/rename/remove/reorder commands work for Clips
Save/update/rename/remove/reorder commands work for Scenes
Scene assignment commands work
Variation commands are removed or replaced by Session-native commands
```

Phase-specific test expectations:

```txt
Phase 1: schema save/load, transition fields, assignment states, and clean-break schema tests
Phase 2: ParamSet capture/apply, wired-param skip, and persisted operator state tests
Phase 3: runtime/control API and cut-transition behavior tests
Phase 4: snapshot and MCP surface tests
Phase 5: UI interaction and manual acceptance tests
```

Manual acceptance scenarios:

```txt
Create Tracks from an empty Session
Assign selected graph nodes to Tracks
Save Clips from live Track state
Save Scenes from active Clips
Build an audiovisual Session with Bass, Drums, Particles, Camera, and Color Grade Tracks
Save multiple Clips per Track
Save Verse, Chorus, Bridge, and Drop Scenes
Launch Scenes on bar quantize
Launch an individual Camera Clip after a Scene and verify the Scene becomes partial
Queue Scene and Clip launches with the same quantize settings
Mark Camera as explicit leave-unchanged in one Scene and verify it is not shown as forgotten
Reload the graph and verify the Session View is unchanged
Verify the old variation strip is gone from the primary UI
```

### Implementation Principle

The implementation should optimize for one coherent user model:

```txt
Session = the playable performance
Track = an independent audio/visual/control channel
Clip = a performable state of one Track
Scene = a section that launches Clips across Tracks
Preset = a reusable low-level node/module setting
```

Every UI label, command description, and doc update should reinforce that hierarchy.

Implementation may be disruptive. Product coherence is more important than preserving the current
shape of the variation strip, clip launcher, or state-preset workflow.

The success condition is not merely that the old features have new names. The success condition is that a new user can build a verse, chorus, bridge, drop, visual burst, camera change, and color shift without learning about raw graph snapshots, state indices, or state-preset mappings.
