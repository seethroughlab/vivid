// Copy Patterns - Vivid Example
// Demonstrates: Copy operator with Linear, Radial, and Grid modes
//
// Creates geometric patterns by replicating shapes with per-copy transforms

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- SOURCE SHAPES -----

    // Circle for linear trail
    auto& circle = chain.add<Shape>("circle");
    circle.type = ShapeType::Circle;
    circle.size.set(0.12f, 0.12f);
    circle.position.set(0.5f, 0.5f);
    circle.color.set(1.0f, 0.4f, 0.2f, 1.0f);
    circle.softness = 0.02f;

    // Triangle for radial array
    auto& triangle = chain.add<Shape>("triangle");
    triangle.type = ShapeType::Polygon;
    triangle.sides = 3;
    triangle.size.set(0.06f, 0.06f);
    triangle.position.set(0.5f, 0.5f);
    triangle.color.set(0.2f, 0.8f, 1.0f, 1.0f);
    triangle.softness = 0.01f;

    // Square for grid
    auto& square = chain.add<Shape>("square");
    square.type = ShapeType::Rectangle;
    square.size.set(0.04f, 0.04f);
    square.position.set(0.5f, 0.5f);
    square.color.set(0.4f, 1.0f, 0.5f, 1.0f);
    square.softness = 0.005f;

    // ----- LINEAR MODE -----
    // Creates trail effects with rotation and scale
    auto& linear = chain.add<Copy>("linear");
    linear.input("circle");
    linear.mode = CopyMode::Linear;
    linear.count = 8;
    linear.offset.set(0.06f, 0.0f);
    linear.rotationStep = 0.15f;
    linear.scaleStep = 0.92f;
    linear.opacityFalloff = 0.12f;

    // ----- RADIAL MODE -----
    // Creates circular arrays like clock faces or flower petals
    auto& radial = chain.add<Copy>("radial");
    radial.input("triangle");
    radial.mode = CopyMode::Radial;
    radial.count = 12;
    radial.radius = 0.25f;
    radial.startAngle = 0.0f;
    radial.endAngle = 6.283f;  // Full circle

    // ----- GRID MODE -----
    // Creates uniform grids
    auto& grid = chain.add<Copy>("grid");
    grid.input("square");
    grid.mode = CopyMode::Grid;
    grid.count = 16;
    grid.columns = 4;
    grid.spacing.set(0.12f, 0.12f);
    grid.opacityFalloff = 0.0f;

    // ----- POST-PROCESSING -----

    // Add bloom to linear for glow trails
    auto& bloom_linear = chain.add<Bloom>("bloom_linear");
    bloom_linear.input("linear");
    bloom_linear.threshold = 0.3f;
    bloom_linear.intensity = 1.2f;
    bloom_linear.radius = 15.0f;

    // Add bloom to radial
    auto& bloom_radial = chain.add<Bloom>("bloom_radial");
    bloom_radial.input("radial");
    bloom_radial.threshold = 0.4f;
    bloom_radial.intensity = 0.8f;
    bloom_radial.radius = 10.0f;

    // ----- CANVAS LAYOUT -----
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "bloom_linear");
    canvas.input(1, "bloom_radial");
    canvas.input(2, "grid");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Mouse controls
    float mouseX = ctx.mouseNorm().x;
    float mouseY = ctx.mouseNorm().y;

    // ----- ANIMATE LINEAR -----
    auto& linear = chain.get<Copy>("linear");

    // Rotate the offset direction over time
    float angle = t * 0.5f;
    float dist = 0.04f + mouseX * 0.08f;
    linear.offset.set(std::cos(angle) * dist, std::sin(angle) * dist);

    // Animate rotation step
    linear.rotationStep = 0.1f + std::sin(t * 0.7f) * 0.15f;

    // Scale step creates spiral effect
    linear.scaleStep = 0.88f + mouseY * 0.12f;

    // ----- ANIMATE RADIAL -----
    auto& radial = chain.get<Copy>("radial");

    // Animate radius
    radial.radius = 0.2f + std::sin(t * 0.4f) * 0.08f;

    // Rotate the entire array
    radial.startAngle = t * 0.3f;
    radial.endAngle = t * 0.3f + 6.283f;

    // Vary copy count with mouse
    int radialCount = 6 + static_cast<int>(mouseX * 10.0f);
    radial.count = radialCount;

    // Rotate individual triangles
    auto& triangle = chain.get<Shape>("triangle");
    triangle.rotation = -t * 0.5f;

    // ----- ANIMATE GRID -----
    auto& grid = chain.get<Copy>("grid");

    // Animate spacing
    float sp = 0.1f + std::sin(t * 0.3f) * 0.03f;
    grid.spacing.set(sp, sp);

    // Pulse square color
    auto& square = chain.get<Shape>("square");
    float pulse = 0.5f + std::sin(t * 2.0f) * 0.5f;
    square.color.set(0.4f + pulse * 0.3f, 1.0f, 0.5f + pulse * 0.3f, 1.0f);

    // ----- DRAW LAYOUT -----
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.03f, 0.03f, 0.06f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int pad = 10;

    // Three-column layout
    int colW = w / 3;

    auto& bloom_linear = chain.get<Bloom>("bloom_linear");
    auto& bloom_radial = chain.get<Bloom>("bloom_radial");

    canvas.drawImage(bloom_linear, pad, pad, colW - pad * 2, h - pad * 2);
    canvas.drawImage(bloom_radial, colW + pad, pad, colW - pad * 2, h - pad * 2);
    canvas.drawImage(grid, colW * 2 + pad, pad, colW - pad * 2, h - pad * 2);

    // Labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, h - 32, 200, 24);
    canvas.fillRect(colW + pad, h - 32, 200, 24);
    canvas.fillRect(colW * 2 + pad, h - 32, 200, 24);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Linear: Trail Effect", pad + 5, h - 14);

    char radialLabel[64];
    snprintf(radialLabel, sizeof(radialLabel), "Radial: %d copies", radialCount);
    canvas.fillText(radialLabel, colW + pad + 5, h - 14);

    canvas.fillText("Grid: 4x4 Array", colW * 2 + pad + 5, h - 14);
}

VIVID_CHAIN(setup, update)
