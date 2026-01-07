// Operator registrations for header-only classes
// This file provides REGISTER_OPERATOR calls for classes defined entirely in headers

#include <vivid/render3d/camera_operator.h>
#include <vivid/render3d/light_operators.h>
#include <vivid/render3d/primitives.h>
#include <vivid/render3d/boolean.h>
#include <vivid/operator_registry.h>

namespace vivid::render3d {

// Camera
REGISTER_MODULE_OPERATOR(CameraOperator, "3D Camera", "Perspective camera with orbit and position controls", false, "vivid-render3d");

// Lights
REGISTER_MODULE_OPERATOR_EX(DirectionalLight, "3D Lighting", "Infinite distance directional light (sun)", false, OutputKind::Light, "vivid-render3d");

REGISTER_MODULE_OPERATOR_EX(PointLight, "3D Lighting", "Omnidirectional point light with falloff", false, OutputKind::Light, "vivid-render3d");

REGISTER_MODULE_OPERATOR_EX(SpotLight, "3D Lighting", "Cone-shaped spotlight with falloff", false, OutputKind::Light, "vivid-render3d");

// Primitive geometry generators
REGISTER_MODULE_OPERATOR_EX(Box, "3D Primitives", "Box/cube mesh generator", false, OutputKind::Geometry, "vivid-render3d");

REGISTER_MODULE_OPERATOR_EX(Sphere, "3D Primitives", "UV sphere mesh generator", false, OutputKind::Geometry, "vivid-render3d");

REGISTER_MODULE_OPERATOR_EX(Cylinder, "3D Primitives", "Cylinder mesh generator", false, OutputKind::Geometry, "vivid-render3d");

REGISTER_MODULE_OPERATOR_EX(Cone, "3D Primitives", "Cone mesh generator", false, OutputKind::Geometry, "vivid-render3d");

REGISTER_MODULE_OPERATOR_EX(Torus, "3D Primitives", "Torus (donut) mesh generator", false, OutputKind::Geometry, "vivid-render3d");

REGISTER_MODULE_OPERATOR_EX(Plane, "3D Primitives", "Flat plane mesh generator", false, OutputKind::Geometry, "vivid-render3d");

// CSG (Constructive Solid Geometry)
REGISTER_MODULE_OPERATOR_EX(Boolean, "3D CSG", "CSG boolean operations (union, subtract, intersect)", true, OutputKind::Geometry, "vivid-render3d");

} // namespace vivid::render3d
