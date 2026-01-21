# Fluid Simulation

Interactive GPU-accelerated 2D fluid dynamics based on Navier-Stokes equations. Draw with mouse to inject colorful dye that flows, swirls, and dissipates realistically.

## Vision

A dark canvas where mouse strokes create vibrant flowing colors. The fluid naturally develops vortices and turbulence, with dye trails that persist and slowly fade. Bloom post-processing adds an ethereal glow.

## Operators

| Operator | Purpose |
|----------|---------|
| FluidSim | GPU Navier-Stokes fluid simulation |
| Bloom | Post-processing glow effect |

## Interaction

- **Left Mouse Drag** - Add force and rainbow dye
- **Right Mouse Drag** - Add invisible force (push fluid without color)
- **Space** - Clear simulation
- **F** - Toggle fullscreen

## Key Parameters

- `viscosity` - Fluid thickness (0.0001 = water-like)
- `dissipation` - Velocity decay per frame (0.995 = slow fade)
- `vorticity` - Swirling detail strength (0.4 = moderate swirls)
- `dyeDissipation` - Color fade rate (0.985 = slow fade)
- `pressureIterations` - Simulation accuracy (40 = high quality)
- `forceScale` - Mouse force multiplier

## Key Concepts

### Navier-Stokes Simulation Pipeline
The fluid simulation runs these steps each frame:
```cpp
1. addForce() - Inject velocity from mouse
2. Advection - Move velocity field by itself
3. Vorticity - Add swirling detail
4. Divergence - Compute velocity divergence
5. Pressure Solve - 40 Jacobi iterations
6. Gradient Subtract - Make velocity divergence-free
7. Advect Dye - Move color by velocity
8. Render - Output dye field
```

### Force Injection
```cpp
g_fluid->addForce(pos.x, pos.y,    // Normalized position (0-1)
                 delta.x * 50.0f,  // Force X
                 delta.y * 50.0f,  // Force Y
                 0.02f);           // Splat radius
```

### Dye Injection with Color Cycling
```cpp
g_hue = std::fmod(g_hue + dt * 0.3f, 1.0f);
glm::vec3 color = hsvToRgb(g_hue, 0.8f, 1.0f);
g_fluid->addDye(pos.x, pos.y, color.r, color.g, color.b, 0.015f);
```

### Post-Processing
```cpp
auto& bloom = chain.add<Bloom>("bloom");
bloom.input("fluid");
bloom.threshold = 0.3f;
bloom.intensity = 0.4f;
```

## Tips for Fluid Effects

1. **Lower viscosity** for water-like flow, higher for honey
2. **Higher vorticity** creates more turbulent swirls
3. **Adjust dissipation** for how quickly motion dies down
4. **Use Bloom** to make bright areas glow
5. **Clear with Space** to reset the canvas
