## What you'll build

A two-track live performance session: one track for drums, one for a synth. Each track holds two named Clips (snapshots of its params), and a Scene launches both tracks together. You'll switch between clips mid-performance and trigger scene changes with beat-quantized timing.

Open the **AV Performance** example (`graphs/session/av_performance.json`) as a starting point — it already has a drum chain and a synth chain wired up. Or use any graph you've already built.

## Concepts covered

| Concept | Description |
|---------|-------------|
| Track | A named group of graph nodes whose params are managed together |
| Clip | A named snapshot of a track's parameter values, recalled on demand |
| Scene | A row that assigns one clip per track; launching a scene fires all assignments at once |
| Dirty indicator | A dot (•) on the track header when live params differ from the active clip |
| Quantize | Defers a launch to the next beat, bar, or 4-bar boundary for rhythmic alignment |

## Step 1: Open the session strip

Press **V** (or use **View → Session**). A horizontal strip appears at the bottom of the graph editor. It starts with one empty track column and one empty scene row.

## Step 2: Assign nodes to the first track

1. In the graph, click to select all the drum nodes (DrumSequencer, DrumKick, DrumSnare, DrumHiHat, Mixer, Reverb — anything that belongs to the drum chain).
2. **Right-click the track header** → **Assign Selected**. The selected nodes now belong to this track. Their graph borders light up in the track's color.
3. **Double-click the track header** to rename it — type "Drums" and press Enter.

## Step 3: Save your first clip

The drum pattern is currently live. Dial in an "Intro" feel — maybe a sparse hi-hat-only pattern, low reverb.

Click the **+** button in the track header. A dialog prompts for a name. Type "Intro" and confirm. The clip cell appears in the grid.

The track now has one clip, "Intro", shown as active (colored background).

## Step 4: Create a second clip

Change the drum params: toggle more steps in the sequencer, increase reverb mix, bring the kick in. This is your "Drop".

Click **+** again in the track header. Name it "Drop".

You now have two clips. The "Drop" clip is active; "Intro" is stored.

## Step 5: Switch between clips

Click the **Intro** cell. The drum nodes' params snap back to the Intro snapshot instantly.

Click **Drop**. Params snap to the Drop state.

Tweak any param on the drum nodes. Notice the **•** (dirty dot) appears on the track header — the live state has drifted from the active clip. Click **+** to save a new clip or right-click the active cell → **Update Clip** to overwrite it.

## Step 6: Add a second track

Click the **+** in the session header to add a new track column.

Select your synth nodes in the graph (ChordProgression, WavetableOsc, VoiceMixer, etc.). **Right-click the new track header** → **Assign Selected**. Rename it "Synth".

Save two clips for this track the same way: dial in a "Verse" state (click **+**, name it "Verse"), then adjust the synth params and save a "Chorus" clip.

## Step 7: Create a scene

You now have two tracks, each with two clips. A Scene captures one clip per track and launches them together.

**Right-click the scene row header** → **Update**. This captures the currently active clip from each track into that scene. Name it "Intro".

Set the second scene: click the Intro clip on Drums and the Verse clip on Synth to make them active. Right-click the second scene row → **Update**. Name it "Verse".

## Step 8: Launch scenes

Click **▶** on the "Intro" scene row. Both tracks' nodes snap to their assigned clips at once.

Click **▶** on the "Verse" scene. Both tracks switch simultaneously.

## Step 9: Quantized launches

The quantize selector in the session header reads **Instant** by default. Change it to **Bar**.

Now when you click a scene, nothing changes yet — the switch waits for the next bar boundary and then fires. You hear the current state play out cleanly before the new clips kick in.

Try **4Bar** for longer, DJ-style transitions.

## What's happening

```
Session
├── Track "Drums"  → [Clip "Intro"]  [Clip "Drop"]
│       ↕ owns → DrumSequencer, DrumKick, Mixer, Reverb
└── Track "Synth"  → [Clip "Verse"]  [Clip "Chorus"]
        ↕ owns → ChordProgression, WavetableOsc, VoiceMixer

Scene "Intro"  → Drums/Intro  +  Synth/Verse
Scene "Chorus" → Drums/Drop   +  Synth/Chorus
```

**Clips** store a flat map of `param → value` for every parameter on every node the track owns. Launching a clip calls `set_param` on each node — the same as dragging a knob, just done all at once.

**Scenes** are a lookup table: each scene row holds one clip ID per track. Launching a scene iterates the table and launches each track's assigned clip.

**Quantization** defers the param writes until the next beat boundary. The session watches the graph metronome (or a designated Clock's `beat_phase`) and fires the buffered launch when the boundary arrives.

**Session state persists** when you save the graph (`Cmd+S`). Tracks, clips, scene assignments, and which clip is active all round-trip through the JSON.

## Next steps

- [Building a Complete AV Patch](../complete-av-patch/) — build the drum + synth + visual rig this tutorial used as a starting point
- [Multi-Voice Patterns with Lanes](../multi-voice-lanes/) — add polyphonic voice layers to a track
