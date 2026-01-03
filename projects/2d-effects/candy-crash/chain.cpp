// Candy Crash - Vivid Port
// Original: http://paperjs.org/examples/candy-crash/
// Colorful bouncing balls that squish on collision
// Note: Original uses additive blending which we don't support yet

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace vivid;
using namespace vivid::effects;

constexpr int NUM_BALLS = 18;
constexpr float MAX_VEC = 15.0f;

struct Ball {
    float radius;
    glm::vec2 point;
    glm::vec2 vector;
    int numSegment;
    std::vector<float> boundOffset;      // Actual distance from center (not delta!)
    std::vector<float> boundOffsetBuff;
    std::vector<glm::vec2> sidePoints;   // Unit vectors for each segment direction
    glm::vec3 hsl;

    Ball(float r, const glm::vec2& p, const glm::vec2& v)
        : radius(r), point(p), vector(v) {
        numSegment = static_cast<int>(r / 3 + 2);
        boundOffset.resize(numSegment, radius);      // Start at full radius
        boundOffsetBuff.resize(numSegment, radius);
        sidePoints.resize(numSegment);

        // Random hue with full saturation and brightness
        hsl = glm::vec3(std::fmod(rand() * 0.1f, 360.0f), 1.0f, 0.5f);

        // Pre-compute unit direction vectors for each segment
        for (int i = 0; i < numSegment; i++) {
            float angle = (6.28318f / numSegment) * i;
            sidePoints[i] = glm::vec2(std::cos(angle), std::sin(angle));
        }
    }

    glm::vec4 getColor() const {
        // HSL to RGB conversion
        float h = hsl.x / 60.0f;
        float s = hsl.y;
        float l = hsl.z;

        float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
        float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
        float m = l - c / 2.0f;

        float r, g, b;
        if (h < 1) { r = c; g = x; b = 0; }
        else if (h < 2) { r = x; g = c; b = 0; }
        else if (h < 3) { r = 0; g = c; b = x; }
        else if (h < 4) { r = 0; g = x; b = c; }
        else if (h < 5) { r = x; g = 0; b = c; }
        else { r = c; g = 0; b = x; }

        return glm::vec4(r + m, g + m, b + m, 1.0f);
    }

    glm::vec2 getSidePoint(int index) const {
        return point + sidePoints[index] * boundOffset[index];
    }

    float getBoundOffset(const glm::vec2& p) const {
        glm::vec2 diff = point - p;
        float angle = std::atan2(diff.y, diff.x) + 3.14159f;  // 0 to 2*PI
        int idx = static_cast<int>(angle / 6.28318f * numSegment) % numSegment;
        return boundOffset[idx];
    }

    void iterate() {
        checkBorders();
        if (glm::length(vector) > MAX_VEC) {
            vector = glm::normalize(vector) * MAX_VEC;
        }
        point += vector;
        updateShape();
    }

    void checkBorders() {
        float width = 1280.0f;
        float height = 720.0f;

        // Wrap around (like original Paper.js)
        if (point.x < -radius) point.x = width + radius;
        if (point.x > width + radius) point.x = -radius;
        if (point.y < -radius) point.y = height + radius;
        if (point.y > height + radius) point.y = -radius;
    }

    void updateShape() {
        // Spring back toward original radius and smooth with neighbors
        for (int i = 0; i < numSegment; i++) {
            // Minimum bound - can't compress more than 75%
            if (boundOffset[i] < radius / 4.0f) {
                boundOffset[i] = radius / 4.0f;
            }

            int next = (i + 1) % numSegment;
            int prev = (i + numSegment - 1) % numSegment;

            float offset = boundOffset[i];
            // Spring back toward original radius (slow - divide by 15)
            offset += (radius - offset) / 15.0f;
            // Smooth with neighbors
            offset += ((boundOffset[next] + boundOffset[prev]) / 2.0f - offset) / 3.0f;

            boundOffsetBuff[i] = offset;
            boundOffset[i] = offset;
        }
    }

    void calcBounds(Ball& other) {
        // Check if each of our side points penetrates the other ball
        for (int i = 0; i < numSegment; i++) {
            glm::vec2 tp = getSidePoint(i);
            float bLen = other.getBoundOffset(tp);
            float td = glm::length(tp - other.point);
            if (td < bLen) {
                // Push this segment inward
                boundOffsetBuff[i] -= (bLen - td) / 2.0f;
            }
        }
    }

    void updateBounds() {
        for (int i = 0; i < numSegment; i++) {
            boundOffset[i] = boundOffsetBuff[i];
        }
    }

    void react(Ball& other) {
        float dist = glm::length(point - other.point);
        if (dist < radius + other.radius && dist > 0.01f) {
            float overlap = radius + other.radius - dist;
            // Very gentle velocity adjustment (not physical push-apart)
            glm::vec2 direc = glm::normalize(point - other.point) * (overlap * 0.015f);
            vector += direc;
            other.vector -= direc;

            // Calculate boundary deformation
            calcBounds(other);
            other.calcBounds(*this);
            updateBounds();
            other.updateBounds();
        }
    }

    void draw(Canvas& canvas) {
        glm::vec4 color = getColor();

        canvas.beginPath();

        // Get all current side points
        std::vector<glm::vec2> pts(numSegment);
        for (int i = 0; i < numSegment; i++) {
            pts[i] = getSidePoint(i);
        }

        canvas.moveTo(pts[0].x, pts[0].y);

        // Draw smooth curve through points
        for (int i = 0; i < numSegment; i++) {
            int next = (i + 1) % numSegment;
            // Control point is current, end point is midpoint to next
            float midX = (pts[i].x + pts[next].x) / 2.0f;
            float midY = (pts[i].y + pts[next].y) / 2.0f;
            canvas.quadraticCurveTo(pts[i].x, pts[i].y, midX, midY);
        }
        canvas.closePath();

        canvas.fillStyle(color);
        canvas.fill();
    }
};

// Global state
std::vector<Ball> balls;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);

    chain.output("canvas");

    if (chain.hasError()) {
        ctx.setError(chain.error());
        return;
    }

    // Create balls (matching Paper.js: radius 60-120, random positions)
    srand(42);
    for (int i = 0; i < NUM_BALLS; i++) {
        float radius = 60.0f + (rand() % 1000) / 1000.0f * 60.0f;
        glm::vec2 point(
            (rand() % 1000) / 1000.0f * 1280.0f,
            (rand() % 1000) / 1000.0f * 720.0f
        );
        glm::vec2 vector(
            std::cos((rand() % 1000) / 1000.0f * 6.28318f),
            std::sin((rand() % 1000) / 1000.0f * 6.28318f)
        );
        vector *= (rand() % 1000) / 100.0f;  // Random speed 0-10
        balls.emplace_back(radius, point, vector);
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& canvas = chain.get<Canvas>("canvas");

    // Black background
    canvas.clear(0.0f, 0.0f, 0.0f, 1.0f);

    // React (collisions) first - like the original
    for (size_t i = 0; i < balls.size() - 1; i++) {
        for (size_t j = i + 1; j < balls.size(); j++) {
            balls[i].react(balls[j]);
        }
    }

    // Then iterate (move + update shape)
    for (auto& ball : balls) {
        ball.iterate();
    }

    // Draw all balls
    for (auto& ball : balls) {
        ball.draw(canvas);
    }
}

VIVID_CHAIN(setup, update)
