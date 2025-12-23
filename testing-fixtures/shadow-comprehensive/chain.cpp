// Shadow Comprehensive - All Light Types with Shadows
// Demonstrates:
// - DirectionalLight, PointLight, SpotLight with shadow casting
// - Multiple geometry types (box, cylinder, torus, sphere, glTF)
// - Per-object castShadow/receiveShadow toggles via ImGui
// - Switching between light types and shading modes

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/gui/imgui.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Light selection state
static int g_activeLight = 0;  // 0=directional, 1=point, 2=spot
static bool g_shadowsEnabled = true;
static int g_shadingMode = 1;  // 0=Flat, 1=PBR, 2=Toon
static bool g_lightCastsShadow[] = {true, true, true};  // sun, point, spot

// Camera orbit control state
static float g_cameraAzimuth = 0.3f;
static float g_cameraElevation = 0.5f;
static float g_cameraDistance = 12.0f;
static glm::vec2 g_lastMouse = {0, 0};
static bool g_isDragging = false;

// Object shadow states
// Objects: 0=ground, 1=metalCube, 2=pipe, 3=torus, 4=sphere, 5=frontCube, 6=helmet
static const char* g_objectNames[] = {"Ground", "Metal Cube", "Pipe", "Torus", "Sphere", "Front Cube", "Helmet (glTF)"};
static bool g_castShadow[] = {false, true, true, true, false, true, true};
static bool g_receiveShadow[] = {true, true, true, true, true, false, true};
static const int g_numObjects = 7;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // PBR Textured Materials
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
    // Geometry for Scene (with materials assigned directly to meshes)
    // =========================================================================

    auto& hollowCube = chain.add<Box>("hollowCube");
    hollowCube.size(1.5f);
    hollowCube.setMaterial(&metalMat);

    auto& pipe = chain.add<Cylinder>("pipe");
    pipe.radius(0.5f);
    pipe.height(2.0f);
    pipe.segments(32);
    pipe.setMaterial(&bronzeMat);

    auto& gear = chain.add<Torus>("gear");
    gear.outerRadius(0.8f);
    gear.innerRadius(0.3f);
    gear.segments(32);
    gear.rings(16);
    // gear has no material - uses vertex color

    auto& groundPlane = chain.add<Plane>("groundPlane");
    groundPlane.size(12.0f, 12.0f);
    groundPlane.setMaterial(&groundMat);

    auto& sphere = chain.add<Sphere>("sphere");
    sphere.radius(0.7f);
    sphere.segments(32);
    sphere.setMaterial(&graniteMat);

    auto& cube = chain.add<Box>("cube");
    cube.size(1.0f, 1.0f, 1.0f);
    cube.setMaterial(&graniteMat);

    // =========================================================================
    // Scene Composition
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Ground plane (receives shadows, doesn't cast)
    auto groundEntry = scene.add(&groundPlane);
    groundEntry.setTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)));
    groundEntry.setCastShadow(false);
    groundEntry.setReceiveShadow(true);

    // Hollow cube (left) - casts and receives shadows
    glm::mat4 hollowCubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 1.0f, 0.0f));
    auto hollowCubeEntry = scene.add(&hollowCube);
    hollowCubeEntry.setTransform(hollowCubeTransform);
    hollowCubeEntry.setCastShadow(true);
    hollowCubeEntry.setReceiveShadow(true);

    // Pipe (center) - casts shadows
    glm::mat4 pipeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    auto pipeEntry = scene.add(&pipe);
    pipeEntry.setTransform(pipeTransform);
    pipeEntry.setCastShadow(true);
    pipeEntry.setReceiveShadow(true);

    // Gear/Torus (right) - casts shadows, no material (uses vertex color)
    glm::mat4 gearTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.8f, 0.8f, 0.0f));
    auto gearEntry = scene.add(&gear);
    gearEntry.setTransform(gearTransform);
    gearEntry.setColor(0.7f, 0.7f, 0.8f, 1.0f);
    gearEntry.setCastShadow(true);
    gearEntry.setReceiveShadow(true);

    // Granite sphere (front-left) - receives shadows but does NOT cast
    glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.7f, 2.0f));
    auto sphereEntry = scene.add(&sphere);
    sphereEntry.setTransform(sphereTransform);
    sphereEntry.setCastShadow(false);
    sphereEntry.setReceiveShadow(true);

    // Simple cube (front-right) - casts but does NOT receive shadows
    glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.5f, 2.0f));
    auto cubeEntry = scene.add(&cube);
    cubeEntry.setTransform(cubeTransform);
    cubeEntry.setCastShadow(true);
    cubeEntry.setReceiveShadow(false);

    // DamagedHelmet glTF model (has its own embedded material)
    auto& helmet = chain.add<GLTFLoader>("helmet");
    helmet.file("assets/meshes/DamagedHelmet.glb");
    helmet.loadTextures(true);  // Load embedded PBR textures

    glm::mat4 helmetTransform = glm::translate(glm::mat4(1.0f), glm::vec3(3.5f, 1.0f, 0.0f)) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(0.8f));
    auto helmetEntry = scene.add(&helmet);
    helmetEntry.setTransform(helmetTransform);
    helmetEntry.setCastShadow(true);
    helmetEntry.setReceiveShadow(true);

    // =========================================================================
    // Lights - All Three Types with Shadow Casting
    // =========================================================================

    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(1.0f, -1.5f, 0.5f);
    sun.color(1.0f, 0.98f, 0.95f);
    sun.intensity = 2.0f;
    sun.castShadow(true);
    sun.shadowBias(0.01f);
    sun.drawDebug(true);  // Show yellow arrow indicating sun direction

    auto& point = chain.add<PointLight>("point");
    point.position(0.0f, 4.0f, 0.0f);
    point.color(0.9f, 0.8f, 0.6f);
    point.intensity = 2.5f;
    point.range = 15.0f;
    point.castShadow(true);
    point.shadowBias(0.02f);
    point.drawDebug(true);  // Show wireframe sphere at light position

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
    spot.drawDebug(true);  // Show wireframe cone at light position

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
    render.setLightInput(&sun);      // Primary light (slot 0)
    render.addLight(&point);          // Additional light (slot 1) - for debug viz
    render.addLight(&spot);           // Additional light (slot 2) - for debug viz
    render.setShadingMode(ShadingMode::PBR);
    render.setAmbient(0.2f);
    render.setShadows(true);
    render.setShadowMapResolution(1024);
    render.setClearColor(0.5f, 0.6f, 0.8f, 1.0f);

    chain.output("render");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    auto& sun = chain.get<DirectionalLight>("sun");
    auto& point = chain.get<PointLight>("point");
    auto& spot = chain.get<SpotLight>("spot");
    auto& render = chain.get<Render3D>("render");
    auto& scene = chain.get<SceneComposer>("scene");
    auto& camera = chain.get<CameraOperator>("camera");

    // =========================================================================
    // Orbit Camera Controls (drag to rotate, scroll to zoom)
    // =========================================================================

    glm::vec2 mousePos = ctx.mouse();
    auto leftButton = ctx.mouseButton(0);
    glm::vec2 scrollDelta = ctx.scroll();

    // Check if mouse is over ImGui window
    bool imguiWantsMouse = ImGui::GetIO().WantCaptureMouse;

    if (!imguiWantsMouse) {
        // Start dragging on left mouse button press
        if (leftButton.pressed) {
            g_isDragging = true;
            g_lastMouse = mousePos;
        }

        // Update camera while dragging
        if (g_isDragging && leftButton.held) {
            glm::vec2 delta = mousePos - g_lastMouse;
            g_cameraAzimuth -= delta.x * 0.005f;
            g_cameraElevation += delta.y * 0.005f;
            // Clamp elevation to avoid gimbal lock
            g_cameraElevation = std::clamp(g_cameraElevation, -1.5f, 1.5f);
            g_lastMouse = mousePos;
        }

        // Stop dragging on release
        if (leftButton.released) {
            g_isDragging = false;
        }

        // Zoom with scroll wheel
        if (std::abs(scrollDelta.y) > 0.01f) {
            g_cameraDistance -= scrollDelta.y * 0.5f;
            g_cameraDistance = std::clamp(g_cameraDistance, 3.0f, 30.0f);
        }
    }

    // Apply camera settings
    camera.azimuth(g_cameraAzimuth);
    camera.elevation(g_cameraElevation);
    camera.distance(g_cameraDistance);

    // =========================================================================
    // ImGui Control Panel
    // =========================================================================

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Shadow Controls")) {
        // Global shadows toggle
        if (ImGui::Checkbox("Shadows Enabled", &g_shadowsEnabled)) {
            render.setShadows(g_shadowsEnabled);
        }

        ImGui::Separator();

        // Shading Mode
        ImGui::Text("Shading Mode:");
        const char* shadingModes[] = {"Flat", "PBR", "Toon"};
        if (ImGui::Combo("##shading", &g_shadingMode, shadingModes, 3)) {
            ShadingMode modes[] = {ShadingMode::Flat, ShadingMode::PBR, ShadingMode::Toon};
            render.setShadingMode(modes[g_shadingMode]);
        }

        ImGui::Separator();

        // Light Selection
        ImGui::Text("Active Light:");
        const char* lightNames[] = {"Directional (Sun)", "Point (Orbiting)", "Spot (Corner)"};
        if (ImGui::Combo("##light", &g_activeLight, lightNames, 3)) {
            if (g_activeLight == 0) render.setLightInput(&sun);
            else if (g_activeLight == 1) render.setLightInput(&point);
            else render.setLightInput(&spot);
        }

        // Light casts shadow checkbox
        if (ImGui::Checkbox("Light Casts Shadow", &g_lightCastsShadow[g_activeLight])) {
            if (g_activeLight == 0) sun.castShadow(g_lightCastsShadow[0]);
            else if (g_activeLight == 1) point.castShadow(g_lightCastsShadow[1]);
            else spot.castShadow(g_lightCastsShadow[2]);
        }

        ImGui::Separator();

        // Object Shadow Controls
        ImGui::Text("Object Shadows:");
        ImGui::BeginChild("ObjectList", ImVec2(0, 200), true);

        for (int i = 0; i < g_numObjects; i++) {
            ImGui::PushID(i);

            // Object name with tree node style
            bool open = ImGui::TreeNodeEx(g_objectNames[i], ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed);
            if (open) {
                // Cast shadow checkbox
                if (ImGui::Checkbox("Cast", &g_castShadow[i])) {
                    scene.entries()[i].castShadow = g_castShadow[i];
                    scene.markDirty();  // Trigger scene rebuild
                }
                ImGui::SameLine();
                // Receive shadow checkbox
                if (ImGui::Checkbox("Receive", &g_receiveShadow[i])) {
                    scene.entries()[i].receiveShadow = g_receiveShadow[i];
                    scene.markDirty();  // Trigger scene rebuild
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    }
    ImGui::End();

    // =========================================================================
    // Animations
    // =========================================================================

    // Animate point light position (orbiting)
    float pointRadius = 3.0f;
    float pointHeight = 4.0f + std::sin(time * 0.5f) * 0.5f;
    float pointX = std::cos(time * 0.3f) * pointRadius;
    float pointZ = std::sin(time * 0.3f) * pointRadius;
    point.position(pointX, pointHeight, pointZ);

    // Animate spot light
    float spotAngle = time * 0.8f;
    float spotRadius = 4.0f;
    float spotX = std::cos(spotAngle) * spotRadius;
    float spotZ = std::sin(spotAngle) * spotRadius;
    float spotY = 5.0f + std::sin(time * 1.5f) * 1.0f;
    spot.position(spotX, spotY, spotZ);
    spot.direction(-spotX * 0.3f, -1.0f, -spotZ * 0.3f);

    // Rotate objects
    auto& entries = scene.entries();

    // Hollow cube (index 1)
    entries[1].transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 1.0f, 0.0f)) *
                           glm::rotate(glm::mat4(1.0f), time * 0.2f, glm::vec3(0, 1, 0));

    // Pipe (index 2)
    entries[2].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
                           glm::rotate(glm::mat4(1.0f), glm::radians(15.0f), glm::vec3(1, 0, 0)) *
                           glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(0, 1, 0));

    // Gear/Torus (index 3)
    entries[3].transform = glm::translate(glm::mat4(1.0f), glm::vec3(1.8f, 0.8f, 0.0f)) *
                           glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0));

    // Mark scene dirty so transforms are synced to Scene objects
    scene.markDirty();

    // Animate sun direction
    float sunAngle = time * 0.1f;
    sun.direction(std::cos(sunAngle), -1.5f, std::sin(sunAngle) * 0.5f);
}

VIVID_CHAIN(setup, update)
