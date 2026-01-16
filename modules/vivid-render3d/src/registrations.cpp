// Operator registrations for header-only classes
// This file provides REGISTER calls for classes defined entirely in headers

#include <vivid/render3d/camera_operator.h>
#include <vivid/render3d/light_operators.h>
#include <vivid/render3d/primitives.h>
#include <vivid/render3d/sweep.h>
#include <vivid/render3d/boolean.h>
#include <vivid/operator_registry.h>

namespace vivid::render3d {

// Camera
REGISTER_OPERATOR(CameraOperator, "3D Camera", "Perspective camera with orbit and position controls", false);

// Lights
REGISTER_OPERATOR(DirectionalLight, "3D Lighting", "Infinite distance directional light (sun)", false);
REGISTER_OPERATOR(PointLight, "3D Lighting", "Omnidirectional point light with falloff", false);
REGISTER_OPERATOR(SpotLight, "3D Lighting", "Cone-shaped spotlight with falloff", false);

// Primitive geometry generators
REGISTER_OPERATOR(Box, "3D Primitives", "Box/cube mesh generator", false);
REGISTER_OPERATOR(Sphere, "3D Primitives", "UV sphere mesh generator", false);
REGISTER_OPERATOR(Cylinder, "3D Primitives", "Cylinder mesh generator", false);
REGISTER_OPERATOR(Cone, "3D Primitives", "Cone mesh generator", false);
REGISTER_OPERATOR(Torus, "3D Primitives", "Torus (donut) mesh generator", false);
REGISTER_OPERATOR(Plane, "3D Primitives", "Flat plane mesh generator", false);
REGISTER_OPERATOR(Sweep, "3D Primitives", "Sweep profile along path (helix, tube, ring)", false);

// CSG (Constructive Solid Geometry)
REGISTER_OPERATOR(Boolean, "3D CSG", "CSG boolean operations (union, subtract, intersect)", true);

} // namespace vivid::render3d
