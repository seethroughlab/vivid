// Testing Fixture: Canvas Compositing
// Tests Canvas drawing with clipping and transforms
//
// Visual verification:
// - Multiple overlapping shapes drawn with Canvas API
// - Clip path masking (circular reveal)
// - Transform stack (save/restore)

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);

    chain.output("canvas");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = static_cast<float>(ctx.time());

    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.05f, 0.05f, 0.1f, 1.0f);

    // Background gradient
    auto bgGrad = canvas.createRadialGradient(640.0f, 360.0f, 0.0f, 640.0f, 360.0f, 500.0f);
    bgGrad.addColorStop(0.0f, 0.15f, 0.1f, 0.25f, 1.0f);
    bgGrad.addColorStop(1.0f, 0.02f, 0.02f, 0.05f, 1.0f);
    canvas.fillStyle(bgGrad);
    canvas.fillRect(0, 0, 1280, 720);

    // === Clipping demo (center) ===
    canvas.save();

    // Animated clip circle
    float clipX = 640.0f + std::sin(t * 0.5f) * 100.0f;
    float clipY = 360.0f + std::cos(t * 0.7f) * 80.0f;
    float clipRadius = 150.0f + std::sin(t) * 30.0f;

    canvas.beginPath();
    canvas.arc(clipX, clipY, clipRadius, 0.0f, 6.28318f);
    canvas.closePath();
    canvas.clip();

    // Draw colorful pattern inside clip
    for (int i = 0; i < 8; i++) {
        float angle = i * 0.785f + t * 0.3f;
        float x = clipX + std::cos(angle) * 120.0f;
        float y = clipY + std::sin(angle) * 120.0f;
        canvas.fillStyle(
            0.5f + 0.5f * std::sin(t + i * 0.5f),
            0.5f + 0.5f * std::sin(t + i * 0.7f + 2.0f),
            0.5f + 0.5f * std::sin(t + i * 0.9f + 4.0f),
            1.0f
        );
        canvas.fillCircle(x, y, 60.0f);
    }

    canvas.restore();

    // Draw clip boundary
    canvas.strokeStyle(1.0f, 1.0f, 1.0f, 0.5f);
    canvas.lineWidth(2.0f);
    canvas.strokeCircle(clipX, clipY, clipRadius);

    // === Rotating squares (corners) ===
    float positions[4][2] = {{150, 150}, {1130, 150}, {150, 570}, {1130, 570}};
    for (int i = 0; i < 4; i++) {
        canvas.save();
        canvas.translate(positions[i][0], positions[i][1]);
        canvas.rotate(t * (0.5f + i * 0.2f));

        canvas.fillStyle(
            0.8f - i * 0.15f,
            0.3f + i * 0.15f,
            0.2f + i * 0.2f,
            0.9f
        );
        canvas.fillRect(-40.0f, -40.0f, 80.0f, 80.0f);

        canvas.strokeStyle(1.0f, 1.0f, 1.0f, 0.7f);
        canvas.lineWidth(2.0f);
        canvas.strokeRect(-40.0f, -40.0f, 80.0f, 80.0f);

        canvas.restore();
    }

    // === Orbiting circles ===
    for (int i = 0; i < 6; i++) {
        float angle = t * 0.8f + i * 1.047f;
        float orbitRadius = 250.0f;
        float x = 640.0f + std::cos(angle) * orbitRadius;
        float y = 360.0f + std::sin(angle) * orbitRadius;

        canvas.fillStyle(1.0f, 0.8f, 0.2f, 0.7f);
        canvas.fillCircle(x, y, 20.0f);
    }
}

VIVID_CHAIN(setup, update)
