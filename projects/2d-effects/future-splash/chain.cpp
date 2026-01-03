// Future Splash - Vivid Port
// Original: http://paperjs.org/examples/future-splash/
// A dramatic wave simulation that responds to movement

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>
#include <vector>

using namespace vivid;
using namespace vivid::effects;

// Physics constants (matching Paper.js original)
constexpr float FRICTION = 0.8f;
constexpr float MASS = 2.0f;
constexpr float SPRING_STRENGTH = 0.1f;

struct WavePoint {
    glm::vec2 position;
    glm::vec2 previous;
    glm::vec2 velocity{0, 0};
    bool fixed = false;

    void update() {
        if (fixed) return;

        // Velocity from position change
        velocity = position - previous;
        velocity *= FRICTION;

        previous = position;
        position += velocity;
    }
};

struct Spring {
    WavePoint* a;
    WavePoint* b;
    float restLength;
    float strength;

    void update() {
        glm::vec2 diff = b->position - a->position;
        float dist = glm::length(diff);
        if (dist < 0.001f) return;

        float displacement = (dist - restLength) / dist;
        glm::vec2 force = diff * displacement * strength * 0.5f;

        if (!a->fixed) {
            a->position += force;
        }
        if (!b->fixed) {
            b->position -= force;
        }
    }
};

// Global state
std::vector<WavePoint> points;
std::vector<Spring> springs;
float canvasWidth = 1280.0f;
float canvasHeight = 720.0f;

void createWave() {
    points.clear();
    springs.clear();

    int numPoints = 16;
    float spacing = canvasWidth / (numPoints - 1);

    // Create points along the middle of the screen
    for (int i = 0; i < numPoints; i++) {
        WavePoint p;
        p.position = {i * spacing, canvasHeight / 2.0f};
        p.previous = p.position;
        // Fix first and last two points
        p.fixed = (i < 2 || i >= numPoints - 2);
        points.push_back(p);
    }

    // Create springs between adjacent points
    for (size_t i = 0; i < points.size() - 1; i++) {
        Spring s;
        s.a = &points[i];
        s.b = &points[i + 1];
        s.restLength = spacing;
        s.strength = SPRING_STRENGTH;
        springs.push_back(s);
    }
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);

    chain.output("canvas");

    if (chain.hasError()) {
        ctx.setError(chain.error());
        return;
    }

    createWave();
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    auto& canvas = chain.get<Canvas>("canvas");

    // White background (like the original)
    canvas.clear(1.0f, 1.0f, 1.0f, 1.0f);

    // Simulate mouse movement - dramatic oscillation
    float mouseX = canvasWidth / 2.0f + 400.0f * std::sin(time * 0.8f);
    float mouseY = canvasHeight / 2.0f + 200.0f * std::sin(time * 1.7f);

    // Apply mouse interaction - push nearby points
    for (auto& p : points) {
        if (p.fixed) continue;

        glm::vec2 diff = glm::vec2(mouseX, mouseY) - p.position;
        float dist = glm::length(diff);

        if (dist < 150.0f && dist > 1.0f) {
            // Strong push away from mouse
            float strength = (150.0f - dist) / 150.0f;
            strength = strength * strength;  // Quadratic falloff
            p.position.y += (p.position.y - mouseY) * strength * 0.3f;
        }
    }

    // Update springs multiple times for stability
    for (int iter = 0; iter < 8; iter++) {
        for (auto& spring : springs) {
            spring.update();
        }
    }

    // Update point physics
    for (auto& p : points) {
        p.update();
    }

    // Draw filled wave shape
    canvas.beginPath();

    // Start from bottom-left
    canvas.moveTo(0, canvasHeight);

    // Line to first point
    canvas.lineTo(points[0].position.x, points[0].position.y);

    // Smooth curve through all points
    for (size_t i = 0; i < points.size() - 1; i++) {
        float cx = (points[i].position.x + points[i + 1].position.x) / 2.0f;
        float cy = (points[i].position.y + points[i + 1].position.y) / 2.0f;
        canvas.quadraticCurveTo(points[i].position.x, points[i].position.y, cx, cy);
    }

    // Final point
    canvas.lineTo(points.back().position.x, points.back().position.y);

    // Close to bottom-right and back
    canvas.lineTo(canvasWidth, canvasHeight);
    canvas.closePath();

    // Fill with black (like the original)
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 1.0f);
    canvas.fill();
}

VIVID_CHAIN(setup, update)
