// Diligent Engine Integration Test
//
// This example validates that Diligent Engine is properly integrated.
// Currently, Diligent is compiled alongside the existing WebGPU backend.
//
// The DiligentBackend class is ready for use but not yet connected to the
// rendering pipeline. The next steps are:
// 1. Create DiligentPBR renderer using DiligentFX
// 2. Route Context::render3D calls through DiligentBackend
// 3. Convert shaders from WGSL to HLSL
//
// For now, this example just renders using the existing WebGPU path
// to verify that Diligent compilation doesn't break the existing code.

#include <vivid/vivid.h>
#include <iostream>

using namespace vivid;

static Mesh3D boxMesh;
static Texture output;

void setup(Chain& chain) {
    chain.setOutput("out");

#ifdef VIVID_USE_DILIGENT
    std::cout << "Diligent Engine: ENABLED (compiled in)" << std::endl;
#else
    std::cout << "Diligent Engine: DISABLED (not compiled)" << std::endl;
#endif
}

void update(Chain& chain, Context& ctx) {
    static bool initialized = false;
    if (!initialized) {
        boxMesh = ctx.createCube();
        output = ctx.createTexture();
        initialized = true;
    }

    float t = ctx.time();

    // Camera
    Camera3D camera;
    camera.position = glm::vec3(
        std::cos(t * 0.5f) * 4.0f,
        2.5f,
        std::sin(t * 0.5f) * 4.0f
    );
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    camera.fov = 45.0f;

    // Light
    Light sun = Light::directional(
        glm::vec3(-0.5f, -1.0f, -0.3f),
        glm::vec3(1.0f),
        1.0f
    );

    SceneLighting lighting;
    lighting.ambientColor = glm::vec3(0.2f);
    lighting.ambientIntensity = 0.2f;
    lighting.addLight(sun);

    // Material
    PBRMaterial mat;
    mat.albedo = glm::vec3(0.2f, 0.5f, 0.9f);
    mat.roughness = 0.4f;
    mat.metallic = 0.0f;

    // Transform - rotating cube
    glm::mat4 transform = glm::rotate(
        glm::mat4(1.0f),
        t * 0.5f,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    // Render (still using WebGPU path for now)
    ctx.render3D(boxMesh, camera, transform, mat, lighting, output,
                 glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));

    // Output
    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
