// Fluid Simulation - Interactive 2D Navier-Stokes
// GPU-accelerated fluid dynamics with mouse interaction
//
// Controls:
//   Mouse drag - Add force and dye (creates swirling colors)
//   Space      - Clear simulation
//   F          - Toggle fullscreen
//   TAB        - Toggle chain visualizer
//   ESC        - Quit

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

// Global references for update()
static FluidSim* g_fluid = nullptr;
static Bloom* g_bloom = nullptr;

// Color cycling for dye injection
static float g_hue = 0.0f;

// Convert HSV to RGB
glm::vec3 hsvToRgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;

    glm::vec3 rgb;
    if (h < 1.0f/6.0f) rgb = {c, x, 0};
    else if (h < 2.0f/6.0f) rgb = {x, c, 0};
    else if (h < 3.0f/6.0f) rgb = {0, c, x};
    else if (h < 4.0f/6.0f) rgb = {0, x, c};
    else if (h < 5.0f/6.0f) rgb = {x, 0, c};
    else rgb = {c, 0, x};

    return rgb + glm::vec3(m);
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Fluid simulation
    auto& fluid = chain.add<FluidSim>("fluid");
    fluid.viscosity = 0.0001f;       // Low viscosity for water-like behavior
    fluid.dissipation = 0.995f;      // Velocity slowly fades
    fluid.vorticity = 0.4f;          // Add swirling detail
    fluid.dyeDissipation = 0.985f;   // Dye fades slightly slower
    fluid.pressureIterations = 40;   // Accurate pressure solve
    fluid.forceScale = 2.0f;         // Amplify mouse forces
    fluid.clearColor.set(0.02f, 0.02f, 0.05f, 1.0f);  // Dark blue-ish background
    g_fluid = &fluid;

    // Subtle bloom for glow effect
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("fluid");
    bloom.threshold = 0.3f;
    bloom.intensity = 0.4f;
    bloom.radius = 8.0f;
    g_bloom = &bloom;

    chain.output("bloom");
}

void update(Context& ctx) {
    float dt = static_cast<float>(ctx.dt());

    // Toggle fullscreen with F key
    if (ctx.key(GLFW_KEY_F).pressed && !ctx.superHeld()) {
        ctx.fullscreen(!ctx.fullscreen());
    }

    // Clear simulation with Space
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        g_fluid->clear();
    }

    // Mouse interaction - add force and dye when dragging
    if (ctx.mouseButton(0).held) {
        // Get normalized mouse position (0-1)
        glm::vec2 pos = ctx.mouseNorm();

        // Get mouse velocity
        glm::vec2 delta = ctx.mouseDeltaNorm();

        // Add force in direction of mouse movement
        float forceMultiplier = 50.0f;
        g_fluid->addForce(pos.x, pos.y,
                         delta.x * forceMultiplier,
                         delta.y * forceMultiplier,
                         0.02f);  // radius

        // Cycle through colors as we paint
        g_hue = std::fmod(g_hue + dt * 0.3f, 1.0f);
        glm::vec3 color = hsvToRgb(g_hue, 0.8f, 1.0f);

        // Add colorful dye
        g_fluid->addDye(pos.x, pos.y,
                       color.r, color.g, color.b,
                       0.015f,   // radius
                       1.0f);    // alpha
    }

    // Right-click to add force without dye (invisible push)
    if (ctx.mouseButton(1).held) {
        glm::vec2 pos = ctx.mouseNorm();
        glm::vec2 delta = ctx.mouseDeltaNorm();

        g_fluid->addForce(pos.x, pos.y,
                         delta.x * 80.0f,
                         delta.y * 80.0f,
                         0.04f);
    }
}

VIVID_CHAIN(setup, update)
