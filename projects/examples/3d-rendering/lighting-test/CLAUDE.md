# Lighting Test

Demonstrates all light types in Vivid 3D: point lights, spot lights, and directional lights with colored lighting.

## Vision

A simple test scene with white objects arranged to showcase different light types. Colored lights create dramatic overlapping pools of illumination, demonstrating how multiple lights combine and affect surfaces.

## Scene Setup

- Ground plane (light gray to show colored lighting)
- 3 white spheres in triangle formation
- Central white cube

## Lighting

| Light | Type | Color | Notes |
|-------|------|-------|-------|
| Red | Point | Red | Left side, radius 12 |
| Blue | Point | Blue | Right side, radius 12 |
| Green | Spot | Green | Above, 45° cone pointing down |
| Ambient | Directional | White | Subtle fill, 0.5 intensity |

## Animation

- Camera slowly orbits the scene
- Spot light circles above, always pointing toward center
- Shows how lighting changes with viewpoint and light position

## Key Concepts

- Point lights with range/falloff
- Spot lights with cone angle and soft blend
- Multiple light contributions combining on surfaces
