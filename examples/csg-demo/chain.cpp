// CSG Demo Example
// Demonstrates Constructive Solid Geometry operations

#include <vivid/vivid.h>
#include <vivid/operators.h>
#include <vivid/mesh.h>
#include <vivid/csg/csg.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>

using namespace vivid;
using namespace vivid::csg;

// Static state
static std::unique_ptr<Render3D> render3d;
static std::unique_ptr<Output> output;
static Mesh csgMesh;
static int meshIndex = -1;
static float rotation = 0.0f;
static bool initialized = false;

void setup(Context& ctx) {
    std::cout << "[CSG Demo] Creating CSG model..." << std::endl;

    // Create a complex CSG shape: box with spherical holes and cylindrical cutouts

    // Start with a box
    Solid base = Solid::box(2.0f, 1.5f, 1.5f);

    // Subtract spheres from corners to create rounded effect
    Solid cornerSphere = Solid::sphere(0.4f, 16);
    base = base - cornerSphere.translate(-0.8f, 0.5f, 0.5f);
    base = base - cornerSphere.translate(0.8f, 0.5f, 0.5f);
    base = base - cornerSphere.translate(-0.8f, -0.5f, 0.5f);
    base = base - cornerSphere.translate(0.8f, -0.5f, 0.5f);

    // Add a cylinder through the middle
    Solid cylinder = Solid::cylinder(0.3f, 3.0f, 24);
    base = base - cylinder.rotateZ(3.14159f / 2.0f);  // Rotate to go through X axis

    // Add some spheres on top as decoration
    Solid topSphere = Solid::sphere(0.25f, 16);
    base = base + topSphere.translate(0.0f, 0.9f, 0.0f);
    base = base + topSphere.translate(-0.5f, 0.9f, 0.0f);
    base = base + topSphere.translate(0.5f, 0.9f, 0.0f);

    // Create a torus intersection
    Solid torus = Solid::torus(0.6f, 0.15f, 24, 12);
    base = base + torus.translate(0.0f, -0.6f, 0.0f);

    std::cout << "[CSG Demo] Triangle count: " << base.triangleCount() << std::endl;

    // Convert to mesh
    MeshData meshData = base.toMeshData();
    MeshUtils::calculateTangents(meshData);

    if (!csgMesh.create(ctx.device(), meshData)) {
        std::cerr << "[CSG Demo] Failed to create mesh" << std::endl;
        return;
    }

    // Create operators
    render3d = std::make_unique<Render3D>();
    output = std::make_unique<Output>();
    output->setInput(render3d.get());

    render3d->init(ctx);
    output->init(ctx);

    // Add CSG mesh to scene
    meshIndex = render3d->addObject(&csgMesh, glm::mat4(1.0f));

    // Setup camera
    render3d->camera().setOrbit(glm::vec3(0, 0, 0), 4.0f, 45.0f, 25.0f);

    // Scene settings
    render3d->backgroundColor(0.15f, 0.15f, 0.2f);
    render3d->ambientColor(0.4f, 0.4f, 0.45f);

    // Lighting
    render3d->clearLights();

    Light3D keyLight;
    keyLight.type = Light3D::Type::Directional;
    keyLight.direction = glm::normalize(glm::vec3(-0.5f, -0.8f, -0.5f));
    keyLight.color = glm::vec3(1.0f, 0.95f, 0.9f);
    keyLight.intensity = 2.5f;
    render3d->addLight(keyLight);

    Light3D fillLight;
    fillLight.type = Light3D::Type::Directional;
    fillLight.direction = glm::normalize(glm::vec3(0.7f, -0.3f, 0.5f));
    fillLight.color = glm::vec3(0.7f, 0.8f, 1.0f);
    fillLight.intensity = 1.2f;
    render3d->addLight(fillLight);

    initialized = true;
    std::cout << "[CSG Demo] Ready!" << std::endl;
}

void update(Context& ctx) {
    if (!initialized) return;

    // Rotate the object
    rotation += 0.5f * ctx.dt();

    if (auto* obj = render3d->getObject(meshIndex)) {
        obj->transform = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0, 1, 0));
        obj->color = glm::vec4(0.8f, 0.6f, 0.4f, 1.0f);  // Bronze-ish color
    }

    // Render
    render3d->process(ctx);
    output->process(ctx);
}

VIVID_CHAIN(setup, update)
