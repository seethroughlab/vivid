# Package Libraries

Vivid supports additional operator libraries as packages.

## Available Package Libraries

### vivid-wavetable

- `WavetableSynth`
- Repo: `https://github.com/seethroughlab/vivid-wavetable`

### vivid-drums

- `DrumKick`
- `DrumSnare`
- `DrumHiHat`
- `DrumClap`
- `DrumCymbal`
- Repo: `https://github.com/seethroughlab/vivid-drums`

### vivid-plexus

- `Plexus`
- `PlexusSynth`
- Repo: `https://github.com/seethroughlab/vivid-plexus`

### vivid-sequencers

- `Sequencer`
- `DrumSequencer`
- `PatternSeq`
- `NotePattern`
- `NoteDuration`
- `Arpeggiator`
- `ChordProgression`
- `StateMachine`
- Repo: `https://github.com/seethroughlab/vivid-sequencers`

### Additional package libraries

- `vivid-3d`: `https://github.com/seethroughlab/vivid-3d`
- `vivid-glitch`: `https://github.com/seethroughlab/vivid-glitch`

## Install Packages

From a built vivid-core checkout:

```bash
./build/vivid install https://github.com/seethroughlab/vivid-wavetable.git
./build/vivid install https://github.com/seethroughlab/vivid-drums.git
./build/vivid install https://github.com/seethroughlab/vivid-plexus.git
./build/vivid install https://github.com/seethroughlab/vivid-sequencers.git
```

Optional package libraries:

```bash
./build/vivid install https://github.com/seethroughlab/vivid-3d.git
./build/vivid install https://github.com/seethroughlab/vivid-glitch.git
```

## Local Development Workflow

```bash
./build/vivid link ../vivid-sequencers
./build/vivid rebuild vivid-sequencers
./build/vivid uninstall vivid-sequencers
```

Use the same pattern for other package names.
