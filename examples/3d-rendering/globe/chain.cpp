// Globe - Vivid 3D Example
// A rotating Earth with PBR lighting and procedural noise displacement
//
// Controls:
//   TAB: Open chain visualizer
//   Use the Globe Controls panel to adjust lights, rotation, and displacement

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/gui/imgui.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Globe state
static bool g_autoRotate = true;
static float g_rotation = 0.0f;

// Displacement state
static bool g_displacementEnabled = true;
static float g_displacementAmplitude = 0.25f;

// Light enable states
static bool g_sunEnabled = true;
static bool g_spotlightEnabled = true;
static bool g_pointLightEnabled = true;
static bool g_rimLightEnabled = true;

// Light intensities (stored when enabled)
static float g_sunIntensity = 3.0f;
static float g_spotlightIntensity = 12.0f;
static float g_pointLightIntensity = 6.0f;
static float g_rimLightIntensity = 1.5f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Earth Sphere
    // =========================================================================

    auto& material = chain.add<TexturedMaterial>("earthMat");
    material.baseColor("assets/textures/flat_earth_Largest_still.0330.jpg");
    material.roughnessFactor(0.75f);
    material.metallicFactor(0.0f);

    auto& sphere = chain.add<Sphere>("earth");
    sphere.radius(1.0f);
    sphere.segments(128);
    sphere.computeTangents();
    sphere.setMaterial(&material);

    // =========================================================================
    // Displacement Noise
    // =========================================================================

    auto& noise = chain.add<Noise>("terrain");
    noise.scale = 3.0f;
    noise.speed = 0.3f;
    noise.octaves = 4;
    noise.type(NoiseType::Simplex);
    noise.setResolution(512, 512);

    auto& scene = SceneComposer::create(chain, "scene");
    scene.add(&sphere);

    // =========================================================================
    // Camera & Lighting
    // =========================================================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(3.0f);
    camera.elevation(0.3f);
    camera.azimuth(0.0f);
    camera.fov(45.0f);

    // Key light (sun) - warm directional
    auto& sunLight = chain.add<DirectionalLight>("sun");
    sunLight.direction(-0.5f, -0.3f, -1.0f);
    sunLight.color(1.0f, 0.97f, 0.91f);
    sunLight.intensity = g_sunIntensity;

    // Warm spotlight accent
    auto& spotlight = chain.add<SpotLight>("spotlight");
    spotlight.position(2.5f, 2.0f, 2.5f);
    spotlight.direction(-0.6f, -0.5f, -0.6f);
    spotlight.color(1.0f, 0.85f, 0.6f);
    spotlight.intensity = g_spotlightIntensity;
    spotlight.range = 10.0f;
    spotlight.spotAngle = 25.0f;
    spotlight.spotBlend = 0.4f;

    // Cool blue point light
    auto& pointLight = chain.add<PointLight>("point");
    pointLight.position(-2.5f, 0.0f, 1.5f);
    pointLight.color(0.3f, 0.5f, 1.0f);
    pointLight.intensity = g_pointLightIntensity;
    pointLight.range = 8.0f;

    // Rim light - from behind and above to create edge glow on top/sides
    auto& rimLight = chain.add<DirectionalLight>("rim");
    rimLight.direction(0.0f, -0.5f, 1.0f);  // From behind-above, pointing down-forward
    rimLight.color(0.5f, 0.6f, 0.9f);
    rimLight.intensity = g_rimLightIntensity;

    // =========================================================================
    // 3D Rendering
    // =========================================================================

    auto& render = chain.add<Render3D>("render");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sunLight);
    render.addLight(&spotlight);
    render.addLight(&pointLight);
    render.addLight(&rimLight);
    render.setShadingMode(ShadingMode::PBR);
    render.setColor(0.02f, 0.02f, 0.04f, 1.0f);

    render.setDisplacementInput(&noise);
    render.setDisplacementAmplitude(g_displacementAmplitude);
    render.setDisplacementMidpoint(0.5f);

    // =========================================================================
    // Post-Processing
    // =========================================================================

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("render");
    bloom.threshold = 0.9f;
    bloom.intensity = 0.2f;
    bloom.radius = 6.0f;

    auto& vignette = chain.add<CRTEffect>("vignette");
    vignette.input("bloom");
    vignette.curvature = 0.0f;
    vignette.vignette = 0.4f;
    vignette.scanlines = 0.0f;
    vignette.bloom = 0.0f;
    vignette.chromatic = 0.0f;

    chain.output("vignette");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Globe - Earth with PBR Lighting" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Use the Globe Controls panel to adjust:" << std::endl;
    std::cout << "- Individual lights (enable/disable + intensity)" << std::endl;
    std::cout << "- Auto-rotation" << std::endl;
    std::cout << "- Displacement effect" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& camera = chain.get<CameraOperator>("camera");
    auto& scene = chain.get<SceneComposer>("scene");
    auto& render = chain.get<Render3D>("render");
    auto& noise = chain.get<Noise>("terrain");

    // Get light references
    auto& sunLight = chain.get<DirectionalLight>("sun");
    auto& spotlight = chain.get<SpotLight>("spotlight");
    auto& pointLight = chain.get<PointLight>("point");
    auto& rimLight = chain.get<DirectionalLight>("rim");

    float dt = static_cast<float>(ctx.dt());

    // =========================================================================
    // ImGui Controls Panel
    // =========================================================================

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Globe Controls")) {
        // --- Rotation ---
        ImGui::SeparatorText("Rotation");
        ImGui::Checkbox("Auto-Rotate", &g_autoRotate);
        if (!g_autoRotate) {
            ImGui::SliderFloat("Rotation", &g_rotation, 0.0f, 6.28f, "%.2f rad");
        }

        // --- Displacement ---
        ImGui::SeparatorText("Displacement");
        if (ImGui::Checkbox("Enable Displacement", &g_displacementEnabled)) {
            if (g_displacementEnabled) {
                render.setDisplacementInput(&noise);
            } else {
                render.setDisplacementInput(nullptr);
            }
        }
        if (g_displacementEnabled) {
            if (ImGui::SliderFloat("Amplitude", &g_displacementAmplitude, 0.0f, 0.5f)) {
                render.setDisplacementAmplitude(g_displacementAmplitude);
            }
        }

        // --- Lights ---
        ImGui::SeparatorText("Lights");

        // Sun light
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.97f, 0.7f, 1.0f));
        if (ImGui::Checkbox("Sun (Warm)", &g_sunEnabled)) {
            sunLight.intensity = g_sunEnabled ? g_sunIntensity : 0.0f;
        }
        ImGui::PopStyleColor();
        if (g_sunEnabled) {
            ImGui::Indent();
            if (ImGui::SliderFloat("##SunIntensity", &g_sunIntensity, 0.0f, 10.0f, "%.1f")) {
                sunLight.intensity = g_sunIntensity;
            }
            ImGui::Unindent();
        }

        // Spotlight
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.6f, 1.0f));
        if (ImGui::Checkbox("Spotlight (Orange)", &g_spotlightEnabled)) {
            spotlight.intensity = g_spotlightEnabled ? g_spotlightIntensity : 0.0f;
        }
        ImGui::PopStyleColor();
        if (g_spotlightEnabled) {
            ImGui::Indent();
            if (ImGui::SliderFloat("##SpotIntensity", &g_spotlightIntensity, 0.0f, 30.0f, "%.1f")) {
                spotlight.intensity = g_spotlightIntensity;
            }
            ImGui::Unindent();
        }

        // Point light
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
        if (ImGui::Checkbox("Point Light (Blue)", &g_pointLightEnabled)) {
            pointLight.intensity = g_pointLightEnabled ? g_pointLightIntensity : 0.0f;
        }
        ImGui::PopStyleColor();
        if (g_pointLightEnabled) {
            ImGui::Indent();
            if (ImGui::SliderFloat("##PointIntensity", &g_pointLightIntensity, 0.0f, 20.0f, "%.1f")) {
                pointLight.intensity = g_pointLightIntensity;
            }
            ImGui::Unindent();
        }

        // Rim light
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 0.95f, 1.0f));
        if (ImGui::Checkbox("Rim Light (Back)", &g_rimLightEnabled)) {
            rimLight.intensity = g_rimLightEnabled ? g_rimLightIntensity : 0.0f;
        }
        ImGui::PopStyleColor();
        if (g_rimLightEnabled) {
            ImGui::Indent();
            if (ImGui::SliderFloat("##RimIntensity", &g_rimLightIntensity, 0.0f, 5.0f, "%.1f")) {
                rimLight.intensity = g_rimLightIntensity;
            }
            ImGui::Unindent();
        }

        ImGui::Separator();
        if (ImGui::Button("Reset All")) {
            g_autoRotate = true;
            g_rotation = 0.0f;
            g_displacementEnabled = true;
            g_displacementAmplitude = 0.25f;
            g_sunEnabled = true;
            g_spotlightEnabled = true;
            g_pointLightEnabled = true;
            g_rimLightEnabled = true;
            g_sunIntensity = 3.0f;
            g_spotlightIntensity = 12.0f;
            g_pointLightIntensity = 6.0f;
            g_rimLightIntensity = 1.5f;

            render.setDisplacementInput(&noise);
            render.setDisplacementAmplitude(g_displacementAmplitude);
            sunLight.intensity = g_sunIntensity;
            spotlight.intensity = g_spotlightIntensity;
            pointLight.intensity = g_pointLightIntensity;
            rimLight.intensity = g_rimLightIntensity;
        }
    }
    ImGui::End();

    // =========================================================================
    // Update Globe
    // =========================================================================

    if (g_autoRotate) {
        g_rotation += dt * 0.1f;
    }

    glm::mat4 earthTransform = glm::rotate(glm::mat4(1.0f), g_rotation, glm::vec3(0, 1, 0));
    earthTransform = glm::rotate(earthTransform, glm::radians(23.5f), glm::vec3(0, 0, 1));
    scene.entries()[0].transform = earthTransform;
    scene.markDirty();  // Rebuild scene with new transform

    float time = static_cast<float>(ctx.time());
    float elevation = 0.3f + std::sin(time * 0.2f) * 0.05f;
    camera.elevation(elevation);
    // Camera stays fixed - globe rotates instead
}

VIVID_CHAIN(setup, update)
