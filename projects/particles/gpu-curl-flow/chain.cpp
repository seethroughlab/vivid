// GPU Curl Flow - Velocity-aligned 3D cubes in curl noise fields
// Now uses the unified ParticleSystem operator
//
// Controls:
//   Mouse drag - Orbit camera
//   Scroll     - Zoom in/out
//   F          - Toggle fullscreen
//   TAB        - Toggle chain visualizer
//   ESC        - Quit

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/bloom.h>
#include <vivid/render3d/render3d.h>
#include <vivid/gui/imgui.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Global references for update()
static CameraOperator* g_camera = nullptr;
static ParticleSystem* g_particles = nullptr;
static Bloom* g_bloom = nullptr;

// Camera state
static float cameraAzimuth = 0.4f;
static float cameraElevation = 0.5f;
static float cameraDistance = 5.5f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Start in fullscreen
    ctx.fullscreen(true);

    // Camera with orbit controls
    auto& camera = chain.add<CameraOperator>("camera");
    camera.fov(50.0f);
    camera.orbitCenter(0, 0, 0);
    camera.distance(cameraDistance);
    camera.elevation(cameraElevation);
    camera.azimuth(cameraAzimuth);
    g_camera = &camera;

    // Unified particle system with mesh rendering
    auto& ps = chain.add<ParticleSystem>("flow");
    ps.space(ParticleSpace::World3D);
    ps.simulation(SimulationMode::CPU);
    ps.rendering(RenderMode::Mesh);
    g_particles = &ps;

    // Built-in elongated cube mesh
    ps.useBuiltinCube(0.01f, 0.05f);  // width, length
    ps.alignToVelocity(true);

    // Emitter - sphere at origin
    ps.emitter(PsEmitterShape::Sphere);
    ps.emitterPosition.set(0.0f, 0.0f, 0.0f);
    ps.emitterSize = 0.8f;
    ps.emitRate = 6000.0f;
    ps.maxParticles = 30000;

    // Particle lifetime
    ps.lifeMin = 5.0f;
    ps.lifeMax = 10.0f;

    // Size
    ps.sizeStart = 1.0f;
    ps.sizeEnd = 1.0f;

    // Curl noise for organic flow
    ps.curlStrength = 2.5f;
    ps.curlScale = 0.5f;
    ps.curlSpeed = 0.02f;
    ps.curlOctaves = 2;

    // Light drag
    ps.drag = 0.02f;

    // Deep crimson/burgundy color (matching original)
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(1.2f, 0.02f, 0.02f, 1.0f);  // Oversaturated red
    ps.colorEnd.set(0.6f, 0.01f, 0.01f, 0.5f);
    ps.fadeOut = true;

    // Dark background
    ps.clearColor.set(0.02f, 0.02f, 0.03f, 1.0f);

    // Subtle bloom for glow
    auto& bloom = chain.add<Bloom>("glow");
    bloom.input("flow");
    bloom.threshold = 0.6f;
    bloom.intensity = 0.3f;
    bloom.radius = 1.0f;
    g_bloom = &bloom;

    chain.output("glow");
}

void update(Context& ctx) {
    float dt = static_cast<float>(ctx.dt());

    // Toggle fullscreen with F key
    if (ctx.key(GLFW_KEY_F).pressed) {
        ctx.fullscreen(!ctx.fullscreen());
    }

    // Interactive camera: drag to orbit, scroll to zoom
    if (ctx.mouseButton(0).held) {
        glm::vec2 delta = ctx.mouseDeltaNorm();
        cameraAzimuth -= delta.x * 2.0f;
        cameraElevation = glm::clamp(cameraElevation + delta.y * 2.0f, -1.5f, 1.5f);
    }
    glm::vec2 scroll = ctx.scroll();
    if (scroll.y != 0.0f) {
        cameraDistance = glm::clamp(cameraDistance - scroll.y * 0.3f, 1.0f, 10.0f);
    }

    g_camera->azimuth(cameraAzimuth);
    g_camera->elevation(cameraElevation);
    g_camera->distance(cameraDistance);

    // Update particle system with camera data
    const Camera3D& cam = g_camera->outputCamera();
    g_particles->setCamera(
        cam.viewMatrix(),
        cam.projectionMatrix(),
        cam.right(),
        cam.getUp()
    );

    // ImGui Controls Panel
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Curl Flow Controls")) {
        ImGui::SeparatorText("Curl Noise");

        float curlScale = g_particles->curlScale;
        if (ImGui::SliderFloat("Scale", &curlScale, 0.1f, 2.0f)) {
            g_particles->curlScale = curlScale;
        }

        float curlStrength = g_particles->curlStrength;
        if (ImGui::SliderFloat("Strength", &curlStrength, 0.5f, 5.0f)) {
            g_particles->curlStrength = curlStrength;
        }

        float curlSpeed = g_particles->curlSpeed;
        if (ImGui::SliderFloat("Speed", &curlSpeed, 0.0f, 0.1f, "%.3f")) {
            g_particles->curlSpeed = curlSpeed;
        }

        int curlOctaves = g_particles->curlOctaves;
        if (ImGui::SliderInt("Octaves", &curlOctaves, 1, 4)) {
            g_particles->curlOctaves = curlOctaves;
        }

        ImGui::SeparatorText("Particles");

        float emitRate = g_particles->emitRate;
        if (ImGui::SliderFloat("Spawn Rate", &emitRate, 1000.0f, 15000.0f, "%.0f/s")) {
            g_particles->emitRate = emitRate;
        }

        float emitterSize = g_particles->emitterSize;
        if (ImGui::SliderFloat("Spawn Radius", &emitterSize, 0.1f, 2.0f)) {
            g_particles->emitterSize = emitterSize;
        }

        float drag = g_particles->drag;
        if (ImGui::SliderFloat("Drag", &drag, 0.0f, 1.0f, "%.3f")) {
            g_particles->drag = drag;
        }

        ImGui::SeparatorText("Visuals");

        float bloomIntensity = g_bloom->intensity;
        if (ImGui::SliderFloat("Bloom Intensity", &bloomIntensity, 0.0f, 1.0f)) {
            g_bloom->intensity = bloomIntensity;
        }

        float bloomThreshold = g_bloom->threshold;
        if (ImGui::SliderFloat("Bloom Threshold", &bloomThreshold, 0.0f, 1.5f)) {
            g_bloom->threshold = bloomThreshold;
        }

        ImGui::SeparatorText("Info");
        ImGui::Text("Particles: %d", g_particles->particleCount());
        ImGui::Text("FPS: %.1f", 1.0f / dt);

        ImGui::Separator();
        if (ImGui::Button("Reset Defaults")) {
            g_particles->curlScale = 0.5f;
            g_particles->curlStrength = 2.5f;
            g_particles->curlSpeed = 0.02f;
            g_particles->curlOctaves = 2;
            g_particles->emitRate = 6000.0f;
            g_particles->emitterSize = 0.8f;
            g_particles->drag = 0.02f;
            g_bloom->intensity = 0.3f;
            g_bloom->threshold = 0.6f;
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear Particles")) {
            g_particles->burst(-g_particles->particleCount());  // Not implemented yet
        }
    }
    ImGui::End();
}

VIVID_CHAIN(setup, update)
