/**
 * 2D Instancing Example - Bouncing Circles with Physics
 *
 * Demonstrates efficient 2D rendering using GPU instancing via ctx.drawCircles().
 * All circles are rendered in a single draw call regardless of count.
 *
 * Features:
 * - Simple physics simulation (velocity + gravity + collisions)
 * - GPU-instanced circle rendering
 * - Dynamic color based on velocity
 */

#include <vivid/vivid.h>
#include <cmath>
#include <vector>
#include <cstdlib>

using namespace vivid;

// Ball state
struct Ball {
    glm::vec2 position;
    glm::vec2 velocity;
    float radius;
    glm::vec4 color;
};

// Global state
static std::vector<Ball> balls;
static Texture output;
static bool initialized = false;

glm::vec4 hueToRGB(float h) {
    float r, g, b;
    int i = int(h * 6.0f);
    float f = h * 6.0f - i;
    float q = 1.0f - f;

    switch (i % 6) {
        case 0: r = 1; g = f; b = 0; break;
        case 1: r = q; g = 1; b = 0; break;
        case 2: r = 0; g = 1; b = f; break;
        case 3: r = 0; g = q; b = 1; break;
        case 4: r = f; g = 0; b = 1; break;
        case 5: r = 1; g = 0; b = q; break;
        default: r = 1; g = 1; b = 1; break;
    }

    return glm::vec4(r, g, b, 1.0f);
}

void setup(Chain& chain) {
    chain.setOutput("out");
}

void update(Chain& chain, Context& ctx) {
    // Initialize on first frame
    if (!initialized) {
        output = ctx.createTexture();

        // Create initial balls
        const int numBalls = 50;
        balls.reserve(numBalls);

        for (int i = 0; i < numBalls; i++) {
            Ball ball;
            ball.position = glm::vec2(
                0.1f + 0.8f * (float)rand() / RAND_MAX,
                0.1f + 0.8f * (float)rand() / RAND_MAX
            );
            ball.velocity = glm::vec2(
                (float)rand() / RAND_MAX * 0.4f - 0.2f,
                (float)rand() / RAND_MAX * 0.4f - 0.2f
            );
            ball.radius = 0.015f + 0.025f * (float)rand() / RAND_MAX;

            // Random hue
            float hue = (float)rand() / RAND_MAX;
            ball.color = hueToRGB(hue);

            balls.push_back(ball);
        }

        initialized = true;
    }

    float dt = ctx.dt();

    // Physics simulation
    const float gravity = 0.5f;
    const float damping = 0.98f;
    const float bounce = 0.85f;

    for (auto& ball : balls) {
        // Apply gravity
        ball.velocity.y -= gravity * dt;

        // Apply damping
        ball.velocity *= damping;

        // Update position
        ball.position += ball.velocity * dt;

        // Boundary collisions
        if (ball.position.x - ball.radius < 0.0f) {
            ball.position.x = ball.radius;
            ball.velocity.x *= -bounce;
        }
        if (ball.position.x + ball.radius > 1.0f) {
            ball.position.x = 1.0f - ball.radius;
            ball.velocity.x *= -bounce;
        }
        if (ball.position.y - ball.radius < 0.0f) {
            ball.position.y = ball.radius;
            ball.velocity.y *= -bounce;
        }
        if (ball.position.y + ball.radius > 1.0f) {
            ball.position.y = 1.0f - ball.radius;
            ball.velocity.y *= -bounce;
        }

        // Update alpha based on speed (brighter when moving fast)
        float speed = glm::length(ball.velocity);
        float brightness = 0.5f + 0.5f * std::min(speed * 3.0f, 1.0f);
        ball.color.a = brightness;
    }

    // Ball-to-ball collisions
    for (size_t i = 0; i < balls.size(); i++) {
        for (size_t j = i + 1; j < balls.size(); j++) {
            glm::vec2 diff = balls[j].position - balls[i].position;
            float dist = glm::length(diff);
            float minDist = balls[i].radius + balls[j].radius;

            if (dist < minDist && dist > 0.001f) {
                glm::vec2 normal = diff / dist;

                // Separate balls
                float overlap = minDist - dist;
                balls[i].position -= normal * (overlap * 0.5f);
                balls[j].position += normal * (overlap * 0.5f);

                // Elastic collision
                glm::vec2 relVel = balls[j].velocity - balls[i].velocity;
                float velAlongNormal = glm::dot(relVel, normal);

                if (velAlongNormal < 0) {
                    float restitution = 0.9f;
                    float impulse = -(1.0f + restitution) * velAlongNormal / 2.0f;
                    balls[i].velocity -= impulse * normal;
                    balls[j].velocity += impulse * normal;
                }
            }
        }
    }

    // Convert to Circle2D for instanced rendering
    std::vector<Circle2D> circles;
    circles.reserve(balls.size());

    for (const auto& ball : balls) {
        circles.emplace_back(ball.position, ball.radius, ball.color);
    }

    // Render all circles in one GPU instanced draw call
    ctx.drawCircles(circles, output, glm::vec4(0.05f, 0.05f, 0.1f, 1.0f));
    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
