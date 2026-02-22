/**
 * Materials & GLTF Example
 *
 * Demonstrates: GLTFLoader, SceneComposer, TexturedMaterial
 *
 * Loads a GLTF model with textures and adds procedural materials
 * to scene objects. Shows PBR material configuration with color
 * factors, metallic/roughness, and emissive properties.
 */

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // GLTF Model Loading
    // =========================================================================

    // GLTFLoader: loads .glb/.gltf files with geometry and materials
    auto& model = chain.add<GLTFLoader>("helmet");
    model.file("assets/meshes/DamagedHelmet.glb");
    model.loadTextures(true);        // Load PBR texture maps
    model.computeTangents(true);     // Required for normal mapping
    model.scale = 1.0f;

    // =========================================================================
    // Custom Materials using TexturedMaterial
    // =========================================================================

    // Copper material: metallic with warm color
    auto& copperMat = chain.add<TexturedMaterial>("copper");
    copperMat.baseColorFactor(0.95f, 0.64f, 0.54f, 1.0f);  // Copper color
    copperMat.metallicFactor(1.0f);         // Fully metallic
    copperMat.roughnessFactor(0.3f);        // Slightly rough

    // Matte plastic material: non-metallic with bright color
    auto& plasticMat = chain.add<TexturedMaterial>("plastic");
    plasticMat.baseColorFactor(0.2f, 0.6f, 0.9f, 1.0f);  // Blue
    plasticMat.metallicFactor(0.0f);        // Non-metallic (dielectric)
    plasticMat.roughnessFactor(0.7f);       // Rough surface

    // Emissive material: glowing surface
    auto& glowMat = chain.add<TexturedMaterial>("glow");
    glowMat.baseColorFactor(0.1f, 0.1f, 0.1f, 1.0f);     // Dark base
    glowMat.metallicFactor(0.0f);
    glowMat.roughnessFactor(0.5f);
    glowMat.emissiveFactor(0.2f, 1.0f, 0.5f);             // Green glow
    glowMat.emissiveStrength(3.0f);                         // Bright emission

    // =========================================================================
    // Scene Composition
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Add GLTF model (uses its own embedded textures)
    scene.add(&model, glm::mat4(1.0f), glm::vec4(1.0f));

    // Ground plane with copper material
    auto& ground = scene.add<Plane>("ground",
        glm::translate(glm::mat4(1.0f), glm::vec3(0, -1.5f, 0)));
    ground.size(8.0f, 8.0f);
    scene.setEntryMaterial(1, &copperMat);

    // Sphere with plastic material
    auto& blueSphere = scene.add<Sphere>("blueSphere",
        glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.0f, 0.0f)));
    blueSphere.radius(0.5f);
    blueSphere.segments(32);
    scene.setEntryMaterial(2, &plasticMat);

    // Cube with emissive glow material
    auto& glowCube = scene.add<Box>("glowCube",
        glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f)));
    glowCube.size(0.8f, 0.8f, 0.8f);
    scene.setEntryMaterial(3, &glowMat);

    // =========================================================================
    // Lighting
    // =========================================================================

    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(-1.0f, -2.0f, -1.0f);
    sun.color(1.0f, 0.95f, 0.9f);
    sun.intensity = 2.0f;

    // IBL for environment reflections (essential for PBR metals)
    auto& ibl = chain.add<IBLEnvironment>("ibl");
    ibl.setUseDefault();

    // =========================================================================
    // Camera
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(6.0f);
    camera.elevation(0.3f);
    camera.azimuth(0.0f);
    camera.fov(50.0f);

    // =========================================================================
    // Render
    // =========================================================================

    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.setShadingMode(ShadingMode::PBR);
    render.setAmbient(0.15f);
    render.setIbl(true);
    render.setEnvironmentInput(&ibl);
    render.setShowSkybox(true);
    render.setClearColor(0.05f, 0.05f, 0.08f, 1.0f);

    // Bloom to make emissive glow visible
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.setInput(0, &render);
    bloom.threshold = 0.8f;
    bloom.intensity = 0.5f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Orbit camera
    auto& camera = chain.get<CameraOperator>("camera");
    camera.azimuth(time * 0.2f);

    // Fit camera to model bounds when loaded
    auto& model = chain.get<GLTFLoader>("helmet");
    static bool needsFit = true;
    if (model.isLoaded() && needsFit) {
        float radius = model.bounds().radius();
        camera.distance(radius / std::sin(glm::radians(50.0f) * 0.5f) * 1.5f);
        needsFit = false;
    }

    // Rotate glow cube
    auto& scene = chain.get<SceneComposer>("scene");
    auto& entries = scene.entries();
    if (entries.size() > 3) {
        entries[3].transform = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f)) *
                               glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0)) *
                               glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(1, 0, 0));
    }
}

VIVID_CHAIN(setup, update)
