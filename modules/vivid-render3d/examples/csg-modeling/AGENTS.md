# CSG Modeling

Constructive Solid Geometry for creating complex shapes from primitives.

## Operators Used

- **Boolean** - CSG operations (union, subtract, intersect)
- **Box, Sphere** - Primitive shapes
- **SceneComposer** - Combine meshes
- **Render3D** - PBR rendering

## Key Concepts

### Boolean Operations
```cpp
auto& csg = chain.add<Boolean>("csg");
csg.setInputA(&shape1);
csg.setInputB(&shape2);
csg.setOperation(BooleanOp::Subtract);  // Remove B from A
```

Operations:
- **BooleanOp::Union** - Combine shapes (A + B)
- **BooleanOp::Subtract** - Remove B from A (A - B)
- **BooleanOp::Intersect** - Keep only overlap (A ∩ B)

### Creating CSG Shapes
```cpp
// CSG inputs: create via chain.add<>() (NOT scene.add<>())
auto& box = chain.add<Box>("box");
box.size(1.5f, 1.5f, 1.5f);

auto& sphere = chain.add<Sphere>("sphere");
sphere.radius(1.0f);

// Boolean operation
auto& hollowCube = chain.add<Boolean>("hollowCube");
hollowCube.setInputA(&box);
hollowCube.setInputB(&sphere);
hollowCube.setOperation(BooleanOp::Subtract);

// Add result to scene with transform and color
scene.add(&hollowCube,
    glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0)),
    glm::vec4(0.4f, 0.8f, 1.0f, 1.0f));
```

### Common Patterns

#### Hollow Sphere
```cpp
auto& outer = chain.add<Sphere>("outer");
outer.radius(1.0f);

auto& inner = chain.add<Sphere>("inner");
inner.radius(0.85f);

auto& hollow = chain.add<Boolean>("hollow");
hollow.setInputA(&outer);
hollow.setInputB(&inner);
hollow.setOperation(BooleanOp::Subtract);
```

#### Rounded Cube
```cpp
auto& box = chain.add<Box>("box");
box.size(1.4f, 1.4f, 1.4f);

auto& sphere = chain.add<Sphere>("sphere");
sphere.radius(1.05f);

auto& rounded = chain.add<Boolean>("rounded");
rounded.setInputA(&box);
rounded.setInputB(&sphere);
rounded.setOperation(BooleanOp::Intersect);
```

### Animating CSG Results
CSG results animate via SceneComposer entry transforms:
```cpp
auto& scene = chain.get<SceneComposer>("scene");
auto& entries = scene.entries();

entries[0].transform = glm::translate(glm::mat4(1.0f), position) *
                      glm::rotate(glm::mat4(1.0f), angle, axis);
```

### Render3D Setup
```cpp
auto& render = chain.add<Render3D>("render");
render.setInput(&scene);
render.setCameraInput(&cam);
render.setLightInput(&sun);
render.setMetallic(0.2f);
render.setRoughness(0.6f);
render.setAmbient(0.25f);
```

## Performance Notes

- CSG is computed on CPU using CGAL
- Complex shapes may take time to compute
- Results are cached until inputs change
- Use lower segment counts for performance

## Controls

- **Mouse X** - Camera orbit angle
- **Mouse Y** - Camera height/distance
