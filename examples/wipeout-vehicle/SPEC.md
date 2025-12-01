# Wipeout Vehicle Generator

## Goal
Create an audio-reactive procedural hover vehicle inspired by Wipeout 2097, demonstrating multiple Vivid features working together: procedural 3D geometry, PBR materials, decals, bloom effects, and audio-driven animation.

## Done
- [x] Basic angular hull geometry (wedge body)
- [x] Side fins and rear spoiler
- [x] Cockpit canopy
- [x] PBR metallic materials with roughness variation
- [ ] Team livery decal projection
- [ ] Grime/weathering overlays
- [x] Engine exhaust (emissive glow)
- [x] Bloom post-processing
- [x] Orbit camera with mouse control
- [x] Audio input with FFT analysis
- [x] Beat detection → engine glow intensity
- [x] Bass → hover height oscillation
- [x] Mids → color cycling
- [ ] Particle exhaust trail

## Current Focus
Core showcase complete. Optional future enhancements: decals for team livery, particle exhaust trails.

## Vehicle Design Notes
- Wipeout 2097 style: angular, aggressive, low-slung
- Main body: elongated wedge tapering to sharp nose
- Side pods: cylindrical engine nacelles
- Fins: thin angular stabilizers
- Cockpit: tinted canopy dome toward front
- Colors: team-based (red/white, blue/yellow, etc.)

## Technical Approach
1. Build hull from triangulated vertex data using ctx.createMesh()
2. Use multiple meshes for body parts (hull, fins, cockpit, engines)
3. Apply different PBR materials per component
4. Use decal projection for livery stripes
5. Composite 2D particles over final 3D render for exhaust
