// 2D Physics Demo
// Demonstrates the 2D shader-based approach with CPU-side physics
//
// Key differences from 3D:
// - 2D uses shaders (SDF) for rendering shapes
// - Physics/state managed in C++, passed as uniforms
// - No depth buffer, no meshes, no camera
//
// Controls:
//   Click: Add impulse to nearest circle
//   Space: Reset positions

#include <vivid/vivid.h>
#include <cmath>
#include <algorithm>

using namespace vivid;

// Physics state for circles
struct Circle {
    float x, y;      // Position (0-1 normalized)
    float vx, vy;    // Velocity
    float radius;    // Radius in screen space
};

static Circle circles[4];
static Texture output;
static bool initialized = false;

void initCircles() {
    // Initialize 4 circles at random-ish positions
    circles[0] = {0.25f, 0.5f, 0.3f, 0.2f, 0.08f};
    circles[1] = {0.75f, 0.5f, -0.2f, 0.3f, 0.06f};
    circles[2] = {0.5f, 0.25f, 0.1f, -0.25f, 0.07f};
    circles[3] = {0.5f, 0.75f, -0.15f, -0.1f, 0.05f};
}

void setup(Chain& chain) {
    chain.setOutput("out");
}

void update(Chain& chain, Context& ctx) {
    if (!initialized) {
        output = ctx.createTexture();
        initCircles();
        initialized = true;
    }

    float dt = ctx.dt();

    // Reset on space
    if (ctx.wasKeyPressed(32)) {  // GLFW_KEY_SPACE
        initCircles();
    }

    // Click adds impulse to nearest circle
    if (ctx.wasMousePressed(0)) {
        float mx = ctx.mouseNormX();
        float my = 1.0f - ctx.mouseNormY();  // Flip Y

        float minDist = 1000.0f;
        int nearest = 0;
        for (int i = 0; i < 4; i++) {
            float dx = circles[i].x - mx;
            float dy = circles[i].y - my;
            float dist = std::sqrt(dx*dx + dy*dy);
            if (dist < minDist) {
                minDist = dist;
                nearest = i;
            }
        }

        // Add impulse away from click
        float dx = circles[nearest].x - mx;
        float dy = circles[nearest].y - my;
        float len = std::sqrt(dx*dx + dy*dy) + 0.001f;
        circles[nearest].vx += (dx / len) * 0.5f;
        circles[nearest].vy += (dy / len) * 0.5f;
    }

    // Physics simulation
    for (int i = 0; i < 4; i++) {
        // Update position
        circles[i].x += circles[i].vx * dt;
        circles[i].y += circles[i].vy * dt;

        // Bounce off walls
        float r = circles[i].radius * 0.5f;  // Approximate radius in normalized coords
        if (circles[i].x < r) {
            circles[i].x = r;
            circles[i].vx = std::abs(circles[i].vx) * 0.9f;
        }
        if (circles[i].x > 1.0f - r) {
            circles[i].x = 1.0f - r;
            circles[i].vx = -std::abs(circles[i].vx) * 0.9f;
        }
        if (circles[i].y < r) {
            circles[i].y = r;
            circles[i].vy = std::abs(circles[i].vy) * 0.9f;
        }
        if (circles[i].y > 1.0f - r) {
            circles[i].y = 1.0f - r;
            circles[i].vy = -std::abs(circles[i].vy) * 0.9f;
        }

        // Simple friction
        circles[i].vx *= 0.995f;
        circles[i].vy *= 0.995f;
    }

    // Circle-circle collision
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            float dx = circles[j].x - circles[i].x;
            float dy = circles[j].y - circles[i].y;
            float dist = std::sqrt(dx*dx + dy*dy);
            float minDist = (circles[i].radius + circles[j].radius) * 0.5f;

            if (dist < minDist && dist > 0.001f) {
                // Separate circles
                float overlap = minDist - dist;
                float nx = dx / dist;
                float ny = dy / dist;

                circles[i].x -= nx * overlap * 0.5f;
                circles[i].y -= ny * overlap * 0.5f;
                circles[j].x += nx * overlap * 0.5f;
                circles[j].y += ny * overlap * 0.5f;

                // Exchange velocities along collision normal
                float v1n = circles[i].vx * nx + circles[i].vy * ny;
                float v2n = circles[j].vx * nx + circles[j].vy * ny;

                circles[i].vx += (v2n - v1n) * nx * 0.9f;
                circles[i].vy += (v2n - v1n) * ny * 0.9f;
                circles[j].vx += (v1n - v2n) * nx * 0.9f;
                circles[j].vy += (v1n - v2n) * ny * 0.9f;
            }
        }
    }

    // Pass circle data to shader via uniforms
    Context::ShaderParams params;
    params.param0 = circles[0].x;
    params.param1 = circles[0].y;
    params.param2 = circles[1].x;
    params.param3 = circles[1].y;
    params.param4 = circles[2].x;
    params.param5 = circles[2].y;
    params.param6 = circles[3].x;
    params.param7 = circles[3].y;
    params.vec0X = circles[0].radius;
    params.vec0Y = circles[1].radius;
    params.vec1X = circles[2].radius;
    params.vec1Y = circles[3].radius;

    // Render using shader (2D approach)
    // Path is relative to project folder
    ctx.runShader("shaders/circles.wgsl", nullptr, output, params);
    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
