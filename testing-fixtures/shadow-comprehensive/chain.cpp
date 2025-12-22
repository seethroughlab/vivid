// Shadow Comprehensive - All Light Types with Shadows
// Demonstrates:
// - DirectionalLight, PointLight, SpotLight with shadow casting
// - Multiple geometry types (box, cylinder, torus, sphere)
// - Per-object castShadow/receiveShadow toggles
// - Switching between light types at runtime
//
// Press 1/2/3 to cycle active light (directional/point/spot)
// Press SPACE to toggle shadows on/off
// Press S to toggle current light's shadow casting

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <GLFW/glfw3.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Light selection state
static int g_activeLight = 0;  // 0=directional, 1=point, 2=spot
static bool g_shadowsEnabled = true;

// Light marker indices in scene
static int g_pointLightMarkerIndex = -1;
static int g_spotLightMarkerIndex = -1;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // PBR Textured Materials (for future use when PBR shadows are implemented)
    // =========================================================================

    // Ground material - hexagon pavers (great for showing shadows)
    auto& groundMat = chain.add<TexturedMaterial>("groundMat");
    groundMat.baseColor("assets/materials/hexagon-pavers1-bl/hexagon-pavers1_albedo.png");
    groundMat.normal("assets/materials/hexagon-pavers1-bl/hexagon-pavers1_normal-ogl.png");
    groundMat.metallic("assets/materials/hexagon-pavers1-bl/hexagon-pavers1_metallic.png");
    groundMat.roughness("assets/materials/hexagon-pavers1-bl/hexagon-pavers1_roughness.png");
    groundMat.ao("assets/materials/hexagon-pavers1-bl/hexagon-pavers1_ao.png");

    // Metal material for CSG objects
    auto& metalMat = chain.add<TexturedMaterial>("metalMat");
    metalMat.baseColor("assets/materials/worn-shiny-metal-bl/worn-shiny-metal-albedo.png");
    metalMat.normal("assets/materials/worn-shiny-metal-bl/worn-shiny-metal-Normal-ogl.png");
    metalMat.metallic("assets/materials/worn-shiny-metal-bl/worn-shiny-metal-Metallic.png");
    metalMat.roughness("assets/materials/worn-shiny-metal-bl/worn-shiny-metal-Roughness.png");
    metalMat.ao("assets/materials/worn-shiny-metal-bl/worn-shiny-metal-ao.png");

    // Bronze material
    auto& bronzeMat = chain.add<TexturedMaterial>("bronzeMat");
    bronzeMat.baseColor("assets/materials/bronze-bl/bronze_albedo.png");
    bronzeMat.normal("assets/materials/bronze-bl/bronze_normal-ogl.png");
    bronzeMat.metallic("assets/materials/bronze-bl/bronze_metallic.png");
    bronzeMat.roughness("assets/materials/bronze-bl/bronze_roughness.png");
    bronzeMat.ao("assets/materials/bronze-bl/bronze_ao.png");

    // Granite material for primitive objects
    auto& graniteMat = chain.add<TexturedMaterial>("graniteMat");
    graniteMat.baseColor("assets/materials/speckled-granite-tiles-bl/speckled-granite-tiles_albedo.png");
    graniteMat.normal("assets/materials/speckled-granite-tiles-bl/speckled-granite-tiles_normal-ogl.png");
    graniteMat.metallic("assets/materials/speckled-granite-tiles-bl/speckled-granite-tiles_metallic.png");
    graniteMat.roughness("assets/materials/speckled-granite-tiles-bl/speckled-granite-tiles_roughness.png");
    graniteMat.ao("assets/materials/speckled-granite-tiles-bl/speckled-granite-tiles_ao.png");

    // =========================================================================
    // Geometry for Scene
    // =========================================================================

    // Use simple primitives instead of CSG for now
    auto& hollowCube = chain.add<Box>("hollowCube");
    hollowCube.size(1.5f);

    auto& pipe = chain.add<Cylinder>("pipe");
    pipe.radius(0.5f);
    pipe.height(2.0f);
    pipe.segments(32);

    auto& gear = chain.add<Torus>("gear");
    gear.outerRadius(0.8f);
    gear.innerRadius(0.3f);
    gear.segments(32);
    gear.rings(16);

    // =========================================================================
    // Primitives for basic shapes
    // =========================================================================

    auto& groundPlane = chain.add<Plane>("groundPlane");
    groundPlane.size(12.0f, 12.0f);

    auto& sphere = chain.add<Sphere>("sphere");
    sphere.radius(0.7f);
    sphere.segments(32);

    auto& cube = chain.add<Box>("cube");
    cube.size(1.0f, 1.0f, 1.0f);

    // Light markers (small spheres to show light positions)
    auto& pointMarker = chain.add<Sphere>("pointMarker");
    pointMarker.radius(0.15f);
    pointMarker.segments(12);

    auto& spotMarker = chain.add<Cone>("spotMarker");
    spotMarker.radius(0.2f);
    spotMarker.height(0.4f);
    spotMarker.segments(12);

    // =========================================================================
    // Scene Composition
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane (receives shadows, doesn't cast)
    auto groundEntry = scene.add(&groundPlane, nullptr);
    groundEntry.setTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)));
    groundEntry.setCastShadow(false);
    groundEntry.setReceiveShadow(true);
    scene.entries().back().material = &groundMat;

    // Hollow cube (left) - casts and receives shadows, metal material
    glm::mat4 hollowCubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 1.0f, 0.0f));
    auto hollowCubeEntry = scene.add(&hollowCube, nullptr);
    hollowCubeEntry.setTransform(hollowCubeTransform);
    hollowCubeEntry.setCastShadow(true);
    hollowCubeEntry.setReceiveShadow(true);
    scene.entries().back().material = &metalMat;

    // Pipe (center) - casts shadows, bronze material
    glm::mat4 pipeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    auto pipeEntry = scene.add(&pipe, nullptr);
    pipeEntry.setTransform(pipeTransform);
    pipeEntry.setCastShadow(true);
    pipeEntry.setReceiveShadow(true);
    scene.entries().back().material = &bronzeMat;

    // Gear/Torus (right) - casts shadows
    glm::mat4 gearTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.8f, 0.8f, 0.0f));
    auto gearEntry = scene.add(&gear, nullptr);
    gearEntry.setTransform(gearTransform);
    gearEntry.setColor(0.7f, 0.7f, 0.8f, 1.0f);
    gearEntry.setCastShadow(true);
    gearEntry.setReceiveShadow(true);

    // Granite sphere (front-left) - receives shadows but does NOT cast
    glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.7f, 2.0f));
    auto sphereEntry = scene.add(&sphere, nullptr);
    sphereEntry.setTransform(sphereTransform);
    sphereEntry.setCastShadow(false);  // Demo: no shadow casting
    sphereEntry.setReceiveShadow(true);
    scene.entries().back().material = &graniteMat;

    // Simple cube (front-right) - casts but does NOT receive shadows
    glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.5f, 2.0f));
    auto cubeEntry = scene.add(&cube, nullptr);
    cubeEntry.setTransform(cubeTransform);
    cubeEntry.setCastShadow(true);
    cubeEntry.setReceiveShadow(false);  // Demo: always fully lit
    scene.entries().back().material = &graniteMat;

    // Point light marker (yellow, emissive)
    g_pointLightMarkerIndex = static_cast<int>(scene.entries().size());
    auto pointMarkerEntry = scene.add(&pointMarker, nullptr);
    pointMarkerEntry.setTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.0f, 0.0f)));
    pointMarkerEntry.setColor(1.0f, 0.9f, 0.3f, 1.0f);
    pointMarkerEntry.setCastShadow(false);
    pointMarkerEntry.setReceiveShadow(false);

    // Spot light marker (cone, orange)
    g_spotLightMarkerIndex = static_cast<int>(scene.entries().size());
    auto spotMarkerEntry = scene.add(&spotMarker, nullptr);
    spotMarkerEntry.setTransform(glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 5.0f, 3.0f)));
    spotMarkerEntry.setColor(1.0f, 0.5f, 0.2f, 1.0f);
    spotMarkerEntry.setCastShadow(false);
    spotMarkerEntry.setReceiveShadow(false);

    // =========================================================================
    // Lights - All Three Types with Shadow Casting
    // =========================================================================

    // Directional light (sun) - shadows from above-left
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(1.0f, -1.5f, 0.5f);
    sun.color(1.0f, 0.98f, 0.95f);
    sun.intensity = 2.0f;  // Brighter for better visibility
    sun.castShadow(true);
    sun.shadowBias(0.01f);

    // Point light - orbiting above scene
    auto& point = chain.add<PointLight>("point");
    point.position(0.0f, 4.0f, 0.0f);
    point.color(0.9f, 0.8f, 0.6f);
    point.intensity = 2.5f;
    point.range = 15.0f;
    point.castShadow(true);
    point.shadowBias(0.02f);

    // Spot light - pointing down at scene from corner
    auto& spot = chain.add<SpotLight>("spot");
    spot.position(3.0f, 5.0f, 3.0f);
    spot.direction(-0.5f, -1.0f, -0.5f);
    spot.color(0.8f, 0.9f, 1.0f);
    spot.intensity = 3.0f;
    spot.range = 15.0f;
    spot.spotAngle = 35.0f;
    spot.spotBlend = 0.2f;
    spot.castShadow(true);
    spot.shadowBias(0.01f);

    // =========================================================================
    // Camera
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0.5f, 0);
    camera.distance(12.0f);
    camera.elevation(0.5f);
    camera.azimuth(0.3f);
    camera.fov(50.0f);

    // =========================================================================
    // Render with PBR and Shadows
    // =========================================================================

    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    // Start with directional light
    render.setLightInput(&sun);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.2f);
    render.setShadows(true);
    render.setShadowMapResolution(1024);
    render.setClearColor(0.5f, 0.6f, 0.8f, 1.0f);  // Light blue sky

    chain.output("render");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Shadow Comprehensive Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "PBR materials + All light types with shadows" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  1 = Directional light (sun)" << std::endl;
    std::cout << "  2 = Point light (orbiting)" << std::endl;
    std::cout << "  3 = Spot light (corner)" << std::endl;
    std::cout << "  SPACE = Toggle shadows" << std::endl;
    std::cout << "  S = Toggle current light's shadow casting" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Shadow toggles:" << std::endl;
    std::cout << "  - Granite sphere: castShadow=false" << std::endl;
    std::cout << "  - Front cube: receiveShadow=false" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    auto& sun = chain.get<DirectionalLight>("sun");
    auto& point = chain.get<PointLight>("point");
    auto& spot = chain.get<SpotLight>("spot");
    auto& render = chain.get<Render3D>("render");
    auto& scene = chain.get<SceneComposer>("scene");

    // Key handling
    if (ctx.key(GLFW_KEY_1).pressed) {
        g_activeLight = 0;
        render.setLightInput(&sun);
        std::cout << "Active light: Directional (sun)" << std::endl;
    }
    if (ctx.key(GLFW_KEY_2).pressed) {
        g_activeLight = 1;
        render.setLightInput(&point);
        std::cout << "Active light: Point" << std::endl;
    }
    if (ctx.key(GLFW_KEY_3).pressed) {
        g_activeLight = 2;
        render.setLightInput(&spot);
        std::cout << "Active light: Spot" << std::endl;
    }
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        g_shadowsEnabled = !g_shadowsEnabled;
        render.setShadows(g_shadowsEnabled);
        std::cout << "Shadows: " << (g_shadowsEnabled ? "ON" : "OFF") << std::endl;
    }
    if (ctx.key(GLFW_KEY_S).pressed) {
        // Toggle shadow casting on current light
        // Note: We toggle via the castShadow method; there's no getter for current state
        // so we track it externally if needed, or just toggle based on what we set
        if (g_activeLight == 0) {
            static bool sunCasts = true;
            sunCasts = !sunCasts;
            sun.castShadow(sunCasts);
            std::cout << "Sun castShadow: " << (sunCasts ? "ON" : "OFF") << std::endl;
        } else if (g_activeLight == 1) {
            static bool pointCasts = true;
            pointCasts = !pointCasts;
            point.castShadow(pointCasts);
            std::cout << "Point castShadow: " << (pointCasts ? "ON" : "OFF") << std::endl;
        } else {
            static bool spotCasts = true;
            spotCasts = !spotCasts;
            spot.castShadow(spotCasts);
            std::cout << "Spot castShadow: " << (spotCasts ? "ON" : "OFF") << std::endl;
        }
    }

    // Animate point light position (orbiting)
    float pointRadius = 3.0f;
    float pointHeight = 4.0f + std::sin(time * 0.5f) * 0.5f;
    float pointX = std::cos(time * 0.3f) * pointRadius;
    float pointZ = std::sin(time * 0.3f) * pointRadius;
    point.position(pointX, pointHeight, pointZ);

    // Update point light marker
    if (g_pointLightMarkerIndex >= 0 && g_pointLightMarkerIndex < static_cast<int>(scene.entries().size())) {
        scene.entries()[g_pointLightMarkerIndex].transform =
            glm::translate(glm::mat4(1.0f), glm::vec3(pointX, pointHeight, pointZ));
    }

    // Animate spot light (swinging direction)
    float spotSwing = std::sin(time * 0.4f) * 0.3f;
    spot.direction(-0.5f + spotSwing, -1.0f, -0.5f - spotSwing);

    // Rotate CSG objects slowly
    auto& entries = scene.entries();

    // Hollow cube (index 1) - slow rotation
    entries[1].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 1.0f, 0.0f)) *
                           glm::rotate(glm::mat4(1.0f), time * 0.2f, glm::vec3(0, 1, 0));

    // Pipe (index 2) - tilt and rotate
    entries[2].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
                           glm::rotate(glm::mat4(1.0f), glm::radians(15.0f), glm::vec3(1, 0, 0)) *
                           glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(0, 1, 0));

    // Gear/Torus (index 3) - spin
    entries[3].transform = glm::translate(glm::mat4(1.0f), glm::vec3(1.8f, 0.8f, 0.0f)) *
                           glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0));

    // Animate sun direction slightly
    float sunAngle = time * 0.1f;
    sun.direction(std::cos(sunAngle), -1.5f, std::sin(sunAngle) * 0.5f);
}

VIVID_CHAIN(setup, update)
