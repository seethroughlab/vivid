# Post-Processing Example

Demonstrates depth-based post-processing effects and 3D particles.

## Operators Demonstrated

- **Fog** - Distance-based atmospheric fog using depth buffer
- **DepthOfField** - Focus-based blur for cinematic depth effect
- **DepthMask** - Restrict 2D effects to 3D object regions using depth
- **Particles3D** - Billboard particle system with emitter shapes and physics

## Key Concepts

### Enabling Depth Output
All depth-based effects require `setDepthOutput(true)` on Render3D:

```cpp
auto& render = chain.add<Render3D>("render");
render.setDepthOutput(true);  // CRITICAL: enables depth buffer
```

### Fog
Distance-based atmospheric haze:

```cpp
auto& fog = chain.add<Fog>("fog");
fog.input(&render);                // Takes color + depth
fog.fogColor[0] = 0.5f;           // Fog color (match clear color)
fog.fogColor[1] = 0.55f;
fog.fogColor[2] = 0.6f;
fog.fogStart = 5.0f;              // Fog starts at 5 world units
fog.fogEnd = 40.0f;               // Fully fogged at 40 units
fog.fogMode = FogMode::Linear;    // Linear, Exponential, or ExponentialSquared
```

### DepthOfField
Focus-based blur for cinematic depth:

```cpp
auto& dof = chain.add<DepthOfField>("dof");
dof.input(&render);               // Takes color + depth
dof.focusDistance(0.3f);           // Normalized depth (0=near, 1=far)
dof.focusRange(0.1f);             // Range that stays sharp
dof.blurStrength(0.6f);           // Maximum blur amount
```

### DepthMask
Restrict 2D effects to 3D geometry regions:

```cpp
auto& mask = chain.add<DepthMask>("mask");
mask.input("effect2d");            // 2D texture to mask
mask.setRender3D(&render);         // Depth source

mask.mode(DepthMaskMode::Object);      // Only on 3D objects
mask.mode(DepthMaskMode::Background);  // Only in empty space
mask.mode(DepthMaskMode::DepthFade);   // Fade based on depth

mask.threshold = 0.95f;           // Depth cutoff
mask.softness = 0.3f;             // Edge softness
mask.invert = false;              // Invert mask
```

### Particles3D
Billboard particle system with camera-facing sprites:

```cpp
auto& fire = chain.add<Particles3D>("fire");
fire.setCameraInput(&camera);          // REQUIRED: billboard orientation

// Emitter
fire.emitter(Emitter3DShape::Cone);    // Point, Sphere, Box, Cone, Disc
fire.position(0, 0, 0);
fire.emitterDirection(0, 1, 0);        // Upward
fire.coneAngle(15.0f);

// Emission
fire.emitRate(100.0f);                 // Particles per second
fire.maxParticles(5000);

// Motion
fire.velocity(0, 2.0f, 0);            // Initial velocity
fire.spread(20.0f);                    // Cone of directions
fire.gravity(0, -0.5f, 0);            // Physics gravity
fire.drag(0.1f);                       // Velocity damping

// Appearance
fire.life(2.0f);                       // Lifetime in seconds
fire.size(0.3f, 0.0f);                // Start size → end size
fire.color(1, 0.5f, 0.1f);            // Start color (orange)
fire.colorEnd(1, 0, 0, 0);            // End color (transparent red)
fire.additive(true);                   // Additive blending for glow
fire.fadeOut(true);                    // Fade at end of life
```

### Emitter Shapes
```cpp
fire.emitter(Emitter3DShape::Point);    // Single point
fire.emitter(Emitter3DShape::Sphere);   // Sphere surface/volume
fire.emitter(Emitter3DShape::Box);      // Box volume
fire.emitter(Emitter3DShape::Cone);     // Cone (jets, flames)
fire.emitter(Emitter3DShape::Disc);     // Flat disc
```

## Related Operators

- **Render3D** - 3D renderer that provides depth buffer
- **Bloom** - Glow effect (can combine with DepthMask)
- **Composite** - Layer multiple effects together
