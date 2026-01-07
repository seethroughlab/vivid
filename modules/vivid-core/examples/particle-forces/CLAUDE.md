# Particle Forces

Demonstrates the modular particle force system with CPU simulation.

## Operators Used

- **ParticleSystem** - GPU-accelerated particle system
- **CurlNoiseForce** - Turbulent, swirling motion
- **DragForce** - Velocity damping
- **GravityForce** - Directional force

## Key Concepts

### Force Stack API
ParticleSystem uses a modular force stack - add forces dynamically:
```cpp
auto& ps = chain.add<ParticleSystem>("particles");
ps.clearForces();  // Remove default forces

// Add individual forces
auto& curl = ps.addForce<CurlNoiseForce>();
auto& drag = ps.addForce<DragForce>();
auto& grav = ps.addForce<GravityForce>();

// Get a force back
auto* existingCurl = ps.getForce<CurlNoiseForce>();
```

### Available Forces
```cpp
// Gravity - constant directional force
auto& grav = ps.addForce<GravityForce>();
grav.direction.set(0.0f, -9.8f, 0.0f);

// Drag - velocity damping
auto& drag = ps.addForce<DragForce>();
drag.coefficient = 0.3f;  // 0 = no drag, 1 = heavy drag

// Curl Noise - turbulent, swirling motion
auto& curl = ps.addForce<CurlNoiseForce>();
curl.strength = 1.2f;
curl.scale = 3.0f;       // Noise frequency
curl.speed = 0.4f;       // Animation speed
curl.octaves = 3;        // Noise complexity
curl.is3D = false;       // 2D or 3D curl

// Turbulence - similar to curl but more chaotic
auto& turb = ps.addForce<TurbulenceForce>();
turb.strength = 0.5f;
turb.scale = 2.0f;
turb.octaves = 2;

// Point Attractor - pull toward a point
auto& attr = ps.addForce<PointAttractorForce>();
attr.position.set(0.5f, 0.5f, 0.0f);
attr.strength = 2.0f;
attr.radius = 0.5f;

// Vortex - circular motion around an axis
auto& vort = ps.addForce<VortexForce>();
vort.position.set(0.5f, 0.5f, 0.0f);
vort.axis.set(0.0f, 0.0f, 1.0f);
vort.strength = 1.0f;

// Wind - directional force with noise variation
auto& wind = ps.addForce<WindForce>();
wind.direction.set(1.0f, 0.0f, 0.0f);
wind.strength = 0.5f;
wind.turbulence = 0.2f;

// Velocity Field - sample velocity from a texture
auto& field = ps.addForce<VelocityFieldForce>();
field.setTexture(&flowTexture);
field.strength = 1.0f;
```

### Simulation Modes
```cpp
ps.simulation(SimulationMode::CPU);  // CPU simulation (debugging)
ps.simulation(SimulationMode::GPU);  // WebGPU compute (default, fast)
```

### Particle Spaces
```cpp
ps.space(ParticleSpace::Screen2D);   // Normalized coordinates (0-1)
ps.space(ParticleSpace::World3D);    // 3D world coordinates
```

### Rendering Modes
```cpp
ps.rendering(RenderMode::Circle);    // Soft circles
ps.rendering(RenderMode::Point);     // Hardware points
ps.rendering(RenderMode::Trail);     // Motion trails
ps.rendering(RenderMode::Sprite);    // Custom texture
```

## Force Combinations

### Smoke Effect
```cpp
ps.addForce<GravityForce>().direction.set(0, 0.5f, 0);  // Upward
ps.addForce<DragForce>().coefficient = 0.1f;
ps.addForce<TurbulenceForce>().strength = 0.3f;
```

### Galaxy/Spiral
```cpp
ps.addForce<PointAttractorForce>().strength = 0.5f;
ps.addForce<VortexForce>().strength = 1.0f;
ps.addForce<DragForce>().coefficient = 0.05f;
```

### Flowing Water
```cpp
auto& curl = ps.addForce<CurlNoiseForce>();
curl.strength = 1.0f;
curl.scale = 2.0f;
curl.speed = 0.3f;
ps.addForce<DragForce>().coefficient = 0.2f;
```

## Controls

No interactive controls - animations run automatically.
