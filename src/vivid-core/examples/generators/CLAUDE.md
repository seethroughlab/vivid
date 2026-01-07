# Generators

Demonstrates all core generator operators in a 2x3 grid layout.

## Operators Used

- **SolidColor** - Flat color fill with animated hue
- **Gradient** - Linear gradient with rotating angle
- **Ramp** - Animated HSV rainbow gradient
- **Shape** - SDF shapes (Circle, Rectangle, Polygon)
- **LFO** - Low frequency oscillator for parameter modulation
- **Canvas** - Grid layout and text labels

## Key Concepts

### Generators vs Effects
Generators create content from nothing (no input required). Effects transform existing textures.

### LFO Modulation
LFOs output values between -1 and 1, useful for animating parameters:
```cpp
auto& lfo = chain.add<LFO>("lfo");
lfo.frequency = 0.5f;
lfo.waveform(LFOWaveform::Sine);
// In update():
float value = lfo.value();  // -1 to 1
```

### Shape Types
```cpp
auto& shape = chain.add<Shape>("shape");
shape.type(ShapeType::Circle);      // Circle
shape.type(ShapeType::Rectangle);   // Rectangle
shape.type(ShapeType::Polygon);     // Polygon with N sides
shape.type(ShapeType::Line);        // Line segment

// Common properties
shape.size.set(0.3f, 0.3f);         // Width/height
shape.color.set(1.0f, 0.5f, 0.2f, 1.0f);  // RGBA
shape.position.set(0.0f, 0.0f);     // Center position (-0.5 to 0.5)
shape.rotation = 0.5f;              // Radians
shape.sides = 6;                    // For Polygon type
shape.cornerRadius = 0.05f;         // For Rectangle
shape.softness = 0.02f;             // Edge softness
```

### Solid Color
```cpp
auto& solid = chain.add<SolidColor>("solid");
solid.color.set(0.1f, 0.2f, 0.8f, 1.0f);  // RGBA
```

### Gradient
```cpp
auto& gradient = chain.add<Gradient>("gradient");
gradient.colorA.set(1.0f, 0.0f, 0.0f, 1.0f);  // Start color
gradient.colorB.set(0.0f, 0.0f, 1.0f, 1.0f);  // End color
gradient.angle = 0.785f;  // 45 degrees in radians
```

### Ramp (Animated HSV Gradient)
```cpp
auto& ramp = chain.add<Ramp>("ramp");
ramp.hueSpeed = 0.2f;      // How fast the rainbow moves
ramp.saturation = 0.8f;
ramp.brightness = 0.9f;
```

## Controls

No interactive controls - animations run automatically.
