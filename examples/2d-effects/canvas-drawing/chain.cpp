// Canvas Demo - Vivid Example
// Demonstrates 2D canvas drawing with shapes and text

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);

    // Add HSV color cycling effect
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input(&canvas);

    chain.output("hsv");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    auto& canvas = chain.get<Canvas>("canvas");

    // Clear canvas with dark blue background
    canvas.clear(0.1f, 0.1f, 0.2f, 1.0f);

    // Bouncing ball
    float ballX = 640.0f + 200.0f * std::sin(time * 1.5f);
    float ballY = 360.0f + 100.0f * std::sin(time * 2.3f);
    canvas.circleFilled(ballX, ballY, 40.0f, {1.0f, 0.4f, 0.2f, 1.0f});

    // Rotating squares
    for (int i = 0; i < 4; i++) {
        float angle = time * 0.5f + i * 1.5708f;  // 90 degrees apart
        float x = 640.0f + 180.0f * std::cos(angle);
        float y = 360.0f + 180.0f * std::sin(angle);

        glm::vec4 color = {
            0.5f + 0.5f * std::sin(time + i),
            0.5f + 0.5f * std::sin(time + i + 2.0f),
            0.5f + 0.5f * std::sin(time + i + 4.0f),
            1.0f
        };

        canvas.rectFilled(x - 25.0f, y - 25.0f, 50.0f, 50.0f, color);
    }

    // Pulsing ring in center
    float ringRadius = 80.0f + 20.0f * std::sin(time * 3.0f);
    canvas.circle(640.0f, 360.0f, ringRadius, 4.0f, {0.2f, 0.8f, 1.0f, 1.0f});

    // Lines radiating from center
    for (int i = 0; i < 12; i++) {
        float angle = i * 6.28f / 12 + time * 0.3f;
        float x1 = 640.0f + 100.0f * std::cos(angle);
        float y1 = 360.0f + 100.0f * std::sin(angle);
        float x2 = 640.0f + 250.0f * std::cos(angle);
        float y2 = 360.0f + 250.0f * std::sin(angle);
        canvas.line(x1, y1, x2, y2, 2.0f, {1.0f, 1.0f, 1.0f, 0.5f});
    }

    // Triangle decoration
    canvas.triangleFilled(
        {100.0f, 650.0f},
        {150.0f, 550.0f},
        {200.0f, 650.0f},
        {0.8f, 0.2f, 0.8f, 1.0f}
    );

    // Animate HSV hue shift
    auto& hsv = chain.get<HSV>("hsv");
    hsv.hueShift = std::fmod(time * 0.1f, 1.0f);  // Slowly cycle through colors
}

VIVID_CHAIN(setup, update)
