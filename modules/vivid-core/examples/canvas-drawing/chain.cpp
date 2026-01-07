// Canvas Demo - Vivid Example
// Demonstrates HTML Canvas 2D-style API with shapes, paths, and transforms

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
    hsv.input("canvas");

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

    // Bouncing ball using fillStyle + fillCircle
    float ballX = 640.0f + 200.0f * std::sin(time * 1.5f);
    float ballY = 360.0f + 100.0f * std::sin(time * 2.3f);
    canvas.fillStyle(1.0f, 0.4f, 0.2f, 1.0f);
    canvas.fillCircle(ballX, ballY, 40.0f);

    // Rotating squares using transforms
    for (int i = 0; i < 4; i++) {
        float angle = time * 0.5f + i * 1.5708f;  // 90 degrees apart
        float x = 640.0f + 180.0f * std::cos(angle);
        float y = 360.0f + 180.0f * std::sin(angle);

        canvas.fillStyle(
            0.5f + 0.5f * std::sin(time + i),
            0.5f + 0.5f * std::sin(time + i + 2.0f),
            0.5f + 0.5f * std::sin(time + i + 4.0f),
            1.0f
        );
        canvas.fillRect(x - 25.0f, y - 25.0f, 50.0f, 50.0f);
    }

    // Pulsing ring in center using strokeStyle + path
    float ringRadius = 80.0f + 20.0f * std::sin(time * 3.0f);
    canvas.strokeStyle(0.2f, 0.8f, 1.0f, 1.0f);
    canvas.lineWidth(4.0f);
    canvas.strokeCircle(640.0f, 360.0f, ringRadius);

    // Lines radiating from center using path API
    canvas.strokeStyle(1.0f, 1.0f, 1.0f, 0.5f);
    canvas.lineWidth(2.0f);
    for (int i = 0; i < 12; i++) {
        float angle = i * 6.28f / 12 + time * 0.3f;
        float x1 = 640.0f + 100.0f * std::cos(angle);
        float y1 = 360.0f + 100.0f * std::sin(angle);
        float x2 = 640.0f + 250.0f * std::cos(angle);
        float y2 = 360.0f + 250.0f * std::sin(angle);

        canvas.beginPath();
        canvas.moveTo(x1, y1);
        canvas.lineTo(x2, y2);
        canvas.stroke();
    }

    // Clipping demo (bottom left) - rectangle clipped to circular region
    canvas.save();
    // Create circular clip region
    canvas.beginPath();
    canvas.arc(150.0f, 600.0f, 60.0f, 0.0f, 6.28318f);  // Full circle
    canvas.closePath();
    canvas.clip();

    // Draw gradient rectangle (will be clipped to circle)
    auto clipGrad = canvas.createLinearGradient(50.0f, 550.0f, 250.0f, 650.0f);
    clipGrad.addColorStop(0.0f, 1.0f, 0.0f, 0.5f, 1.0f);  // Pink
    clipGrad.addColorStop(0.5f, 0.5f, 0.0f, 1.0f, 1.0f);  // Purple
    clipGrad.addColorStop(1.0f, 0.0f, 0.5f, 1.0f, 1.0f);  // Cyan
    canvas.fillStyle(clipGrad);
    canvas.fillRect(50.0f, 500.0f, 200.0f, 200.0f);

    // Draw white stripes (also clipped)
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 0.3f);
    for (int i = 0; i < 8; i++) {
        float stripeX = 60.0f + i * 25.0f;
        canvas.fillRect(stripeX, 500.0f, 10.0f, 200.0f);
    }

    canvas.restore();  // Restores clip state

    // Draw border to show the clip boundary
    canvas.strokeStyle(1.0f, 1.0f, 1.0f, 0.8f);
    canvas.lineWidth(2.0f);
    canvas.strokeCircle(150.0f, 600.0f, 60.0f);

    // Spinning star using transform (save/restore demo)
    canvas.save();
    canvas.translate(1100.0f, 150.0f);
    canvas.rotate(time);
    canvas.fillStyle(1.0f, 0.9f, 0.2f, 1.0f);
    // Draw a simple star shape
    canvas.beginPath();
    for (int i = 0; i < 5; i++) {
        float outerAngle = i * 6.28f / 5 - 1.5708f;  // Start at top
        float innerAngle = outerAngle + 6.28f / 10;
        if (i == 0) {
            canvas.moveTo(50.0f * std::cos(outerAngle), 50.0f * std::sin(outerAngle));
        } else {
            canvas.lineTo(50.0f * std::cos(outerAngle), 50.0f * std::sin(outerAngle));
        }
        canvas.lineTo(20.0f * std::cos(innerAngle), 20.0f * std::sin(innerAngle));
    }
    canvas.closePath();
    canvas.fill();
    canvas.restore();

    // Gradient rectangle demo (bottom right)
    auto gradient = canvas.createLinearGradient(900.0f, 550.0f, 1200.0f, 550.0f);
    gradient.addColorStop(0.0f, 1.0f, 0.0f, 0.0f, 1.0f);  // Red
    gradient.addColorStop(0.5f, 1.0f, 1.0f, 0.0f, 1.0f);  // Yellow
    gradient.addColorStop(1.0f, 0.0f, 1.0f, 0.0f, 1.0f);  // Green
    canvas.fillStyle(gradient);
    canvas.fillRect(900.0f, 500.0f, 300.0f, 100.0f);

    // Radial gradient circle (bottom center)
    auto radialGrad = canvas.createRadialGradient(640.0f, 620.0f, 0.0f, 640.0f, 620.0f, 80.0f);
    radialGrad.addColorStop(0.0f, 1.0f, 1.0f, 1.0f, 1.0f);  // White center
    radialGrad.addColorStop(1.0f, 0.0f, 0.0f, 0.5f, 1.0f);  // Dark purple edge
    canvas.fillStyle(radialGrad);
    canvas.fillCircle(640.0f, 620.0f, 80.0f, 64);

    // Animate HSV hue shift
    auto& hsv = chain.get<HSV>("hsv");
    hsv.hueShift = std::fmod(time * 0.1f, 1.0f);  // Slowly cycle through colors
}

VIVID_CHAIN(setup, update)
