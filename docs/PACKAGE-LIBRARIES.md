# Package Libraries

Packages are distributed as source code (git repos). `vivid install` clones the repo and compiles
it against your current Vivid headers. When Vivid updates, recompile your packages with
`vivid rebuild <name>`. There are no pre-compiled binaries — everything builds from source.

Vivid supports additional operator libraries as packages.

For package authoring templates/checklists, see `../vivid-package-template/README.md`.

## Available Package Libraries

### vivid-3d

3D operator suite (SDF, meshes, particles, lighting, materials, and more).

- Repo: `https://github.com/seethroughlab/vivid-3d`

### vivid-wavetable

Wavetable synthesizer operator.

- Repo: `https://github.com/seethroughlab/vivid-wavetable`

### vivid-glitch

Creative audio and visual glitch effects (stutter, tape stop, datamosh, VHS, and more).

- Repo: `https://github.com/seethroughlab/vivid-glitch`

### vivid-cef

Chromium Embedded Framework browser source operator.

- Repo: `https://github.com/seethroughlab/vivid-cef`

### vivid-physics2d

2D physics simulation operators.

- Repo: `https://github.com/seethroughlab/vivid-physics2d`

### vivid-plexus

Plexus GPU + audio combined effects.

- Repo: `https://github.com/seethroughlab/vivid-plexus`

## Install Packages

From a built vivid-core checkout:

```bash
./build/vivid install https://github.com/seethroughlab/vivid-3d.git
./build/vivid install https://github.com/seethroughlab/vivid-wavetable.git
./build/vivid install https://github.com/seethroughlab/vivid-glitch.git
./build/vivid install https://github.com/seethroughlab/vivid-cef.git
```

## Local Development Workflow

```bash
# Scaffold a new package from template
./build/vivid scaffold-package vivid-my-package --template single

./build/vivid link ../vivid-3d
./build/vivid rebuild vivid-3d
./build/vivid uninstall vivid-3d
```

Use the same pattern for other package names.

## Troubleshooting

- If package graphs show up but nodes appear as `MISSING`, check for plugin ABI mismatch.
- Typical symptom in logs: `probe: skipping ... (ABI X != runtime ABI Y)`.
- Fix: rebuild and run the matching `vivid` binary, then rerun package rebuild:

```bash
cmake --build build
./build/vivid rebuild <package-name>
```
