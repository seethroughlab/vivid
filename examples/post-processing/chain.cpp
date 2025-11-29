/**
 * Post-Processing Example - Bloom and Vignette Effects
 *
 * Demonstrates the Bloom and Vignette post-processing operators
 * applied to a colorful animated scene.
 *
 * Features:
 * - Animated shapes with bright colors
 * - Bloom effect creating glow around bright areas
 * - Vignette effect darkening edges
 * - Mouse control for effect parameters
 */

#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

void setup(Chain& chain) {
    // Create an animated base scene with bright colors

    // Background gradient
    chain.add<Gradient>("bg")
        .mode(0)  // Linear
        .angle(0.0f)
        .color1(glm::vec3(0.1f, 0.0f, 0.2f))  // Dark purple
        .color2(glm::vec3(0.0f, 0.1f, 0.2f)); // Dark blue

    // Bright animated shape 1 - Pulsing circle
    chain.add<Shape>("circle1")
        .type(Shape::Circle)
        .center(glm::vec2(0.3f, 0.5f))
        .radius(0.12f)
        .color(glm::vec3(1.0f, 0.8f, 0.2f));  // Bright yellow

    // Bright animated shape 2 - Another circle
    chain.add<Shape>("circle2")
        .type(Shape::Circle)
        .center(glm::vec2(0.7f, 0.5f))
        .radius(0.10f)
        .color(glm::vec3(0.2f, 0.8f, 1.0f));  // Cyan

    // Bright animated shape 3 - Star
    chain.add<Shape>("star")
        .type(Shape::Star)
        .center(glm::vec2(0.5f, 0.3f))
        .radius(0.08f)
        .points(5)
        .color(glm::vec3(1.0f, 0.3f, 0.5f));  // Pink

    // Small bright dots (simulating particles)
    chain.add<Shape>("dot1")
        .type(Shape::Circle)
        .center(glm::vec2(0.2f, 0.7f))
        .radius(0.03f)
        .color(glm::vec3(1.0f, 1.0f, 1.0f));  // White

    chain.add<Shape>("dot2")
        .type(Shape::Circle)
        .center(glm::vec2(0.8f, 0.3f))
        .radius(0.025f)
        .color(glm::vec3(1.0f, 0.9f, 0.5f));  // Light yellow

    chain.add<Shape>("dot3")
        .type(Shape::Circle)
        .center(glm::vec2(0.6f, 0.8f))
        .radius(0.02f)
        .color(glm::vec3(0.5f, 1.0f, 0.8f));  // Light green

    // Composite all shapes together
    chain.add<Composite>("scene")
        .inputs({"bg", "circle1", "circle2", "star", "dot1", "dot2", "dot3"})
        .opacity(1.0f);

    // Apply bloom effect - creates glow around bright areas
    chain.add<Bloom>("bloom")
        .input("scene")
        .threshold(0.6f)   // Only glow areas above 60% brightness
        .intensity(1.0f)   // Full bloom intensity
        .radius(15.0f)     // Spread of the glow
        .softness(0.7f)    // Soft threshold transition
        .passes(2);        // Two blur passes for smooth glow

    // Apply vignette effect - darken edges
    chain.add<Vignette>("vignette")
        .input("bloom")
        .intensity(0.6f)   // Medium darkening
        .radius(0.9f)      // Start darkening near edges
        .softness(0.5f);   // Smooth falloff

    chain.setOutput("vignette");
}

void update(Chain& chain, Context& ctx) {
    float time = ctx.time();

    // Animate circle positions
    float wave1 = sinf(time * 2.0f) * 0.05f;
    float wave2 = cosf(time * 1.5f) * 0.05f;

    chain.get<Shape>("circle1").center(glm::vec2(0.3f + wave1, 0.5f + wave2));
    chain.get<Shape>("circle2").center(glm::vec2(0.7f - wave1, 0.5f - wave2));

    // Animate star rotation and size
    float pulse = 0.08f + sinf(time * 3.0f) * 0.02f;
    chain.get<Shape>("star")
        .radius(pulse)
        .rotation(time * 0.5f);

    // Animate small dots
    chain.get<Shape>("dot1").center(glm::vec2(0.2f + sinf(time) * 0.1f, 0.7f));
    chain.get<Shape>("dot2").center(glm::vec2(0.8f, 0.3f + cosf(time * 1.2f) * 0.1f));
    chain.get<Shape>("dot3").center(glm::vec2(0.6f + sinf(time * 0.8f) * 0.08f, 0.8f));

    // Mouse control for bloom parameters
    // Mouse X: bloom intensity (0 to 2)
    float bloomIntensity = ctx.mouseNormX() * 2.0f;
    chain.get<Bloom>("bloom").intensity(bloomIntensity);

    // Mouse Y: bloom threshold (0.3 to 1.0)
    float bloomThreshold = 0.3f + ctx.mouseNormY() * 0.7f;
    chain.get<Bloom>("bloom").threshold(bloomThreshold);

    // Animate vignette subtly
    float vignetteWave = 0.5f + sinf(time * 0.5f) * 0.1f;
    chain.get<Vignette>("vignette").intensity(vignetteWave);
}

VIVID_CHAIN(setup, update)
