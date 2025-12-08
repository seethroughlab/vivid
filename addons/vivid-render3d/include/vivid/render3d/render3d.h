#pragma once

// Vivid Render3D - 3D Rendering Addon
// Procedural geometry, CSG operations, and flat/PBR shading

// Core types
#include <vivid/render3d/mesh.h>
#include <vivid/render3d/mesh_builder.h>
#include <vivid/render3d/camera.h>
#include <vivid/render3d/scene.h>
#include <vivid/render3d/material.h>
#include <vivid/render3d/textured_material.h>
#include <vivid/render3d/renderer.h>

// Mesh operators (node-based workflow)
#include <vivid/render3d/mesh_operator.h>
#include <vivid/render3d/static_mesh.h>
#include <vivid/render3d/primitives.h>
#include <vivid/render3d/boolean.h>
#include <vivid/render3d/scene_composer.h>
#include <vivid/render3d/gltf_loader.h>

// Camera and light operators (node-based workflow)
#include <vivid/render3d/camera_operator.h>
#include <vivid/render3d/light_operators.h>

// Instanced rendering
#include <vivid/render3d/instanced_render3d.h>
