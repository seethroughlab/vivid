// Operator registrations for header-only classes
// This file provides REGISTER_OPERATOR_FULL calls for classes defined entirely in headers

#include <vivid/render3d/camera_operator.h>
#include <vivid/render3d/light_operators.h>
#include <vivid/render3d/primitives.h>
#include <vivid/render3d/boolean.h>
#include <vivid/operator_registry.h>

namespace vivid::render3d {

// Camera
REGISTER_OPERATOR_FULL(CameraOperator, "3D Camera", "Perspective camera with orbit and position controls", false)
    .related({"Render3D", "InstancedRender3D", "DirectionalLight", "PointLight", "SpotLight"})
    .limitations({"One camera per Render3D operator"})
    .examples({"modules/vivid-render3d/examples/3d-basics"})
    .api({".position(x,y,z)", ".target(x,y,z)", ".orbitCenter(x,y,z)", ".distance(float)", ".azimuth(float rad)", ".elevation(float rad)", ".fov(float deg)", ".nearPlane(float)", ".farPlane(float)", ".orthoSize(float)", ".perspective()", ".orthographic()"});

// Lights
REGISTER_OPERATOR_FULL_EX(DirectionalLight, "3D Lighting", "Infinite distance directional light (sun)", false, OutputKind::Light)
    .related({"PointLight", "SpotLight", "Render3D", "CameraOperator", "IBLEnvironment"})
    .limitations({"Max 4 shadow-casting lights per scene", "Shadow map resolution affects quality"})
    .examples({"modules/vivid-render3d/examples/shadow-test"})
    .api({".direction(x,y,z)", ".color(r,g,b)", ".castShadow(bool)", ".shadowBias(float)", ".shadowAutoUpdate(bool)", ".shadowNeedsUpdate()"});

REGISTER_OPERATOR_FULL_EX(PointLight, "3D Lighting", "Omnidirectional point light with falloff", false, OutputKind::Light)
    .related({"DirectionalLight", "SpotLight", "Render3D", "CameraOperator"})
    .limitations({"Point shadows use 6 texture lookups (cube map)", "Max 4 shadow-casting lights"})
    .examples({"modules/vivid-render3d/examples/shadow-test"})
    .api({".position(x,y,z)", ".color(r,g,b)", ".castShadow(bool)", ".shadowBias(float)", ".shadowAutoUpdate(bool)", ".shadowNeedsUpdate()"});

REGISTER_OPERATOR_FULL_EX(SpotLight, "3D Lighting", "Cone-shaped spotlight with falloff", false, OutputKind::Light)
    .related({"DirectionalLight", "PointLight", "Render3D", "CameraOperator"})
    .limitations({"Max 4 shadow-casting lights per scene"})
    .examples({"modules/vivid-render3d/examples/shadow-test"})
    .api({".position(x,y,z)", ".direction(x,y,z)", ".color(r,g,b)", ".castShadow(bool)", ".shadowBias(float)", ".shadowAutoUpdate(bool)", ".shadowNeedsUpdate()"});

// Primitive geometry generators
REGISTER_OPERATOR_FULL_EX(Box, "3D Primitives", "Box/cube mesh generator", false, OutputKind::Geometry)
    .related({"Sphere", "Cylinder", "Cone", "Torus", "Plane", "Boolean", "SceneComposer", "Render3D"})
    .examples({"modules/vivid-render3d/examples/3d-basics", "modules/vivid-render3d/examples/geometry-showcase"})
    .api({".size(w,h,d)", ".size(float)"});

REGISTER_OPERATOR_FULL_EX(Sphere, "3D Primitives", "UV sphere mesh generator", false, OutputKind::Geometry)
    .related({"Box", "Cylinder", "Cone", "Torus", "Plane", "Boolean", "SceneComposer", "Render3D"})
    .limitations({"UV seam visible at poles with some textures"})
    .examples({"modules/vivid-render3d/examples/3d-basics", "modules/vivid-render3d/examples/globe"})
    .api({".radius(float)", ".segments(int)", ".noiseDisplacement(amplitude, frequency, octaves)", ".noiseTime(float)"});

REGISTER_OPERATOR_FULL_EX(Cylinder, "3D Primitives", "Cylinder mesh generator", false, OutputKind::Geometry)
    .related({"Box", "Sphere", "Cone", "Torus", "Plane", "Boolean", "SceneComposer", "Render3D"})
    .examples({"modules/vivid-render3d/examples/geometry-showcase"})
    .api({".radius(float)", ".height(float)", ".segments(int)"});

REGISTER_OPERATOR_FULL_EX(Cone, "3D Primitives", "Cone mesh generator", false, OutputKind::Geometry)
    .related({"Box", "Sphere", "Cylinder", "Torus", "Plane", "Boolean", "SceneComposer", "Render3D"})
    .examples({"modules/vivid-render3d/examples/geometry-showcase"})
    .api({".radius(float)", ".height(float)", ".segments(int)"});

REGISTER_OPERATOR_FULL_EX(Torus, "3D Primitives", "Torus (donut) mesh generator", false, OutputKind::Geometry)
    .related({"Box", "Sphere", "Cylinder", "Cone", "Plane", "Boolean", "SceneComposer", "Render3D"})
    .examples({"modules/vivid-render3d/examples/geometry-showcase"})
    .api({".outerRadius(float)", ".innerRadius(float)", ".segments(int)", ".rings(int)"});

REGISTER_OPERATOR_FULL_EX(Plane, "3D Primitives", "Flat plane mesh generator", false, OutputKind::Geometry)
    .related({"Box", "Sphere", "Cylinder", "Cone", "Torus", "Boolean", "SceneComposer", "Render3D"})
    .limitations({"Single-sided by default"})
    .examples({"modules/vivid-render3d/examples/3d-basics"})
    .api({".size(w,h)", ".subdivisions(x,y)"});

// CSG (Constructive Solid Geometry)
REGISTER_OPERATOR_FULL_EX(Boolean, "3D CSG", "CSG boolean operations (union, subtract, intersect)", true, OutputKind::Geometry)
    .related({"Box", "Sphere", "Cylinder", "Cone", "Torus", "Plane", "GLTFLoader", "SceneComposer"})
    .limitations({"CPU-based mesh processing", "Complex operations can be slow", "Requires watertight meshes"})
    .examples({"modules/vivid-render3d/examples/geometry-showcase"})
    .api({".setInputA(MeshOperator*)", ".setInputB(MeshOperator*)", ".setOperation(BooleanOp::Union|Subtract|Intersect)"});

} // namespace vivid::render3d
