// Curl Vortex Demo
// Interactive particle playground with force parameter tweaking
//
// Controls:
//   TAB - Toggle chain visualizer
//   F   - Toggle fullscreen
//   ESC - Quit

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/forces/all_forces.h>
#include <vivid/gui/imgui.h>

using namespace vivid;
using namespace vivid::effects;

// Global references for ImGui
static ParticleSystem* g_ps = nullptr;
static CurlNoiseForce* g_curl = nullptr;
static VortexForce* g_vortex = nullptr;
static PointAttractorForce* g_attractor = nullptr;
static DragForce* g_drag = nullptr;
static GravityForce* g_gravity = nullptr;
static TurbulenceForce* g_turbulence = nullptr;
static WindForce* g_wind = nullptr;

// Force enabled states
static bool curlEnabled = true;
static bool vortexEnabled = true;
static bool attractorEnabled = true;
static bool dragEnabled = true;
static bool gravityEnabled = false;
static bool turbulenceEnabled = false;
static bool windEnabled = false;

void rebuildForces() {
    g_ps->clearForces();

    if (curlEnabled) {
        g_curl = &g_ps->addForce<CurlNoiseForce>();
        g_curl->strength = 1.2f;
        g_curl->scale = 4.0f;
        g_curl->speed = 0.15f;
        g_curl->octaves = 4;
        g_curl->lacunarity = 2.3f;
        g_curl->persistence = 0.55f;
        g_curl->epsilon = 0.1f;
    } else {
        g_curl = nullptr;
    }

    if (vortexEnabled) {
        g_vortex = &g_ps->addForce<VortexForce>();
        g_vortex->center.set(0.5f, 0.5f, 0.0f);
        g_vortex->axis.set(0.0f, 0.0f, 1.0f);
        g_vortex->strength = 0.6f;
        g_vortex->radius = 0.45f;
    } else {
        g_vortex = nullptr;
    }

    if (attractorEnabled) {
        g_attractor = &g_ps->addForce<PointAttractorForce>();
        g_attractor->position.set(0.5f, 0.5f, 0.0f);
        g_attractor->strength = 0.15f;
        g_attractor->radius = 0.4f;
    } else {
        g_attractor = nullptr;
    }

    if (dragEnabled) {
        g_drag = &g_ps->addForce<DragForce>();
        g_drag->coefficient = 0.08f;
    } else {
        g_drag = nullptr;
    }

    if (gravityEnabled) {
        g_gravity = &g_ps->addForce<GravityForce>();
        g_gravity->direction.set(0.0f, -0.1f, 0.0f);
    } else {
        g_gravity = nullptr;
    }

    if (turbulenceEnabled) {
        g_turbulence = &g_ps->addForce<TurbulenceForce>();
        g_turbulence->strength = 0.3f;
    } else {
        g_turbulence = nullptr;
    }

    if (windEnabled) {
        g_wind = &g_ps->addForce<WindForce>();
        g_wind->direction.set(1.0f, 0.0f, 0.0f);
        g_wind->strength = 0.2f;
        g_wind->gustStrength = 0.1f;
    } else {
        g_wind = nullptr;
    }
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& ps = chain.add<ParticleSystem>("particles");
    g_ps = &ps;

    // 2D screen space with GPU simulation
    ps.space(ParticleSpace::Screen2D);
    ps.simulation(SimulationMode::GPU);
    ps.rendering(RenderMode::Circle);
    ps.additive(true);

    // Emitter
    ps.maxParticles = 12000;
    ps.emitter(PsEmitterShape::Ring);
    ps.emitterPosition.set(0.5f, 0.5f, 0.0f);
    ps.emitterSize = 0.06f;
    ps.emitRate = 1200.0f;

    // Particle properties
    ps.lifeMin = 3.0f;
    ps.lifeMax = 6.0f;
    ps.sizeStart = 0.018f;
    ps.sizeEnd = 0.006f;

    // Setup initial forces
    rebuildForces();

    // Colors
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(0.1f, 0.85f, 1.0f, 0.85f);
    ps.colorEnd.set(0.95f, 0.15f, 0.5f, 0.0f);

    ps.clearColor.set(0.008f, 0.008f, 0.025f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    float dt = static_cast<float>(ctx.dt());

    // Toggle fullscreen
    if (ctx.key(GLFW_KEY_F).pressed) {
        ctx.fullscreen(!ctx.fullscreen());
    }

    // ImGui Panel
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 700), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Force Playground")) {
        ImGui::Text("Particles: %d | FPS: %.0f", g_ps->particleCount(), 1.0f / dt);
        ImGui::Separator();

        // === EMITTER ===
        if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen)) {
            float emitRate = g_ps->emitRate;
            if (ImGui::SliderFloat("Emit Rate", &emitRate, 100.0f, 3000.0f, "%.0f/s")) {
                g_ps->emitRate = emitRate;
            }

            float emitterSize = g_ps->emitterSize;
            if (ImGui::SliderFloat("Emitter Size", &emitterSize, 0.01f, 0.2f)) {
                g_ps->emitterSize = emitterSize;
            }

            float sizeStart = g_ps->sizeStart;
            if (ImGui::SliderFloat("Size Start", &sizeStart, 0.005f, 0.05f)) {
                g_ps->sizeStart = sizeStart;
            }

            float sizeEnd = g_ps->sizeEnd;
            if (ImGui::SliderFloat("Size End", &sizeEnd, 0.002f, 0.03f)) {
                g_ps->sizeEnd = sizeEnd;
            }

            float lifeMin = g_ps->lifeMin;
            float lifeMax = g_ps->lifeMax;
            if (ImGui::SliderFloat("Life Min", &lifeMin, 0.5f, 10.0f, "%.1fs")) {
                g_ps->lifeMin = lifeMin;
            }
            if (ImGui::SliderFloat("Life Max", &lifeMax, 0.5f, 10.0f, "%.1fs")) {
                g_ps->lifeMax = lifeMax;
            }
        }

        // === CURL NOISE ===
        bool curlChanged = ImGui::Checkbox("Curl Noise", &curlEnabled);
        if (curlEnabled && g_curl) {
            ImGui::Indent();
            float strength = static_cast<float>(g_curl->strength);
            if (ImGui::SliderFloat("Strength##curl", &strength, 0.0f, 3.0f)) {
                g_curl->strength = strength;
            }
            float scale = static_cast<float>(g_curl->scale);
            if (ImGui::SliderFloat("Scale##curl", &scale, 0.5f, 10.0f)) {
                g_curl->scale = scale;
            }
            float speed = static_cast<float>(g_curl->speed);
            if (ImGui::SliderFloat("Speed##curl", &speed, 0.0f, 0.5f)) {
                g_curl->speed = speed;
            }
            int octaves = static_cast<int>(g_curl->octaves);
            if (ImGui::SliderInt("Octaves##curl", &octaves, 1, 6)) {
                g_curl->octaves = octaves;
            }
            float lacunarity = static_cast<float>(g_curl->lacunarity);
            if (ImGui::SliderFloat("Lacunarity##curl", &lacunarity, 1.5f, 4.0f)) {
                g_curl->lacunarity = lacunarity;
            }
            float persistence = static_cast<float>(g_curl->persistence);
            if (ImGui::SliderFloat("Persistence##curl", &persistence, 0.1f, 1.0f)) {
                g_curl->persistence = persistence;
            }
            float epsilon = static_cast<float>(g_curl->epsilon);
            if (ImGui::SliderFloat("Epsilon##curl", &epsilon, 0.01f, 0.5f)) {
                g_curl->epsilon = epsilon;
            }
            ImGui::Unindent();
        }

        // === VORTEX ===
        bool vortexChanged = ImGui::Checkbox("Vortex", &vortexEnabled);
        if (vortexEnabled && g_vortex) {
            ImGui::Indent();
            float strength = static_cast<float>(g_vortex->strength);
            if (ImGui::SliderFloat("Strength##vortex", &strength, -2.0f, 2.0f)) {
                g_vortex->strength = strength;
            }
            float radius = static_cast<float>(g_vortex->radius);
            if (ImGui::SliderFloat("Radius##vortex", &radius, 0.0f, 1.0f)) {
                g_vortex->radius = radius;
            }
            ImGui::Unindent();
        }

        // === ATTRACTOR ===
        bool attractorChanged = ImGui::Checkbox("Point Attractor", &attractorEnabled);
        if (attractorEnabled && g_attractor) {
            ImGui::Indent();
            float strength = static_cast<float>(g_attractor->strength);
            if (ImGui::SliderFloat("Strength##attr", &strength, -1.0f, 1.0f)) {
                g_attractor->strength = strength;
            }
            float radius = static_cast<float>(g_attractor->radius);
            if (ImGui::SliderFloat("Radius##attr", &radius, 0.0f, 1.0f)) {
                g_attractor->radius = radius;
            }
            ImGui::Unindent();
        }

        // === DRAG ===
        bool dragChanged = ImGui::Checkbox("Drag", &dragEnabled);
        if (dragEnabled && g_drag) {
            ImGui::Indent();
            float coeff = static_cast<float>(g_drag->coefficient);
            if (ImGui::SliderFloat("Coefficient##drag", &coeff, 0.0f, 1.0f)) {
                g_drag->coefficient = coeff;
            }
            ImGui::Unindent();
        }

        // === GRAVITY ===
        bool gravityChanged = ImGui::Checkbox("Gravity", &gravityEnabled);
        if (gravityEnabled && g_gravity) {
            ImGui::Indent();
            float gx = g_gravity->direction.x();
            float gy = g_gravity->direction.y();
            if (ImGui::SliderFloat("X##grav", &gx, -0.5f, 0.5f)) {
                g_gravity->direction.set(gx, gy, 0.0f);
            }
            if (ImGui::SliderFloat("Y##grav", &gy, -0.5f, 0.5f)) {
                g_gravity->direction.set(gx, gy, 0.0f);
            }
            ImGui::Unindent();
        }

        // === TURBULENCE ===
        bool turbulenceChanged = ImGui::Checkbox("Turbulence", &turbulenceEnabled);
        if (turbulenceEnabled && g_turbulence) {
            ImGui::Indent();
            float strength = static_cast<float>(g_turbulence->strength);
            if (ImGui::SliderFloat("Strength##turb", &strength, 0.0f, 1.0f)) {
                g_turbulence->strength = strength;
            }
            ImGui::Unindent();
        }

        // === WIND ===
        bool windChanged = ImGui::Checkbox("Wind", &windEnabled);
        if (windEnabled && g_wind) {
            ImGui::Indent();
            float strength = static_cast<float>(g_wind->strength);
            if (ImGui::SliderFloat("Strength##wind", &strength, 0.0f, 1.0f)) {
                g_wind->strength = strength;
            }
            float gust = static_cast<float>(g_wind->gustStrength);
            if (ImGui::SliderFloat("Gust##wind", &gust, 0.0f, 0.5f)) {
                g_wind->gustStrength = gust;
            }
            ImGui::Unindent();
        }

        // Rebuild forces if any checkbox changed
        if (curlChanged || vortexChanged || attractorChanged || dragChanged ||
            gravityChanged || turbulenceChanged || windChanged) {
            rebuildForces();
        }

        // === COLORS ===
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Colors", ImGuiTreeNodeFlags_DefaultOpen)) {
            float startCol[4] = {g_ps->colorStart.r(), g_ps->colorStart.g(), g_ps->colorStart.b(), g_ps->colorStart.a()};
            if (ImGui::ColorEdit4("Start Color", startCol)) {
                g_ps->colorStart.set(startCol[0], startCol[1], startCol[2], startCol[3]);
            }

            float endCol[4] = {g_ps->colorEnd.r(), g_ps->colorEnd.g(), g_ps->colorEnd.b(), g_ps->colorEnd.a()};
            if (ImGui::ColorEdit4("End Color", endCol)) {
                g_ps->colorEnd.set(endCol[0], endCol[1], endCol[2], endCol[3]);
            }

            float clearCol[3] = {g_ps->clearColor.r(), g_ps->clearColor.g(), g_ps->clearColor.b()};
            if (ImGui::ColorEdit3("Background", clearCol)) {
                g_ps->clearColor.set(clearCol[0], clearCol[1], clearCol[2], 1.0f);
            }

            bool additive = g_ps->additive();
            if (ImGui::Checkbox("Additive Blending", &additive)) {
                g_ps->additive(additive);
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Reset All")) {
            curlEnabled = true;
            vortexEnabled = true;
            attractorEnabled = true;
            dragEnabled = true;
            gravityEnabled = false;
            turbulenceEnabled = false;
            windEnabled = false;
            rebuildForces();
            g_ps->emitRate = 1200.0f;
            g_ps->emitterSize = 0.06f;
            g_ps->sizeStart = 0.018f;
            g_ps->sizeEnd = 0.006f;
        }
    }
    ImGui::End();
}

// Widescreen for particle visualization
VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1600,
    .windowHeight = 900,
    .resizable = true
}))
