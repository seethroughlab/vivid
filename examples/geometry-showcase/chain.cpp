// Geometry Showcase - Vivid Example
// Demonstrates all procedural geometry primitives and CSG operations

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Persistent state across hot-reloads
static Camera3D camera;
static Scene scene;

// All primitive meshes
static Mesh boxMesh;
static Mesh sphereMesh;
static Mesh cylinderMesh;
static Mesh coneMesh;
static Mesh torusMesh;
static Mesh planeMesh;

// CSG operation meshes
static Mesh csgUnionMesh;      // Box + Sphere
static Mesh csgSubtractMesh;   // Box - Sphere (hollow cube)
static Mesh csgIntersectMesh;  // Box & Sphere (rounded cube)

// Complex CSG example
static Mesh pipeMesh;          // Cylinder - smaller cylinder

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    scene.clear();

    // =========================================================================
    // BASIC PRIMITIVES (Top row)
    // =========================================================================

    // 1. Box - basic cube with flat shading
    auto boxBuilder = MeshBuilder::box(1.0f, 1.0f, 1.0f);
    boxBuilder.computeFlatNormals();
    boxMesh = boxBuilder.build();
    boxMesh.upload(ctx);

    // 2. Sphere - smooth sphere
    auto sphereBuilder = MeshBuilder::sphere(0.6f, 32);
    sphereMesh = sphereBuilder.build();
    sphereMesh.upload(ctx);

    // 3. Cylinder
    auto cylinderBuilder = MeshBuilder::cylinder(0.5f, 1.2f, 24);
    cylinderBuilder.computeFlatNormals();
    cylinderMesh = cylinderBuilder.build();
    cylinderMesh.upload(ctx);

    // 4. Cone
    auto coneBuilder = MeshBuilder::cone(0.6f, 1.2f, 24);
    coneBuilder.computeFlatNormals();
    coneMesh = coneBuilder.build();
    coneMesh.upload(ctx);

    // 5. Torus (donut)
    auto torusBuilder = MeshBuilder::torus(0.5f, 0.2f, 32, 16);
    torusMesh = torusBuilder.build();
    torusMesh.upload(ctx);

    // 6. Plane (subdivided for visibility)
    auto planeBuilder = MeshBuilder::plane(1.5f, 1.5f, 4, 4);
    planeBuilder.computeFlatNormals();
    planeMesh = planeBuilder.build();
    planeMesh.upload(ctx);

    // =========================================================================
    // CSG OPERATIONS (Bottom row)
    // =========================================================================

    // CSG Union: Box + Sphere merged
    auto unionBox = MeshBuilder::box(0.8f, 0.8f, 0.8f);
    auto unionSphere = MeshBuilder::sphere(0.6f, 24);
    unionSphere.translate(glm::vec3(0.4f, 0.4f, 0.4f));
    unionBox.add(unionSphere);
    unionBox.computeFlatNormals();
    csgUnionMesh = unionBox.build();
    csgUnionMesh.upload(ctx);

    // CSG Subtract: Hollow cube (box with spherical cavity)
    auto subtractBox = MeshBuilder::box(1.2f, 1.2f, 1.2f);
    auto subtractSphere = MeshBuilder::sphere(0.8f, 24);
    subtractBox.subtract(subtractSphere);
    subtractBox.computeFlatNormals();
    csgSubtractMesh = subtractBox.build();
    csgSubtractMesh.upload(ctx);

    // CSG Intersect: Rounded cube (intersection of box and sphere)
    auto intersectBox = MeshBuilder::box(1.0f, 1.0f, 1.0f);
    auto intersectSphere = MeshBuilder::sphere(0.75f, 24);
    intersectBox.intersect(intersectSphere);
    intersectBox.computeFlatNormals();
    csgIntersectMesh = intersectBox.build();
    csgIntersectMesh.upload(ctx);

    // Complex CSG: Pipe (cylinder with hole through center)
    auto outerCylinder = MeshBuilder::cylinder(0.5f, 1.5f, 32);
    auto innerCylinder = MeshBuilder::cylinder(0.3f, 1.8f, 32);
    outerCylinder.subtract(innerCylinder);
    outerCylinder.computeFlatNormals();
    pipeMesh = outerCylinder.build();
    pipeMesh.upload(ctx);

    // =========================================================================
    // SCENE LAYOUT
    // =========================================================================

    float spacing = 2.2f;
    float topRowY = 1.5f;
    float bottomRowY = -1.5f;

    // Top row: Basic primitives
    scene.add(boxMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 2.5f, topRowY, 0.0f)),
              glm::vec4(0.9f, 0.3f, 0.3f, 1.0f));  // Red box

    scene.add(sphereMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 1.5f, topRowY, 0.0f)),
              glm::vec4(0.3f, 0.9f, 0.4f, 1.0f));  // Green sphere

    scene.add(cylinderMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 0.5f, topRowY, 0.0f)),
              glm::vec4(0.3f, 0.5f, 0.9f, 1.0f));  // Blue cylinder

    scene.add(coneMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 0.5f, topRowY, 0.0f)),
              glm::vec4(0.9f, 0.7f, 0.2f, 1.0f));  // Orange cone

    scene.add(torusMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 1.5f, topRowY, 0.0f)),
              glm::vec4(0.8f, 0.3f, 0.8f, 1.0f));  // Purple torus

    scene.add(planeMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 2.5f, topRowY, 0.0f)) *
              glm::rotate(glm::mat4(1.0f), glm::radians(-30.0f), glm::vec3(1, 0, 0)),
              glm::vec4(0.2f, 0.8f, 0.8f, 1.0f));  // Cyan plane

    // Bottom row: CSG operations
    scene.add(csgUnionMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 1.5f, bottomRowY, 0.0f)),
              glm::vec4(1.0f, 0.6f, 0.3f, 1.0f));  // Union (orange)

    scene.add(csgSubtractMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(-spacing * 0.5f, bottomRowY, 0.0f)),
              glm::vec4(0.4f, 0.8f, 1.0f, 1.0f));  // Subtract (light blue)

    scene.add(csgIntersectMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 0.5f, bottomRowY, 0.0f)),
              glm::vec4(0.8f, 1.0f, 0.4f, 1.0f));  // Intersect (lime)

    scene.add(pipeMesh,
              glm::translate(glm::mat4(1.0f), glm::vec3(spacing * 1.5f, bottomRowY, 0.0f)),
              glm::vec4(0.9f, 0.5f, 0.7f, 1.0f));  // Pipe (pink)

    // =========================================================================
    // CAMERA & RENDERER
    // =========================================================================

    camera.lookAt(glm::vec3(0, 1, 12), glm::vec3(0, 0, 0))
          .fov(50.0f)
          .nearPlane(0.1f)
          .farPlane(100.0f);

    auto& renderer = chain.add<Render3D>("render3d");
    renderer.scene(scene)
            .camera(camera)
            .shadingMode(ShadingMode::Flat)
            .lightDirection(glm::normalize(glm::vec3(1, 2, 1)))
            .lightColor(glm::vec3(1, 1, 1))
            .ambient(0.2f)
            .clearColor(0.08f, 0.08f, 0.12f)
            .resolution(1280, 720);

    chain.output("render3d");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Gentle camera orbit
    float distance = 14.0f;
    float azimuth = time * 0.15f;
    float elevation = 0.25f;
    camera.orbit(distance, azimuth, elevation);

    auto& renderer = chain.get<Render3D>("render3d");
    renderer.camera(camera);

    auto& objects = scene.objects();

    // Animate each object with distinct motion

    // Top row: Basic primitives
    // Box: Rotate around Y
    objects[0].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.2f * 2.5f, 1.5f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0));

    // Sphere: Pulse scale
    float sphereScale = 1.0f + 0.1f * std::sin(time * 2.0f);
    objects[1].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.2f * 1.5f, 1.5f, 0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(sphereScale));

    // Cylinder: Rotate around local Y
    objects[2].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.2f * 0.5f, 1.5f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.7f, glm::vec3(0, 1, 0));

    // Cone: Wobble rotation
    objects[3].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.2f * 0.5f, 1.5f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), 0.3f * std::sin(time * 1.5f), glm::vec3(1, 0, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 0));

    // Torus: Spin around multiple axes
    objects[4].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.2f * 1.5f, 1.5f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.6f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(1, 0, 0));

    // Plane: Rotate to show both sides
    objects[5].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.2f * 2.5f, 1.5f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(-30.0f), glm::vec3(1, 0, 0));

    // Bottom row: CSG operations
    // Union: Tumble rotation
    objects[6].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.2f * 1.5f, -1.5f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.4f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.25f, glm::vec3(1, 0, 0));

    // Subtract (hollow cube): Slow rotation to show interior
    objects[7].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.2f * 0.5f, -1.5f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.2f, glm::vec3(1, 0, 0));

    // Intersect (rounded cube): Gentle rotation
    objects[8].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.2f * 0.5f, -1.5f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.35f, glm::vec3(0, 1, 0));

    // Pipe: Rotate to show hollow center
    objects[9].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.2f * 1.5f, -1.5f, 0.0f)) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
                          glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 0, 1));
}

VIVID_CHAIN(setup, update)
