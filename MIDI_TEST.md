# MIDI Input Testing

## Setup

1. Plug in your MIDI controller before launching vivid
2. Build if needed: `cmake --build build`
3. Launch: `cd build && ./vivid midi_demo.json`

You should see stderr output listing available MIDI ports:
```
[MidiInput] Available MIDI ports (N):
  [0] Your Controller Name
  ...
```

## Basic CC control

The demo graph routes `midi1/cc_value` -> LFO frequency -> noise scale.

1. Turn the mod wheel (CC1) on your controller
2. Run `inspect midi1` in the REPL — `cc_value` and `mod_wheel` should both change
3. The noise visual should respond as the LFO frequency changes

If your controller isn't on port 0, check the port list in stderr and run:
```
set midi1/device <port_number>
```

## CC Learn

1. `set midi1/learn 1`
2. Wiggle any knob/slider on your controller
3. Stderr prints: `[MidiInput] CC Learn: captured CC <N>`
4. `get midi1/cc_number` — should show the captured CC number
5. Now `cc_value` tracks that knob instead of CC1

## Note input

1. Press a key on your controller
2. Stderr prints: `[MidiInput] Note ON: <note> vel=<vel>`
3. `inspect midi1` — check:
   - `note` = MIDI note number (e.g. 60 for middle C)
   - `velocity` = 0.0–1.0
   - `gate` = 1.0 while held
   - `trigger` = 1.0 on the frame of note-on, then 0.0
4. Release the key — `gate` drops to 0.0

## Pitch bend

1. Move the pitch bend wheel
2. `inspect midi1` — `pitch_bend` should range from -1.0 to +1.0

## Channel filter

By default `channel=0` (omni — accepts all channels). To filter:
```
set midi1/channel 1
```
Now only channel 1 messages come through.
