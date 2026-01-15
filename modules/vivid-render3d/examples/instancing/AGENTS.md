# Instancing

GPU instancing for rendering thousands of objects efficiently.

## Operators Used

- **InstancedRender3D** - GPU-instanced mesh rendering
- **Box** - Base mesh to instance
- **CameraOperator** - 3D camera
- **DirectionalLight** - Scene lighting
- **Bloom** - Post-processing

## Key Concepts

### Basic Setup
```cpp
auto& box = chain.add<Box>("box");
auto& cam = chain.add<CameraOperator>("camera");
auto& sun = chain.add<DirectionalLight>("sun");

auto& inst = chain.add<InstancedRender3D>("inst");
inst.setMesh(&box);
inst.setCameraInput(&cam);
inst.setLightInput(&sun);
```

### Instance3D Structure
```cpp
struct Instance3D {
    glm::mat4 transform;  // World transform
    glm::vec4 color;      // Instance color
    float metallic;       // PBR metallic
    float roughness;      // PBR roughness
};
```

### Creating Instances
```cpp
std::vector<Instance3D> instances(1000);

for (int i = 0; i < 1000; i++) {
    Instance3D& inst = instances[i];
    
    // Position
    inst.transform = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(x, y, z)
    );
    
    // Rotation
    inst.transform = glm::rotate(
        inst.transform, angle,
        glm::vec3(0, 1, 0)
    );
    
    // Scale
    inst.transform = glm::scale(
        inst.transform,
        glm::vec3(0.5f)
    );
    
    // Color
    inst.color = glm::vec4(1.0f, 0.5f, 0.2f, 1.0f);
    
    // Material
    inst.metallic = 0.3f;
    inst.roughness = 0.6f;
}

instanced.setInstances(instances);
```

### Animating Instances
Update transforms each frame:
```cpp
for (int i = 0; i < instances.size(); i++) {
    glm::mat4 t = glm::translate(glm::mat4(1.0f), positions[i]);
    t = glm::rotate(t, ctx.time() + i * 0.1f, glm::vec3(0, 1, 0));
    instances[i].transform = t;
}
instanced.setInstances(instances);
```

## Performance

- **500-1000 instances** - Smooth on most GPUs
- **5000+ instances** - May need LOD or culling
- All instances use same mesh = single draw call
- Transform buffer updated per frame

## Use Cases

- Forests (trees, grass)
- Asteroid fields
- Crowds
- Debris
- Procedural cities

## Controls

- **Mouse X** - Camera orbit angle
- **Mouse Y** - Camera height/distance
