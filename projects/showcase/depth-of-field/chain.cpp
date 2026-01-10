// Depth of Field Showcase - Vivid
// Demonstrates real depth-based DOF using Render3D's depth output
// Features procedural PBR materials with varying surface properties:
//   - Near objects: Rocky orange with rough procedural surface
//   - Mid objects: Polished green metal with cellular roughness
//   - Far objects: Glowing blue with animated emissive
//
// Controls:
//   LEFT/RIGHT: Adjust focus distance
//   UP/DOWN: Adjust blur strength
//   D: Toggle depth debug view

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/gui/imgui.h>
#include <iostream>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// DOF parameters
static float g_focusDistance = 0.15f;  // 0=near, 1=far (start focused on near red objects)
static float g_blurStrength = 0.8f;
static bool g_showDepth = false;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Procedural Textures for Materials
    // =========================================================================

    // Near material: Rocky red with procedural roughness variation
    auto& nearRoughnessTex = chain.add<Noise>("nearRoughness");
    nearRoughnessTex.scale = 8.0f;
    nearRoughnessTex.octaves = 4;
    nearRoughnessTex.type = NoiseType::Simplex;
    nearRoughnessTex.setResolution(256, 256);

    // Mid material: Metallic green with cellular pattern
    auto& midRoughnessTex = chain.add<Noise>("midRoughness");
    midRoughnessTex.scale = 12.0f;
    midRoughnessTex.octaves = 2;
    midRoughnessTex.type = NoiseType::Worley;
    midRoughnessTex.setResolution(256, 256);

    // Far material: Glowing blue with animated emissive
    auto& farEmissiveTex = chain.add<Noise>("farEmissive");
    farEmissiveTex.scale = 5.0f;
    farEmissiveTex.speed = 0.3f;
    farEmissiveTex.octaves = 3;
    farEmissiveTex.type = NoiseType::Simplex;
    farEmissiveTex.setResolution(256, 256);

    // Ground: Subtle noise for variation
    auto& groundRoughnessTex = chain.add<Noise>("groundRoughness");
    groundRoughnessTex.scale = 4.0f;
    groundRoughnessTex.octaves = 2;
    groundRoughnessTex.type = NoiseType::Simplex;
    groundRoughnessTex.setResolution(256, 256);

    // =========================================================================
    // Materials with Procedural Textures
    // =========================================================================

    // Near material: Rocky red/orange with rough surface
    auto& nearMat = chain.add<TexturedMaterial>("nearMat");
    nearMat.baseColorFactor(0.9f, 0.25f, 0.1f, 1.0f);  // Orange-red
    nearMat.roughnessInput(&nearRoughnessTex);
    nearMat.roughnessFactor(0.8f);  // Generally rough
    nearMat.metallicFactor(0.0f);

    // Mid material: Polished green metal
    auto& midMat = chain.add<TexturedMaterial>("midMat");
    midMat.baseColorFactor(0.2f, 0.85f, 0.3f, 1.0f);  // Bright green
    midMat.roughnessInput(&midRoughnessTex);
    midMat.roughnessFactor(0.25f);  // Mostly smooth/shiny
    midMat.metallicFactor(0.9f);    // Metallic

    // Far material: Glowing blue with emissive
    auto& farMat = chain.add<TexturedMaterial>("farMat");
    farMat.baseColorFactor(0.1f, 0.3f, 0.9f, 1.0f);  // Blue
    farMat.emissiveInput(&farEmissiveTex);
    farMat.emissiveFactor(0.2f, 0.5f, 1.0f);  // Blue glow
    farMat.emissiveStrength(2.0f);
    farMat.roughnessFactor(0.3f);
    farMat.metallicFactor(0.5f);

    // Ground material: Subtle concrete-like surface
    auto& groundMat = chain.add<TexturedMaterial>("groundMat");
    groundMat.baseColorFactor(0.25f, 0.25f, 0.27f, 1.0f);  // Gray
    groundMat.roughnessInput(&groundRoughnessTex);
    groundMat.roughnessFactor(0.85f);
    groundMat.metallicFactor(0.0f);

    // Near geometry (red) - with material colors
    auto& nearSphere = chain.add<Sphere>("nearSphere");
    nearSphere.radius(0.6f);
    nearSphere.segments(24);
    nearSphere.setMaterial(&nearMat);

    auto& nearBox = chain.add<Box>("nearBox");
    nearBox.size(0.9f, 0.9f, 0.9f);
    nearBox.setMaterial(&nearMat);

    auto& nearTorus = chain.add<Torus>("nearTorus");
    nearTorus.outerRadius(0.5f);
    nearTorus.innerRadius(0.2f);
    nearTorus.segments(24);
    nearTorus.rings(16);
    nearTorus.setMaterial(&nearMat);

    // Mid geometry (green) - with material colors
    auto& midSphere = chain.add<Sphere>("midSphere");
    midSphere.radius(0.6f);
    midSphere.segments(24);
    midSphere.setMaterial(&midMat);

    auto& midBox = chain.add<Box>("midBox");
    midBox.size(0.9f, 0.9f, 0.9f);
    midBox.setMaterial(&midMat);

    auto& midTorus = chain.add<Torus>("midTorus");
    midTorus.outerRadius(0.5f);
    midTorus.innerRadius(0.2f);
    midTorus.segments(24);
    midTorus.rings(16);
    midTorus.setMaterial(&midMat);

    // Far geometry (blue) - with material colors
    auto& farSphere1 = chain.add<Sphere>("farSphere1");
    farSphere1.radius(0.6f);
    farSphere1.segments(24);
    farSphere1.setMaterial(&farMat);

    auto& farSphere2 = chain.add<Sphere>("farSphere2");
    farSphere2.radius(0.6f);
    farSphere2.segments(24);
    farSphere2.setMaterial(&farMat);

    auto& farBox = chain.add<Box>("farBox");
    farBox.size(0.9f, 0.9f, 0.9f);
    farBox.setMaterial(&farMat);

    auto& farTorus1 = chain.add<Torus>("farTorus1");
    farTorus1.outerRadius(0.5f);
    farTorus1.innerRadius(0.2f);
    farTorus1.segments(24);
    farTorus1.rings(16);
    farTorus1.setMaterial(&farMat);

    auto& farTorus2 = chain.add<Torus>("farTorus2");
    farTorus2.outerRadius(0.5f);
    farTorus2.innerRadius(0.2f);
    farTorus2.segments(24);
    farTorus2.rings(16);
    farTorus2.setMaterial(&farMat);

    // =========================================================================
    // Scene with objects at varying depths
    // Color coded: NEAR=red, MID=green, FAR=blue
    // Camera at Z=-10, near Z=-5 to -3, mid Z=5 to 12, far Z=18 to 28
    // =========================================================================

    auto& scene = SceneComposer::create(chain, "scene");

    // NEAR objects (red) - very close to camera (Z=-5 to -3)
    scene.add(&nearSphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(-1.2f, 0, -5.0f)));
    scene.add(&nearBox,
        glm::translate(glm::mat4(1.0f), glm::vec3(1.2f, 0.3f, -4.0f)));
    scene.add(&nearTorus,
        glm::translate(glm::mat4(1.0f), glm::vec3(0, 1.2f, -4.5f)));

    // MID objects (green) - middle distance (Z=5 to 15)
    scene.add(&midSphere,
        glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 8.0f)));
    scene.add(&midBox,
        glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, -0.3f, 10.0f)));
    scene.add(&midTorus,
        glm::rotate(
            glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.5f, 12.0f)),
            0.5f, glm::vec3(1, 0, 0)));

    // FAR objects (blue) - farther from camera (Z=18 to 28)
    scene.add(&farSphere1,
        glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.5f, 20.0f)),
            glm::vec3(1.2f)));
    scene.add(&farSphere2,
        glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, -0.2f, 22.0f)),
            glm::vec3(1.5f)));
    scene.add(&farBox,
        glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.8f, 28.0f)),
            glm::vec3(2.0f)));
    scene.add(&farTorus1,
        glm::scale(
            glm::rotate(
                glm::translate(glm::mat4(1.0f), glm::vec3(-4.0f, 0.0f, 25.0f)),
                1.0f, glm::vec3(0, 1, 0)),
            glm::vec3(1.3f)));
    scene.add(&farTorus2,
        glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 1.0f, 26.0f)),
            glm::vec3(1.5f)));

    // Ground plane - extended for depth
    auto& plane = chain.add<Plane>("plane");
    plane.size(30.0f, 80.0f);
    plane.subdivisions(1, 1);
    plane.setMaterial(&groundMat);

    scene.add(&plane,
        glm::rotate(
            glm::translate(glm::mat4(1.0f), glm::vec3(0, -1.5f, 20.0f)),
            -1.57f, glm::vec3(1, 0, 0)));

    // =========================================================================
    // Camera and Lighting
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.position(0, 2.0f, -10.0f);
    camera.target(0, 0, 20.0f);
    camera.fov(45.0f);
    camera.nearPlane(1.0f);
    camera.farPlane(70.0f);  // Match scene depth range

    auto& keyLight = chain.add<DirectionalLight>("keyLight");
    keyLight.direction(-0.5f, -1.0f, -0.5f);  // From upper-right-front, pointing down-left-back
    keyLight.color(1.0f, 0.95f, 0.9f);  // Warm white
    keyLight.intensity = 4.0f;

    auto& fillLight = chain.add<DirectionalLight>("fillLight");
    fillLight.direction(0.5f, -0.3f, -0.5f);  // From upper-left-front, softer fill
    fillLight.color(0.9f, 0.9f, 1.0f);  // Cool white
    fillLight.intensity = 2.0f;

    // =========================================================================
    // 3D Render with depth output enabled
    // =========================================================================

    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&keyLight);
    render.addLight(&fillLight);
    render.setShadingMode(ShadingMode::PBR);  // PBR shading with material colors
    render.setColor(0.02f, 0.02f, 0.04f, 1.0f);  // Dark background
    render.setDepthOutput(true);  // Enable depth output for DOF

    // =========================================================================
    // Depth of Field post-processing
    // =========================================================================

    auto& dof = chain.add<DepthOfField>("dof");
    dof.input(&render);  // Takes color and depth from Render3D
    dof.focusDistance(g_focusDistance);
    dof.focusRange(0.05f);
    dof.blurStrength(g_blurStrength);

    // =========================================================================
    // Final post-processing
    // =========================================================================

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.setInput(0, &dof);  // DOF -> Bloom
    bloom.threshold = 0.8f;
    bloom.intensity = 0.3f;
    bloom.radius = 6.0f;

    chain.output("bloom");

    // =========================================================================
    // Info
    // =========================================================================

    std::cout << "\n========================================" << std::endl;
    std::cout << "Depth of Field Showcase" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Real depth-based DOF using depth buffer" << std::endl;
    std::cout << "Materials with procedural textures:" << std::endl;
    std::cout << "  NEAR: Rocky orange (rough)" << std::endl;
    std::cout << "  MID: Polished green metal" << std::endl;
    std::cout << "  FAR: Glowing blue (emissive)" << std::endl;
    std::cout << "\nControls:" << std::endl;
    std::cout << "  LEFT/RIGHT: Focus distance" << std::endl;
    std::cout << "  UP/DOWN: Blur strength" << std::endl;
    std::cout << "  D: Toggle depth debug view" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = static_cast<float>(ctx.dt());

    auto& dof = chain.get<DepthOfField>("dof");

    // =========================================================================
    // ImGui Controls Panel
    // =========================================================================

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Depth of Field Controls")) {
        ImGui::SeparatorText("Focus");

        ImGui::SliderFloat("Focus Distance", &g_focusDistance, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Blur Strength", &g_blurStrength, 0.0f, 1.0f, "%.2f");

        ImGui::SeparatorText("Debug");

        if (ImGui::Checkbox("Show Depth Buffer", &g_showDepth)) {
            std::cout << "[DOF] Depth view: " << (g_showDepth ? "ON" : "OFF") << std::endl;
        }
        if (g_showDepth) {
            ImGui::TextWrapped("R=depth, G=focus, B=blur amount");
        }

        ImGui::Separator();

        if (ImGui::Button("Reset Defaults")) {
            g_focusDistance = 0.15f;
            g_blurStrength = 0.8f;
            g_showDepth = false;
        }

        ImGui::SeparatorText("Keyboard Shortcuts");
        ImGui::TextDisabled("LEFT/RIGHT: Focus distance");
        ImGui::TextDisabled("UP/DOWN: Blur strength");
        ImGui::TextDisabled("D: Toggle depth view");
    }
    ImGui::End();

    // =========================================================================
    // Keyboard Input
    // =========================================================================

    // Toggle depth debug view
    if (ctx.key(GLFW_KEY_D).pressed) {
        g_showDepth = !g_showDepth;
        std::cout << "[DOF] Depth view: " << (g_showDepth ? "ON" : "OFF") << std::endl;
    }

    // Adjust focus distance
    if (ctx.key(GLFW_KEY_RIGHT).held) {
        g_focusDistance = std::min(g_focusDistance + dt * 0.3f, 1.0f);
    }
    if (ctx.key(GLFW_KEY_LEFT).held) {
        g_focusDistance = std::max(g_focusDistance - dt * 0.3f, 0.0f);
    }

    // Adjust blur strength
    if (ctx.key(GLFW_KEY_UP).held) {
        g_blurStrength = std::min(g_blurStrength + dt * 0.3f, 1.0f);
    }
    if (ctx.key(GLFW_KEY_DOWN).held) {
        g_blurStrength = std::max(g_blurStrength - dt * 0.3f, 0.0f);
    }

    // =========================================================================
    // Update DOF parameters
    // =========================================================================

    dof.focusDistance(g_focusDistance);
    dof.blurStrength(g_blurStrength);
    dof.showDepth(g_showDepth);
}

// Fullscreen cinematic showcase
VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = true,
    .fullscreen = true
}))
