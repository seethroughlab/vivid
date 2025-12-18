/**
 * @file test_effects_2d.cpp
 * @brief Unit tests for Core 2D Effects operators
 *
 * Tests parameter defaults, setParam/getParam API, params() declarations,
 * and operator configuration. These tests don't require GPU context.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// Core 2D Effects
#include <vivid/effects/noise.h>
#include <vivid/effects/lfo.h>
#include <vivid/effects/blur.h>
#include <vivid/effects/composite.h>
#include <vivid/effects/solid_color.h>
#include <vivid/effects/gradient.h>
#include <vivid/effects/shape.h>
#include <vivid/effects/transform.h>

using namespace vivid;
using namespace vivid::effects;
using Catch::Matchers::WithinRel;

// =============================================================================
// Noise Operator Tests
// =============================================================================

TEST_CASE("Noise operator", "[effects][noise]") {
    Noise noise;

    SECTION("name returns 'Noise'") {
        REQUIRE(noise.name() == "Noise");
    }

    SECTION("parameter defaults") {
        REQUIRE(static_cast<float>(noise.scale) == 4.0f);
        REQUIRE(static_cast<float>(noise.speed) == 0.5f);
        REQUIRE(static_cast<int>(noise.octaves) == 4);
        REQUIRE(static_cast<float>(noise.lacunarity) == 2.0f);
        REQUIRE(static_cast<float>(noise.persistence) == 0.5f);
        REQUIRE(noise.offset.x() == 0.0f);
        REQUIRE(noise.offset.y() == 0.0f);
        REQUIRE(noise.offset.z() == 0.0f);
    }

    SECTION("direct parameter assignment") {
        noise.scale = 10.0f;
        noise.speed = 2.0f;
        noise.octaves = 6;
        noise.lacunarity = 3.0f;
        noise.persistence = 0.7f;

        REQUIRE(static_cast<float>(noise.scale) == 10.0f);
        REQUIRE(static_cast<float>(noise.speed) == 2.0f);
        REQUIRE(static_cast<int>(noise.octaves) == 6);
        REQUIRE(static_cast<float>(noise.lacunarity) == 3.0f);
        REQUIRE(static_cast<float>(noise.persistence) == 0.7f);
    }

    SECTION("getParam API") {
        noise.scale = 8.0f;

        float out[4] = {0};
        REQUIRE(noise.getParam("scale", out));
        REQUIRE(out[0] == 8.0f);

        REQUIRE(noise.getParam("speed", out));
        REQUIRE(out[0] == 0.5f);  // default

        REQUIRE(noise.getParam("octaves", out));
        REQUIRE(out[0] == 4.0f);  // default (int as float)
    }

    SECTION("setParam API") {
        float value[4] = {12.0f, 0, 0, 0};
        REQUIRE(noise.setParam("scale", value));
        REQUIRE(static_cast<float>(noise.scale) == 12.0f);

        value[0] = 3.0f;
        REQUIRE(noise.setParam("speed", value));
        REQUIRE(static_cast<float>(noise.speed) == 3.0f);

        value[0] = 7.0f;
        REQUIRE(noise.setParam("octaves", value));
        REQUIRE(static_cast<int>(noise.octaves) == 7);
    }

    SECTION("setParam returns false for unknown param") {
        float value[4] = {1.0f, 0, 0, 0};
        REQUIRE_FALSE(noise.setParam("nonexistent", value));
    }

    SECTION("getParam returns false for unknown param") {
        float out[4] = {0};
        REQUIRE_FALSE(noise.getParam("nonexistent", out));
    }

    SECTION("params() returns declarations") {
        auto params = noise.params();
        REQUIRE(params.size() >= 5);

        // Check that scale is in the list
        bool foundScale = false;
        for (const auto& p : params) {
            if (p.name == "scale") {
                foundScale = true;
                REQUIRE(p.type == ParamType::Float);
                REQUIRE(p.minVal == 0.1f);
                REQUIRE(p.maxVal == 20.0f);
            }
        }
        REQUIRE(foundScale);
    }

    SECTION("Vec3Param offset") {
        noise.offset.set(1.0f, 2.0f, 3.0f);
        REQUIRE(noise.offset.x() == 1.0f);
        REQUIRE(noise.offset.y() == 2.0f);
        REQUIRE(noise.offset.z() == 3.0f);
    }
}

// =============================================================================
// LFO Operator Tests
// =============================================================================

TEST_CASE("LFO operator", "[effects][lfo]") {
    LFO lfo;

    SECTION("name returns 'LFO'") {
        REQUIRE(lfo.name() == "LFO");
    }

    SECTION("parameter defaults") {
        REQUIRE(static_cast<float>(lfo.frequency) == 1.0f);
        REQUIRE(static_cast<float>(lfo.amplitude) == 1.0f);
        REQUIRE(static_cast<float>(lfo.offset) == 0.0f);
        REQUIRE(static_cast<float>(lfo.phase) == 0.0f);
        REQUIRE(static_cast<float>(lfo.pulseWidth) == 0.5f);
    }

    SECTION("direct parameter assignment") {
        lfo.frequency = 2.5f;
        lfo.amplitude = 0.8f;
        lfo.offset = 0.2f;
        lfo.phase = 0.25f;
        lfo.pulseWidth = 0.75f;

        REQUIRE(static_cast<float>(lfo.frequency) == 2.5f);
        REQUIRE(static_cast<float>(lfo.amplitude) == 0.8f);
        REQUIRE(static_cast<float>(lfo.offset) == 0.2f);
        REQUIRE(static_cast<float>(lfo.phase) == 0.25f);
        REQUIRE(static_cast<float>(lfo.pulseWidth) == 0.75f);
    }

    SECTION("getParam API") {
        lfo.frequency = 5.0f;

        float out[4] = {0};
        REQUIRE(lfo.getParam("frequency", out));
        REQUIRE(out[0] == 5.0f);

        REQUIRE(lfo.getParam("amplitude", out));
        REQUIRE(out[0] == 1.0f);  // default
    }

    SECTION("setParam API") {
        float value[4] = {10.0f, 0, 0, 0};
        REQUIRE(lfo.setParam("frequency", value));
        REQUIRE(static_cast<float>(lfo.frequency) == 10.0f);

        value[0] = 0.5f;
        REQUIRE(lfo.setParam("amplitude", value));
        REQUIRE(static_cast<float>(lfo.amplitude) == 0.5f);
    }

    SECTION("outputKind returns Value") {
        REQUIRE(lfo.outputKind() == OutputKind::Value);
    }

    SECTION("params() returns declarations") {
        auto params = lfo.params();
        REQUIRE(params.size() >= 5);

        bool foundFrequency = false;
        for (const auto& p : params) {
            if (p.name == "frequency") {
                foundFrequency = true;
                REQUIRE(p.type == ParamType::Float);
                REQUIRE(p.minVal == 0.01f);
                REQUIRE(p.maxVal == 20.0f);
            }
        }
        REQUIRE(foundFrequency);
    }

    SECTION("getUniforms returns current state") {
        lfo.frequency = 3.0f;
        lfo.amplitude = 0.6f;
        lfo.offset = 0.1f;

        auto uniforms = lfo.getUniforms();
        REQUIRE(uniforms.frequency == 3.0f);
        REQUIRE(uniforms.amplitude == 0.6f);
        REQUIRE(uniforms.offset == 0.1f);
    }
}

// =============================================================================
// Blur Operator Tests
// =============================================================================

TEST_CASE("Blur operator", "[effects][blur]") {
    Blur blur;

    SECTION("name returns 'Blur'") {
        REQUIRE(blur.name() == "Blur");
    }

    SECTION("parameter defaults") {
        REQUIRE(static_cast<float>(blur.radius) == 5.0f);
        REQUIRE(static_cast<int>(blur.passes) == 1);
    }

    SECTION("direct parameter assignment") {
        blur.radius = 15.0f;
        blur.passes = 3;

        REQUIRE(static_cast<float>(blur.radius) == 15.0f);
        REQUIRE(static_cast<int>(blur.passes) == 3);
    }

    SECTION("getParam API") {
        blur.radius = 20.0f;

        float out[4] = {0};
        REQUIRE(blur.getParam("radius", out));
        REQUIRE(out[0] == 20.0f);

        REQUIRE(blur.getParam("passes", out));
        REQUIRE(out[0] == 1.0f);  // default
    }

    SECTION("setParam API") {
        float value[4] = {25.0f, 0, 0, 0};
        REQUIRE(blur.setParam("radius", value));
        REQUIRE(static_cast<float>(blur.radius) == 25.0f);

        value[0] = 5.0f;
        REQUIRE(blur.setParam("passes", value));
        REQUIRE(static_cast<int>(blur.passes) == 5);
    }

    SECTION("params() returns declarations") {
        auto params = blur.params();
        REQUIRE(params.size() >= 2);

        bool foundRadius = false;
        bool foundPasses = false;
        for (const auto& p : params) {
            if (p.name == "radius") {
                foundRadius = true;
                REQUIRE(p.type == ParamType::Float);
                REQUIRE(p.minVal == 0.0f);
                REQUIRE(p.maxVal == 50.0f);
            }
            if (p.name == "passes") {
                foundPasses = true;
                REQUIRE(p.type == ParamType::Int);
                REQUIRE(p.minVal == 1.0f);
                REQUIRE(p.maxVal == 10.0f);
            }
        }
        REQUIRE(foundRadius);
        REQUIRE(foundPasses);
    }

    SECTION("zero radius is valid (passthrough)") {
        blur.radius = 0.0f;
        REQUIRE(static_cast<float>(blur.radius) == 0.0f);
    }
}

// =============================================================================
// Composite Operator Tests
// =============================================================================

TEST_CASE("Composite operator", "[effects][composite]") {
    Composite composite;

    SECTION("name returns 'Composite'") {
        REQUIRE(composite.name() == "Composite");
    }

    SECTION("parameter defaults") {
        REQUIRE(static_cast<float>(composite.opacity) == 1.0f);
    }

    SECTION("direct parameter assignment") {
        composite.opacity = 0.7f;
        REQUIRE(static_cast<float>(composite.opacity) == 0.7f);
    }

    SECTION("getParam API") {
        composite.opacity = 0.5f;

        float out[4] = {0};
        REQUIRE(composite.getParam("opacity", out));
        REQUIRE(out[0] == 0.5f);
    }

    SECTION("setParam API") {
        float value[4] = {0.3f, 0, 0, 0};
        REQUIRE(composite.setParam("opacity", value));
        REQUIRE(static_cast<float>(composite.opacity) == 0.3f);
    }

    SECTION("inputCount starts at 0") {
        REQUIRE(composite.inputCount() == 0);
    }

    SECTION("BlendMode names") {
        REQUIRE(std::string(Composite::modeName(BlendMode::Over)) == "Over");
        REQUIRE(std::string(Composite::modeName(BlendMode::Add)) == "Add");
        REQUIRE(std::string(Composite::modeName(BlendMode::Multiply)) == "Multiply");
        REQUIRE(std::string(Composite::modeName(BlendMode::Screen)) == "Screen");
        REQUIRE(std::string(Composite::modeName(BlendMode::Overlay)) == "Overlay");
        REQUIRE(std::string(Composite::modeName(BlendMode::Difference)) == "Difference");
    }

    SECTION("params() returns declarations") {
        auto params = composite.params();
        REQUIRE(params.size() >= 1);

        bool foundOpacity = false;
        for (const auto& p : params) {
            if (p.name == "opacity") {
                foundOpacity = true;
                REQUIRE(p.type == ParamType::Float);
                REQUIRE(p.minVal == 0.0f);
                REQUIRE(p.maxVal == 1.0f);
            }
        }
        REQUIRE(foundOpacity);
    }
}

// =============================================================================
// SolidColor Operator Tests
// =============================================================================

TEST_CASE("SolidColor operator", "[effects][solid_color]") {
    SolidColor solidColor;

    SECTION("name returns 'SolidColor'") {
        REQUIRE(solidColor.name() == "SolidColor");
    }

    SECTION("parameter defaults (black)") {
        REQUIRE(solidColor.color.r() == 0.0f);
        REQUIRE(solidColor.color.g() == 0.0f);
        REQUIRE(solidColor.color.b() == 0.0f);
        REQUIRE(solidColor.color.a() == 1.0f);
    }

    SECTION("color assignment") {
        solidColor.color.set(1.0f, 0.5f, 0.25f, 0.8f);

        REQUIRE(solidColor.color.r() == 1.0f);
        REQUIRE(solidColor.color.g() == 0.5f);
        REQUIRE(solidColor.color.b() == 0.25f);
        REQUIRE(solidColor.color.a() == 0.8f);
    }

    SECTION("getParam API for color") {
        solidColor.color.set(0.1f, 0.2f, 0.3f, 0.4f);

        float out[4] = {0};
        REQUIRE(solidColor.getParam("color", out));
        REQUIRE(out[0] == 0.1f);
        REQUIRE(out[1] == 0.2f);
        REQUIRE(out[2] == 0.3f);
        REQUIRE(out[3] == 0.4f);
    }

    SECTION("setParam API for color") {
        float value[4] = {0.9f, 0.8f, 0.7f, 0.6f};
        REQUIRE(solidColor.setParam("color", value));

        REQUIRE(solidColor.color.r() == 0.9f);
        REQUIRE(solidColor.color.g() == 0.8f);
        REQUIRE(solidColor.color.b() == 0.7f);
        REQUIRE(solidColor.color.a() == 0.6f);
    }

    SECTION("getUniforms returns current color") {
        solidColor.color.set(0.5f, 0.6f, 0.7f, 0.8f);

        auto uniforms = solidColor.getUniforms();
        REQUIRE(uniforms.r == 0.5f);
        REQUIRE(uniforms.g == 0.6f);
        REQUIRE(uniforms.b == 0.7f);
        REQUIRE(uniforms.a == 0.8f);
    }

    SECTION("params() returns color declaration") {
        auto params = solidColor.params();
        REQUIRE(params.size() >= 1);

        bool foundColor = false;
        for (const auto& p : params) {
            if (p.name == "color") {
                foundColor = true;
                REQUIRE(p.type == ParamType::Color);
            }
        }
        REQUIRE(foundColor);
    }
}

// =============================================================================
// Gradient Operator Tests
// =============================================================================

TEST_CASE("Gradient operator", "[effects][gradient]") {
    Gradient gradient;

    SECTION("name returns 'Gradient'") {
        REQUIRE(gradient.name() == "Gradient");
    }

    SECTION("parameter defaults") {
        REQUIRE(static_cast<float>(gradient.angle) == 0.0f);
        REQUIRE(static_cast<float>(gradient.scale) == 1.0f);
        REQUIRE(static_cast<float>(gradient.offset) == 0.0f);
        REQUIRE(gradient.center.x() == 0.5f);
        REQUIRE(gradient.center.y() == 0.5f);
        // Default colorA is black
        REQUIRE(gradient.colorA.r() == 0.0f);
        REQUIRE(gradient.colorA.g() == 0.0f);
        REQUIRE(gradient.colorA.b() == 0.0f);
        // Default colorB is white
        REQUIRE(gradient.colorB.r() == 1.0f);
        REQUIRE(gradient.colorB.g() == 1.0f);
        REQUIRE(gradient.colorB.b() == 1.0f);
    }

    SECTION("direct parameter assignment") {
        gradient.angle = 1.57f;  // 90 degrees
        gradient.scale = 2.0f;
        gradient.offset = 0.5f;
        gradient.center.set(0.25f, 0.75f);

        REQUIRE(static_cast<float>(gradient.angle) == 1.57f);
        REQUIRE(static_cast<float>(gradient.scale) == 2.0f);
        REQUIRE(static_cast<float>(gradient.offset) == 0.5f);
        REQUIRE(gradient.center.x() == 0.25f);
        REQUIRE(gradient.center.y() == 0.75f);
    }

    SECTION("color parameters") {
        gradient.colorA.set(1.0f, 0.0f, 0.0f);  // Red
        gradient.colorB.set(0.0f, 0.0f, 1.0f);  // Blue

        REQUIRE(gradient.colorA.r() == 1.0f);
        REQUIRE(gradient.colorA.g() == 0.0f);
        REQUIRE(gradient.colorA.b() == 0.0f);
        REQUIRE(gradient.colorB.r() == 0.0f);
        REQUIRE(gradient.colorB.g() == 0.0f);
        REQUIRE(gradient.colorB.b() == 1.0f);
    }

    SECTION("getParam API") {
        gradient.angle = 3.14f;

        float out[4] = {0};
        REQUIRE(gradient.getParam("angle", out));
        REQUIRE(out[0] == 3.14f);
    }

    SECTION("setParam API") {
        float value[4] = {2.0f, 0, 0, 0};
        REQUIRE(gradient.setParam("scale", value));
        REQUIRE(static_cast<float>(gradient.scale) == 2.0f);
    }

    SECTION("params() returns declarations") {
        auto params = gradient.params();
        REQUIRE(params.size() >= 6);

        bool foundAngle = false;
        bool foundColorA = false;
        bool foundColorB = false;
        for (const auto& p : params) {
            if (p.name == "angle") {
                foundAngle = true;
                REQUIRE(p.type == ParamType::Float);
            }
            if (p.name == "colorA") {
                foundColorA = true;
                REQUIRE(p.type == ParamType::Color);
            }
            if (p.name == "colorB") {
                foundColorB = true;
                REQUIRE(p.type == ParamType::Color);
            }
        }
        REQUIRE(foundAngle);
        REQUIRE(foundColorA);
        REQUIRE(foundColorB);
    }
}

// =============================================================================
// Shape Operator Tests
// =============================================================================

TEST_CASE("Shape operator", "[effects][shape]") {
    Shape shape;

    SECTION("name returns 'Shape'") {
        REQUIRE(shape.name() == "Shape");
    }

    SECTION("parameter defaults") {
        REQUIRE(shape.size.x() == 0.5f);
        REQUIRE(shape.size.y() == 0.5f);
        REQUIRE(shape.position.x() == 0.5f);
        REQUIRE(shape.position.y() == 0.5f);
        REQUIRE(static_cast<float>(shape.rotation) == 0.0f);
        REQUIRE(static_cast<int>(shape.sides) == 5);
        REQUIRE(static_cast<float>(shape.cornerRadius) == 0.0f);
        REQUIRE(static_cast<float>(shape.thickness) == 0.1f);
        REQUIRE(static_cast<float>(shape.softness) == 0.01f);
        // Default color is white
        REQUIRE(shape.color.r() == 1.0f);
        REQUIRE(shape.color.g() == 1.0f);
        REQUIRE(shape.color.b() == 1.0f);
        REQUIRE(shape.color.a() == 1.0f);
    }

    SECTION("Vec2Param size and position") {
        shape.size.set(0.3f, 0.4f);
        shape.position.set(0.25f, 0.75f);

        REQUIRE(shape.size.x() == 0.3f);
        REQUIRE(shape.size.y() == 0.4f);
        REQUIRE(shape.position.x() == 0.25f);
        REQUIRE(shape.position.y() == 0.75f);
    }

    SECTION("direct parameter assignment") {
        shape.rotation = 1.57f;
        shape.sides = 8;
        shape.cornerRadius = 0.1f;
        shape.thickness = 0.2f;
        shape.softness = 0.05f;

        REQUIRE(static_cast<float>(shape.rotation) == 1.57f);
        REQUIRE(static_cast<int>(shape.sides) == 8);
        REQUIRE(static_cast<float>(shape.cornerRadius) == 0.1f);
        REQUIRE(static_cast<float>(shape.thickness) == 0.2f);
        REQUIRE(static_cast<float>(shape.softness) == 0.05f);
    }

    SECTION("color parameter") {
        shape.color.set(1.0f, 0.5f, 0.0f, 0.9f);  // Orange

        REQUIRE(shape.color.r() == 1.0f);
        REQUIRE(shape.color.g() == 0.5f);
        REQUIRE(shape.color.b() == 0.0f);
        REQUIRE(shape.color.a() == 0.9f);
    }

    SECTION("getParam API") {
        shape.rotation = 3.14f;

        float out[4] = {0};
        REQUIRE(shape.getParam("rotation", out));
        REQUIRE(out[0] == 3.14f);
    }

    SECTION("setParam API") {
        float value[4] = {6.0f, 0, 0, 0};
        REQUIRE(shape.setParam("sides", value));
        REQUIRE(static_cast<int>(shape.sides) == 6);
    }

    SECTION("params() returns declarations") {
        auto params = shape.params();
        REQUIRE(params.size() >= 8);

        bool foundSize = false;
        bool foundColor = false;
        bool foundSides = false;
        for (const auto& p : params) {
            if (p.name == "size") {
                foundSize = true;
                REQUIRE(p.type == ParamType::Vec2);
            }
            if (p.name == "color") {
                foundColor = true;
                REQUIRE(p.type == ParamType::Color);
            }
            if (p.name == "sides") {
                foundSides = true;
                REQUIRE(p.type == ParamType::Int);
            }
        }
        REQUIRE(foundSize);
        REQUIRE(foundColor);
        REQUIRE(foundSides);
    }

    SECTION("getUniforms returns current state") {
        shape.size.set(0.6f, 0.7f);
        shape.rotation = 1.0f;
        shape.sides = 6;
        shape.color.set(0.5f, 0.5f, 0.5f, 1.0f);

        auto uniforms = shape.getUniforms();
        REQUIRE(uniforms.sizeX == 0.6f);
        REQUIRE(uniforms.sizeY == 0.7f);
        REQUIRE(uniforms.rotation == 1.0f);
        REQUIRE(uniforms.sides == 6);
        REQUIRE(uniforms.colorR == 0.5f);
        REQUIRE(uniforms.colorG == 0.5f);
        REQUIRE(uniforms.colorB == 0.5f);
        REQUIRE(uniforms.colorA == 1.0f);
    }
}

// =============================================================================
// Transform Operator Tests
// =============================================================================

TEST_CASE("Transform operator", "[effects][transform]") {
    Transform transform;

    SECTION("name returns 'Transform'") {
        REQUIRE(transform.name() == "Transform");
    }

    SECTION("parameter defaults") {
        REQUIRE(transform.scale.x() == 1.0f);
        REQUIRE(transform.scale.y() == 1.0f);
        REQUIRE(static_cast<float>(transform.rotation) == 0.0f);
        REQUIRE(transform.translate.x() == 0.0f);
        REQUIRE(transform.translate.y() == 0.0f);
        REQUIRE(transform.pivot.x() == 0.5f);
        REQUIRE(transform.pivot.y() == 0.5f);
    }

    SECTION("Vec2Param scale, translate, pivot") {
        transform.scale.set(2.0f, 1.5f);
        transform.translate.set(0.1f, -0.2f);
        transform.pivot.set(0.0f, 0.0f);  // Top-left pivot

        REQUIRE(transform.scale.x() == 2.0f);
        REQUIRE(transform.scale.y() == 1.5f);
        REQUIRE(transform.translate.x() == 0.1f);
        REQUIRE(transform.translate.y() == -0.2f);
        REQUIRE(transform.pivot.x() == 0.0f);
        REQUIRE(transform.pivot.y() == 0.0f);
    }

    SECTION("direct parameter assignment") {
        transform.rotation = 0.785f;  // 45 degrees

        REQUIRE(static_cast<float>(transform.rotation) == 0.785f);
    }

    SECTION("getParam API") {
        transform.rotation = 1.57f;

        float out[4] = {0};
        REQUIRE(transform.getParam("rotation", out));
        REQUIRE(out[0] == 1.57f);
    }

    SECTION("setParam API") {
        float value[4] = {3.14f, 0, 0, 0};
        REQUIRE(transform.setParam("rotation", value));
        REQUIRE(static_cast<float>(transform.rotation) == 3.14f);
    }

    SECTION("params() returns declarations") {
        auto params = transform.params();
        REQUIRE(params.size() >= 4);

        bool foundScale = false;
        bool foundRotation = false;
        bool foundPivot = false;
        for (const auto& p : params) {
            if (p.name == "scale") {
                foundScale = true;
                REQUIRE(p.type == ParamType::Vec2);
            }
            if (p.name == "rotation") {
                foundRotation = true;
                REQUIRE(p.type == ParamType::Float);
            }
            if (p.name == "pivot") {
                foundPivot = true;
                REQUIRE(p.type == ParamType::Vec2);
            }
        }
        REQUIRE(foundScale);
        REQUIRE(foundRotation);
        REQUIRE(foundPivot);
    }

    SECTION("getUniforms returns current state") {
        transform.scale.set(1.5f, 2.0f);
        transform.rotation = 0.5f;
        transform.translate.set(0.1f, 0.2f);
        transform.pivot.set(0.3f, 0.7f);

        auto uniforms = transform.getUniforms();
        REQUIRE(uniforms.scaleX == 1.5f);
        REQUIRE(uniforms.scaleY == 2.0f);
        REQUIRE(uniforms.rotation == 0.5f);
        REQUIRE(uniforms.translateX == 0.1f);
        REQUIRE(uniforms.translateY == 0.2f);
        REQUIRE(uniforms.pivotX == 0.3f);
        REQUIRE(uniforms.pivotY == 0.7f);
    }
}
