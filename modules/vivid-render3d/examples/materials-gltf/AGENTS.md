# Materials & GLTF Example

Demonstrates model loading and PBR material configuration.

## Operators Demonstrated

- **GLTFLoader** - Load 3D models from GLTF/GLB files with textures
- **SceneComposer** - Compose multiple geometries and materials into a scene
- **TexturedMaterial** - PBR material with color, metallic, roughness, and emissive

## Key Concepts

### GLTFLoader
Load 3D models with PBR textures:

```cpp
auto& model = chain.add<GLTFLoader>("model");
model.file("assets/model.glb");
model.loadTextures(true);        // Load embedded PBR textures
model.computeTangents(true);     // Required for normal mapping
model.scale = 1.0f;

// Check loading status
if (model.isLoaded()) {
    float radius = model.bounds().radius();
    glm::vec3 center = model.bounds().center();
}
```

### TexturedMaterial (Factor-Based)
Configure PBR materials with scalar factors:

```cpp
// Metallic material
auto& metal = chain.add<TexturedMaterial>("metal");
metal.baseColorFactor(0.95f, 0.64f, 0.54f, 1.0f);  // Copper tone
metal.metallicFactor(1.0f);          // Fully metallic
metal.roughnessFactor(0.3f);         // Smooth-ish

// Dielectric material
auto& plastic = chain.add<TexturedMaterial>("plastic");
plastic.baseColorFactor(0.2f, 0.6f, 0.9f, 1.0f);
plastic.metallicFactor(0.0f);        // Non-metallic
plastic.roughnessFactor(0.7f);       // Rough

// Emissive material (glowing)
auto& glow = chain.add<TexturedMaterial>("glow");
glow.emissiveFactor(0.2f, 1.0f, 0.5f);  // Green glow
glow.emissiveStrength(3.0f);             // Brightness multiplier
```

### TexturedMaterial (Texture Maps)
Load PBR texture maps from files:

```cpp
auto& mat = chain.add<TexturedMaterial>("pbr");
mat.baseColor("assets/albedo.png");       // Color map
mat.normal("assets/normal.png");          // Normal map
mat.metallic("assets/metallic.png");      // Metallic map
mat.roughness("assets/roughness.png");    // Roughness map
mat.ao("assets/ao.png");                  // Ambient occlusion
mat.emissive("assets/emissive.png");      // Emissive map
mat.normalScale(1.0f);                    // Normal map strength
```

### TexturedMaterial (Procedural Input)
Use operator output as texture:

```cpp
auto& noise = chain.add<Noise>("roughTex");
auto& mat = chain.add<TexturedMaterial>("mat");
mat.roughnessInput(&noise);               // Use noise as roughness map
mat.baseColorFactor(0.9f, 0.2f, 0.1f);   // Red base color
```

### SceneComposer Material Assignment
```cpp
auto& scene = SceneComposer::create(chain, "scene");
auto& sphere = scene.add<Sphere>("sphere");
scene.setEntryMaterial(0, &material);      // Assign by entry index
```

### Alpha Modes
```cpp
mat.alphaMode(AlphaMode::Opaque);   // No transparency (default)
mat.alphaMode(AlphaMode::Mask);     // Binary transparency
mat.alphaCutoff(0.5f);              // Cutoff for mask mode
mat.alphaMode(AlphaMode::Blend);    // Full alpha blending
mat.doubleSided(true);              // Render back faces
```

## Related Operators

- **Render3D** - Renders scene with PBR shading
- **IBLEnvironment** - Essential for metallic reflections
- **PointLight** / **SpotLight** - Dynamic lighting
