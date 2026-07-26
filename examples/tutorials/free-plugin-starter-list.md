# Free Plugin Starter List

This list supports ADR-0034 beginner tutorials. Keep the first tutorial strict and small: only
Surge XT is required. Everything else is optional material for follow-up tutorials and demos.

## Required For Tutorial 1

### Surge XT

- Formats: CLAP, VST3, AU
- Role: required beginner instrument
- Install target Vivid expects on macOS: `/Library/Audio/Plug-Ins/CLAP/Surge XT.clap`
- Official site: https://surge-synthesizer.github.io/
- Homebrew cask: `brew install --cask surge-xt`

Surge XT is the assumed installed instrument because it is free, open source, cross-platform,
audible without extra sample packs, and ships a useful preset library. The first tutorial uses the
CLAP bundle directly so the generated project does not depend on a paid DAW/plugin setup.

## Optional Next Installs

### Surge XT Effects

- Formats: CLAP, VST3, AU
- Role: tutorial-friendly delay/reverb/filter effects
- Install target on macOS: `/Library/Audio/Plug-Ins/CLAP/Surge XT Effects.clap`
- Official site: https://surge-synthesizer.github.io/

Surge XT Effects usually installs with Surge XT. It is a good second dependency because it keeps
the early tutorial stack inside one free, known plugin family.

### Vital

- Formats: VST, VST3, AU, LV2
- Role: optional wavetable instrument for later sound-design tutorials
- Official site: https://vital.audio/

Vital's Basic tier is free and includes the full synth feature set with a smaller content library.
Use it only in optional tutorials unless Vivid gains a more generic installed-plugin selection flow
for beginner scripts.

### Cardinal

- Formats: CLAP, VST3, AU, LV2, VST2, standalone
- Role: optional modular-synthesis playground for advanced tutorials
- Official site: https://cardinal.kx.studio/
- Releases: https://github.com/DISTRHO/Cardinal/releases

Cardinal is powerful but too broad for the first-run path. It is better as a later creative-coding
example once the beginner has already seen Vivid's track, graph, mapping, and verification concepts.
