# Particles3D Test

Demonstrates the 3D GPU particle system with billboarded sprites, world-space physics, and fire-like effects.

## Vision

A dramatic fire/embers effect rising from a point source. Particles are emitted in a cone pattern, rising with turbulence and fading from bright orange to dark red as they die.

## Emitter Configuration

- **Shape**: Cone (20° spread angle)
- **Position**: Below center, pointing up
- **Rate**: 150 particles/second
- **Max**: 3000 particles

## Physics

- Upward velocity (3.0 units/sec)
- Slight downward gravity (simulating air resistance)
- Turbulence for organic motion
- Radial velocity for spread
- Drag to slow particles over time

## Visual Style

- Color gradient: bright orange → dark red → fade out
- Size: starts at 0.15, shrinks to 0.05
- Additive blending for glow effect
- Depth-sorted for correct transparency
- Dark blue background for contrast

## Animation

- Camera slowly orbits the effect
- Particles self-animate through physics simulation
