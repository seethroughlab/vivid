# Smoke Simulation

3D particle smoke with animated spritesheet textures. Demonstrates billboarded particles with frame-by-frame animation.

## Vision

Soft, puffy smoke rising from the ground. Each particle displays an animated smoke texture that loops independently, creating organic, varied motion. The particles grow as they rise, simulating expanding gas.

## Spritesheet Animation

- 30-frame smoke animation
- 6x5 grid layout (6 columns, 5 rows)
- 12 FPS playback
- Random starting frame per particle
- Time-based animation (not lifetime-based)

## Emitter Configuration

- **Shape**: Disc at ground level
- **Rate**: 15 particles/second (sparse, puffy)
- **Lifetime**: 4 seconds (long-lived)

## Physics

- Gentle upward drift (1.5 units/sec)
- Slight buoyancy (positive Y gravity)
- Heavy drag to slow movement
- Turbulence for organic drift
- Slow spin rotation

## Visual Style

- White/gray smoke colors
- Normal blending (not additive)
- Size grows from 0.5 to 2.5 (expanding gas)
- Fade out at end of life
- Sky blue background
