# Particles

Demonstrates the 2D GPU particle system with multiple emitters, physics, and compositing.

## Vision

Three distinct particle effects layered together: rising fire, arcing fountain, and expanding rainbow ring. Shows the versatility of the Particles operator with different emitter shapes and physics configurations.

## Particle Layers

| Layer | Emitter | Effect |
|-------|---------|--------|
| Fire | Point | Rising flames with negative gravity, gold to red fade |
| Fountain | Point | Arcing water with positive gravity, blue tones |
| Ring | Ring | Expanding circle with rainbow colors and drag |

## Key Features

- Different emitter shapes (Point, Ring)
- Color gradients over particle lifetime
- Physics: gravity, drag, radial velocity
- Additive blending for glow
- fadeOut for soft particle edges

## Interaction

- **Mouse position** - Controls fire emitter location
- Fountain emit rate pulses with time
- Ring emitter orbits the center
