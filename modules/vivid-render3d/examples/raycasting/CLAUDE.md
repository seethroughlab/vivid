# 3D Raycasting / Picking

Demonstrates screen-to-world ray conversion and object picking in 3D scenes with animated objects.

## Features Demonstrated

- **Screen-to-world ray conversion** - Unproject screen coordinates to world-space ray
- **Ray-sphere intersection** - Test if ray hits a sphere
- **Animated objects** - Spheres bob and rotate to prove raycasting tracks moving targets
- **3D outline effect** - Slightly larger "ghost" spheres provide selection highlights
- **Hover detection** - Highlight objects under cursor
- **Click selection** - Select/deselect objects with mouse
- **Orbital camera control** - Right-drag to orbit

## Key Concepts

### Coordinate System Bridge

2D screen coordinates (Y-down) must be converted to 3D NDC (Y-up) for ray casting:

```cpp
// Screen (0-1, Y-down) to NDC (-1 to +1, Y-up for 3D)
float ndcX = screenX * 2.0f - 1.0f;
float ndcY = 1.0f - screenY * 2.0f;  // Flip Y for 3D coordinate system
```

### Screen-to-World Ray

```cpp
void screenToWorldRay(float screenX, float screenY, const Camera3D& camera,
                      glm::vec3& rayOrigin, glm::vec3& rayDir) {
    // Convert screen (0-1, Y-down) to NDC (-1 to +1, Y-up)
    float ndcX = screenX * 2.0f - 1.0f;
    float ndcY = 1.0f - screenY * 2.0f;

    // Get inverse view-projection matrix
    glm::mat4 invVP = glm::inverse(camera.viewProjectionMatrix());

    // Unproject near and far points
    // IMPORTANT: glm::perspective() uses OpenGL conventions with Z in [-1, 1]
    // WebGPU uses [0, 1] but we're unprojecting through the GLM matrix
    glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPoint = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

    // Perspective divide
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    // Ray from near to far
    rayOrigin = glm::vec3(nearPoint);
    rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));
}
```

### Ray-Sphere Intersection

```cpp
bool raySphereIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                        const glm::vec3& center, float radius, float& hitDist) {
    glm::vec3 oc = rayOrigin - center;
    float a = glm::dot(rayDir, rayDir);
    float b = 2.0f * glm::dot(oc, rayDir);
    float c = glm::dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4.0f * a * c;

    if (discriminant < 0.0f) return false;

    float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
    if (t < 0.0f) t = (-b + std::sqrt(discriminant)) / (2.0f * a);
    if (t < 0.0f) return false;

    hitDist = t;
    return true;
}
```

### Using CameraOperator for Picking

```cpp
// Process camera to ensure matrices are current
camera.process(ctx);

// Get camera reference for ray casting
const Camera3D& cam3d = camera.outputCamera();

// IMPORTANT: Set aspect ratio manually (CameraOperator doesn't auto-set this)
const_cast<Camera3D&>(cam3d).aspect(float(ctx.width()) / ctx.height());

// Get mouse in normalized coords (0-1, Y-down)
glm::vec2 mouseNorm = ctx.mouseNorm();

// Cast ray
glm::vec3 rayOrigin, rayDir;
screenToWorldRay(mouseNorm.x, mouseNorm.y, cam3d, rayOrigin, rayDir);
```

### Animating Objects for Pick Testing

Update object positions each frame and use current positions for picking:

```cpp
std::vector<glm::vec3> currentPositions(sphereCount);

for (size_t i = 0; i < sphereCount; ++i) {
    // Bobbing animation
    float bob = std::sin(time * 2.0f + phaseOffset[i]) * 0.2f;
    glm::vec3 pos = basePosition[i];
    pos.y += bob;
    currentPositions[i] = pos;

    // Update scene entry transform
    entries[i].transform = glm::translate(glm::mat4(1.0f), pos);
}

// Pick using current animated positions
int hovered = pickSphere(mouseNorm.x, mouseNorm.y, camera, currentPositions);
```

### 3D Outline Effect

Use slightly larger "ghost" spheres for selection highlights:

```cpp
// In setup: Create outline spheres (8% larger)
float outlineRadius = sphereRadius * 1.08f;
auto& outline = scene.add<Sphere>("outline",
    transform, glm::vec4(1, 1, 1, 0));  // Start invisible
outline.radius(outlineRadius);

// In update: Show/hide based on selection state
if (isSelected) {
    entries[outlineIdx].color = glm::vec4(1, 1, 1, 0.5f);  // White glow
} else if (isHovered) {
    entries[outlineIdx].color = glm::vec4(0, 1, 1, 0.4f);  // Cyan glow
} else {
    entries[outlineIdx].color = glm::vec4(0, 0, 0, 0);     // Invisible
}

// Match outline transform to main sphere
entries[outlineIdx].transform = glm::scale(mainTransform, glm::vec3(1.08f));
```

## Ray-Primitive Intersections

### Ray-Plane

```cpp
bool rayPlaneIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                       const glm::vec3& planeNormal, float planeD, float& t) {
    float denom = glm::dot(planeNormal, rayDir);
    if (std::abs(denom) < 1e-6f) return false;  // Parallel

    t = -(glm::dot(planeNormal, rayOrigin) + planeD) / denom;
    return t >= 0.0f;
}
```

### Ray-AABB (Axis-Aligned Bounding Box)

```cpp
bool rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                      const glm::vec3& boxMin, const glm::vec3& boxMax, float& t) {
    glm::vec3 invDir = 1.0f / rayDir;
    glm::vec3 t0 = (boxMin - rayOrigin) * invDir;
    glm::vec3 t1 = (boxMax - rayOrigin) * invDir;

    glm::vec3 tmin = glm::min(t0, t1);
    glm::vec3 tmax = glm::max(t0, t1);

    float tNear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
    float tFar = glm::min(glm::min(tmax.x, tmax.y), tmax.z);

    if (tNear > tFar || tFar < 0.0f) return false;

    t = tNear >= 0.0f ? tNear : tFar;
    return true;
}
```

## Coordinate System Summary

| System | X Range | Y Range | Y Direction | Origin |
|--------|---------|---------|-------------|--------|
| `ctx.mouseNorm()` | 0 to 1 | 0 to 1 | Down | Top-left |
| NDC (3D) | -1 to +1 | -1 to +1 | Up | Center |
| World Space | unbounded | unbounded | Up | Scene origin |

The key transformation for picking is:
```
Screen (Y-down) -> NDC (Y-up) -> World Space (Y-up)
```

## Important Notes

1. **Process camera before picking**: Call `camera.process(ctx)` to ensure matrices are current
2. **Set aspect ratio**: CameraOperator doesn't auto-set aspect; do it manually
3. **Use current positions**: When objects animate, pick against animated positions
4. **NDC depth convention**: GLM uses OpenGL's [-1, 1] Z range, not WebGPU's [0, 1]

## Controls

- **Left-click**: Select/deselect sphere
- **Right-drag**: Orbit camera around scene
- **S**: Save snapshot to project directory
- Hover over spheres to see them highlight with cyan glow
- Selected spheres show white glow
- Spheres bob and rotate continuously to demonstrate tracking

## Taking Snapshots Programmatically

```cpp
// Simple: auto-generates filename (snapshot_1.png, snapshot_2.png, etc.)
if (ctx.key(GLFW_KEY_S).pressed) {
    ctx.snapshot();
}

// With custom filename:
ctx.snapshot("my_capture.png");

// Check if successful:
std::string path = ctx.snapshot();
if (!path.empty()) {
    printf("Saved: %s\n", path.c_str());
}
```
