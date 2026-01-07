// Operator registrations for header-only classes
// This file provides REGISTER calls for classes defined entirely in headers

#include <vivid/render3d/camera_operator.h>
#include <vivid/render3d/light_operators.h>
#include <vivid/render3d/primitives.h>
#include <vivid/render3d/boolean.h>
#include <vivid/operator_registry.h>

namespace vivid::render3d {

// Camera
REGISTER(CameraOperator);

// Lights
REGISTER(DirectionalLight);
REGISTER(PointLight);
REGISTER(SpotLight);

// Primitive geometry generators
REGISTER(Box);
REGISTER(Sphere);
REGISTER(Cylinder);
REGISTER(Cone);
REGISTER(Torus);
REGISTER(Plane);

// CSG (Constructive Solid Geometry)
REGISTER(Boolean);

} // namespace vivid::render3d
