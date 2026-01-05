/**
 * @file test_particle_forces.cpp
 * @brief Unit tests for Particle Force classes
 *
 * Tests parameter defaults, compute() methods, getParam/setParam API,
 * and params() declarations for all force types.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vivid/effects/particle_system.h>  // For Particle struct and ParticleSystem
#include <vivid/effects/forces/all_forces.h>
#include <glm/glm.hpp>
#include <cmath>

using namespace vivid::effects;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

// Helper to create a particle for testing
static Particle makeTestParticle(glm::vec3 pos, glm::vec3 vel = glm::vec3(0), float seed = 0.5f) {
    Particle p;
    p.position = pos;
    p.velocity = vel;
    p.seed = seed;
    p.life = 1.0f;
    p.maxLife = 1.0f;
    p.size = 1.0f;
    return p;
}

// =============================================================================
// GravityForce Tests
// =============================================================================

TEST_CASE("GravityForce", "[forces][gravity]") {
    GravityForce force;

    SECTION("type and name") {
        REQUIRE(force.type() == ForceType::Gravity);
        REQUIRE(force.name() == "Gravity");
    }

    SECTION("default parameters") {
        REQUIRE(force.direction.x() == 0.0f);
        REQUIRE(force.direction.y() == 0.0f);
        REQUIRE(force.direction.z() == 0.0f);
    }

    SECTION("parameter assignment") {
        force.direction.set(0.0f, -9.8f, 0.0f);
        REQUIRE(force.direction.x() == 0.0f);
        REQUIRE(force.direction.y() == -9.8f);
        REQUIRE(force.direction.z() == 0.0f);
    }

    SECTION("compute applies gravity scaled by dt") {
        force.direction.set(0.0f, -10.0f, 0.0f);
        Particle p = makeTestParticle(glm::vec3(0, 0, 0));
        float dt = 0.016f;

        glm::vec3 result = force.compute(p, 0.0f, dt);

        REQUIRE_THAT(result.x, WithinAbs(0.0f, 0.0001f));
        REQUIRE_THAT(result.y, WithinAbs(-10.0f * dt, 0.0001f));
        REQUIRE_THAT(result.z, WithinAbs(0.0f, 0.0001f));
    }

    SECTION("getParam API") {
        force.direction.set(1.0f, -9.8f, 0.5f);
        float out[4] = {0};
        REQUIRE(force.getParam("direction", out));
        REQUIRE(out[0] == 1.0f);
        REQUIRE(out[1] == -9.8f);
        REQUIRE(out[2] == 0.5f);
    }

    SECTION("setParam API") {
        float value[4] = {0.0f, -20.0f, 0.0f, 0.0f};
        REQUIRE(force.setParam("direction", value));
        REQUIRE(force.direction.y() == -20.0f);
    }

    SECTION("params returns declarations") {
        auto decls = force.params();
        REQUIRE(decls.size() == 1);
        REQUIRE(decls[0].name == "direction");
    }

    SECTION("uniformSize is correct") {
        REQUIRE(force.uniformSize() == 16);
    }
}

// =============================================================================
// DragForce Tests
// =============================================================================

TEST_CASE("DragForce", "[forces][drag]") {
    DragForce force;

    SECTION("type and name") {
        REQUIRE(force.type() == ForceType::Drag);
        REQUIRE(force.name() == "Drag");
    }

    SECTION("default parameters") {
        REQUIRE(static_cast<float>(force.coefficient) == 0.0f);
    }

    SECTION("parameter assignment") {
        force.coefficient = 0.5f;
        REQUIRE(static_cast<float>(force.coefficient) == 0.5f);
    }

    SECTION("compute returns zero (drag is multiplicative)") {
        force.coefficient = 1.0f;
        Particle p = makeTestParticle(glm::vec3(0), glm::vec3(10, 0, 0));

        glm::vec3 result = force.compute(p, 0.0f, 0.016f);

        // DragForce compute always returns zero - drag is applied via getDragFactor
        REQUIRE(result.x == 0.0f);
        REQUIRE(result.y == 0.0f);
        REQUIRE(result.z == 0.0f);
    }

    SECTION("getDragFactor calculates correctly") {
        force.coefficient = 2.0f;
        float dt = 0.5f;
        float factor = force.getDragFactor(dt);
        // Factor = 1 - coefficient * dt = 1 - 2 * 0.5 = 0
        REQUIRE_THAT(factor, WithinAbs(0.0f, 0.0001f));
    }

    SECTION("getDragFactor with zero drag returns 1") {
        force.coefficient = 0.0f;
        float factor = force.getDragFactor(0.016f);
        REQUIRE_THAT(factor, WithinAbs(1.0f, 0.0001f));
    }

    SECTION("isVelocityMultiplier returns true") {
        REQUIRE(force.isVelocityMultiplier());
    }

    SECTION("getParam API") {
        force.coefficient = 0.75f;
        float out[4] = {0};
        REQUIRE(force.getParam("coefficient", out));
        REQUIRE(out[0] == 0.75f);
    }

    SECTION("setParam API") {
        float value[4] = {1.5f, 0, 0, 0};
        REQUIRE(force.setParam("coefficient", value));
        REQUIRE(static_cast<float>(force.coefficient) == 1.5f);
    }

    SECTION("params returns declarations") {
        auto decls = force.params();
        REQUIRE(decls.size() == 1);
        REQUIRE(decls[0].name == "coefficient");
    }
}

// =============================================================================
// CurlNoiseForce Tests
// =============================================================================

TEST_CASE("CurlNoiseForce", "[forces][curl]") {
    CurlNoiseForce force;

    SECTION("type and name") {
        REQUIRE(force.type() == ForceType::CurlNoise);
        REQUIRE(force.name() == "CurlNoise");
    }

    SECTION("default parameters") {
        REQUIRE(static_cast<float>(force.strength) == 0.0f);
        REQUIRE(static_cast<float>(force.scale) == 4.0f);
        REQUIRE(static_cast<float>(force.speed) == 0.3f);
        REQUIRE(static_cast<int>(force.octaves) == 3);
        REQUIRE(static_cast<float>(force.epsilon) == 0.1f);
        REQUIRE(static_cast<float>(force.lacunarity) == 2.0f);
        REQUIRE(static_cast<float>(force.persistence) == 0.5f);
    }

    SECTION("parameter assignment") {
        force.strength = 2.0f;
        force.scale = 8.0f;
        force.speed = 0.5f;
        force.octaves = 4;
        force.epsilon = 0.05f;
        force.lacunarity = 2.5f;
        force.persistence = 0.6f;

        REQUIRE(static_cast<float>(force.strength) == 2.0f);
        REQUIRE(static_cast<float>(force.scale) == 8.0f);
        REQUIRE(static_cast<float>(force.speed) == 0.5f);
        REQUIRE(static_cast<int>(force.octaves) == 4);
        REQUIRE(static_cast<float>(force.epsilon) == 0.05f);
        REQUIRE(static_cast<float>(force.lacunarity) == 2.5f);
        REQUIRE(static_cast<float>(force.persistence) == 0.6f);
    }

    SECTION("compute returns zero when strength is zero") {
        force.strength = 0.0f;
        Particle p = makeTestParticle(glm::vec3(1, 2, 3));

        glm::vec3 result = force.compute(p, 0.0f, 0.016f);

        REQUIRE(result.x == 0.0f);
        REQUIRE(result.y == 0.0f);
        REQUIRE(result.z == 0.0f);
    }

    SECTION("compute returns non-zero when strength is set") {
        force.strength = 1.0f;
        force.scale = 4.0f;
        Particle p = makeTestParticle(glm::vec3(1, 2, 3));

        glm::vec3 result = force.compute(p, 1.0f, 0.016f);

        // Curl noise should produce some force
        bool hasForce = (result.x != 0.0f || result.y != 0.0f || result.z != 0.0f);
        REQUIRE(hasForce);
    }

    SECTION("getParam API") {
        force.strength = 1.5f;
        force.scale = 6.0f;
        force.epsilon = 0.05f;
        force.lacunarity = 2.5f;
        force.persistence = 0.6f;
        float out[4] = {0};

        REQUIRE(force.getParam("strength", out));
        REQUIRE(out[0] == 1.5f);

        REQUIRE(force.getParam("scale", out));
        REQUIRE(out[0] == 6.0f);

        REQUIRE(force.getParam("octaves", out));
        REQUIRE(out[0] == 3.0f);  // default octaves as float

        REQUIRE(force.getParam("epsilon", out));
        REQUIRE(out[0] == 0.05f);

        REQUIRE(force.getParam("lacunarity", out));
        REQUIRE(out[0] == 2.5f);

        REQUIRE(force.getParam("persistence", out));
        REQUIRE(out[0] == 0.6f);
    }

    SECTION("setParam API") {
        float value[4] = {3.0f, 0, 0, 0};
        REQUIRE(force.setParam("strength", value));
        REQUIRE(static_cast<float>(force.strength) == 3.0f);

        value[0] = 5.0f;
        REQUIRE(force.setParam("octaves", value));
        REQUIRE(static_cast<int>(force.octaves) == 5);

        value[0] = 0.2f;
        REQUIRE(force.setParam("epsilon", value));
        REQUIRE(static_cast<float>(force.epsilon) == 0.2f);

        value[0] = 3.0f;
        REQUIRE(force.setParam("lacunarity", value));
        REQUIRE(static_cast<float>(force.lacunarity) == 3.0f);

        value[0] = 0.7f;
        REQUIRE(force.setParam("persistence", value));
        REQUIRE(static_cast<float>(force.persistence) == 0.7f);
    }

    SECTION("params returns all declarations") {
        auto decls = force.params();
        REQUIRE(decls.size() == 7);

        std::vector<std::string> names;
        for (const auto& d : decls) names.push_back(d.name);

        REQUIRE(std::find(names.begin(), names.end(), "strength") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "scale") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "speed") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "octaves") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "epsilon") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "lacunarity") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "persistence") != names.end());
    }

    SECTION("uniformSize is 32 bytes") {
        REQUIRE(force.uniformSize() == 32);
    }
}

// =============================================================================
// TurbulenceForce Tests
// =============================================================================

TEST_CASE("TurbulenceForce", "[forces][turbulence]") {
    TurbulenceForce force;

    SECTION("type and name") {
        REQUIRE(force.type() == ForceType::Turbulence);
        REQUIRE(force.name() == "Turbulence");
    }

    SECTION("default parameters") {
        REQUIRE(static_cast<float>(force.strength) == 0.0f);
    }

    SECTION("compute returns zero when strength is zero") {
        force.strength = 0.0f;
        Particle p = makeTestParticle(glm::vec3(0), glm::vec3(0), 0.5f);

        glm::vec3 result = force.compute(p, 0.0f, 0.016f);

        REQUIRE(result.x == 0.0f);
        REQUIRE(result.y == 0.0f);
        REQUIRE(result.z == 0.0f);
    }

    SECTION("compute returns jitter when strength is set") {
        force.strength = 1.0f;
        Particle p = makeTestParticle(glm::vec3(0), glm::vec3(0), 0.5f);

        glm::vec3 result = force.compute(p, 1.0f, 0.016f);

        // Turbulence should produce random jitter
        bool hasJitter = (result.x != 0.0f || result.y != 0.0f || result.z != 0.0f);
        REQUIRE(hasJitter);
    }

    SECTION("getParam API") {
        force.strength = 2.5f;
        float out[4] = {0};
        REQUIRE(force.getParam("strength", out));
        REQUIRE(out[0] == 2.5f);
    }

    SECTION("setParam API") {
        float value[4] = {3.5f, 0, 0, 0};
        REQUIRE(force.setParam("strength", value));
        REQUIRE(static_cast<float>(force.strength) == 3.5f);
    }
}

// =============================================================================
// PointAttractorForce Tests
// =============================================================================

TEST_CASE("PointAttractorForce", "[forces][attractor]") {
    PointAttractorForce force;

    SECTION("type and name") {
        REQUIRE(force.type() == ForceType::PointAttractor);
        REQUIRE(force.name() == "PointAttractor");
    }

    SECTION("default parameters") {
        REQUIRE(force.position.x() == 0.5f);
        REQUIRE(force.position.y() == 0.5f);
        REQUIRE(force.position.z() == 0.0f);
        REQUIRE(static_cast<float>(force.strength) == 0.0f);
        REQUIRE(static_cast<float>(force.radius) == 0.0f);  // unbounded by default
    }

    SECTION("compute returns zero when strength is zero") {
        force.strength = 0.0f;
        Particle p = makeTestParticle(glm::vec3(0, 0, 0));

        glm::vec3 result = force.compute(p, 0.0f, 0.016f);

        REQUIRE(result.x == 0.0f);
        REQUIRE(result.y == 0.0f);
        REQUIRE(result.z == 0.0f);
    }

    SECTION("positive strength attracts particles") {
        force.position.set(1.0f, 0.0f, 0.0f);
        force.strength = 1.0f;
        Particle p = makeTestParticle(glm::vec3(0, 0, 0));

        glm::vec3 result = force.compute(p, 0.0f, 1.0f);

        // Particle at origin should be pulled toward (1,0,0)
        REQUIRE(result.x > 0.0f);  // Attracted in +x direction
    }

    SECTION("negative strength repels particles") {
        force.position.set(1.0f, 0.0f, 0.0f);
        force.strength = -1.0f;
        Particle p = makeTestParticle(glm::vec3(0, 0, 0));

        glm::vec3 result = force.compute(p, 0.0f, 1.0f);

        // Particle at origin should be pushed away from (1,0,0)
        REQUIRE(result.x < 0.0f);  // Repelled in -x direction
    }

    SECTION("force is zero when particle at attractor position") {
        force.position.set(0.0f, 0.0f, 0.0f);
        force.strength = 10.0f;
        Particle p = makeTestParticle(glm::vec3(0, 0, 0));

        glm::vec3 result = force.compute(p, 0.0f, 1.0f);

        // Too close - returns zero
        REQUIRE(result.x == 0.0f);
        REQUIRE(result.y == 0.0f);
        REQUIRE(result.z == 0.0f);
    }

    SECTION("radius=0 uses inverse distance falloff") {
        force.position.set(0.0f, 0.0f, 0.0f);
        force.strength = 1.0f;
        force.radius = 0.0f;  // unbounded

        Particle p1 = makeTestParticle(glm::vec3(1, 0, 0));
        Particle p2 = makeTestParticle(glm::vec3(2, 0, 0));

        glm::vec3 r1 = force.compute(p1, 0.0f, 1.0f);
        glm::vec3 r2 = force.compute(p2, 0.0f, 1.0f);

        // Inverse distance: force at 2x distance should be ~0.5x
        REQUIRE_THAT(r2.x / r1.x, WithinRel(0.5f, 0.01f));
    }

    SECTION("radius>0 uses quadratic falloff") {
        force.position.set(0.0f, 0.0f, 0.0f);
        force.strength = 1.0f;
        force.radius = 2.0f;

        // At distance 0, falloff = 1
        // At distance 1 (halfway to radius), falloff = 1 - (0.5)² = 0.75
        // At distance 2 (at radius), falloff = 0
        Particle pHalf = makeTestParticle(glm::vec3(1, 0, 0));
        Particle pAtRadius = makeTestParticle(glm::vec3(2, 0, 0));
        Particle pBeyond = makeTestParticle(glm::vec3(3, 0, 0));

        glm::vec3 rHalf = force.compute(pHalf, 0.0f, 1.0f);
        glm::vec3 rAtRadius = force.compute(pAtRadius, 0.0f, 1.0f);
        glm::vec3 rBeyond = force.compute(pBeyond, 0.0f, 1.0f);

        // At halfway: falloff = 1 - (1/2)² = 0.75, pulling toward origin (negative)
        REQUIRE_THAT(rHalf.x, WithinRel(-0.75f, 0.01f));

        // At radius: force is zero
        REQUIRE_THAT(rAtRadius.x, WithinAbs(0.0f, 0.0001f));

        // Beyond radius: force is zero
        REQUIRE_THAT(rBeyond.x, WithinAbs(0.0f, 0.0001f));
    }

    SECTION("getParam API") {
        force.position.set(2.0f, 3.0f, 4.0f);
        force.strength = 5.0f;
        force.radius = 3.0f;
        float out[4] = {0};

        REQUIRE(force.getParam("position", out));
        REQUIRE(out[0] == 2.0f);
        REQUIRE(out[1] == 3.0f);
        REQUIRE(out[2] == 4.0f);

        REQUIRE(force.getParam("strength", out));
        REQUIRE(out[0] == 5.0f);

        REQUIRE(force.getParam("radius", out));
        REQUIRE(out[0] == 3.0f);
    }

    SECTION("setParam API") {
        float pos[4] = {1.0f, 2.0f, 3.0f, 0.0f};
        REQUIRE(force.setParam("position", pos));
        REQUIRE(force.position.x() == 1.0f);
        REQUIRE(force.position.y() == 2.0f);
        REQUIRE(force.position.z() == 3.0f);

        float str[4] = {-5.0f, 0, 0, 0};
        REQUIRE(force.setParam("strength", str));
        REQUIRE(static_cast<float>(force.strength) == -5.0f);

        float rad[4] = {4.0f, 0, 0, 0};
        REQUIRE(force.setParam("radius", rad));
        REQUIRE(static_cast<float>(force.radius) == 4.0f);
    }

    SECTION("params returns all declarations") {
        auto decls = force.params();
        REQUIRE(decls.size() == 3);  // position, strength, radius
    }

    SECTION("uniformSize is 32 bytes") {
        REQUIRE(force.uniformSize() == 32);
    }
}

// =============================================================================
// VortexForce Tests
// =============================================================================

TEST_CASE("VortexForce", "[forces][vortex]") {
    VortexForce force;

    SECTION("type and name") {
        REQUIRE(force.type() == ForceType::Vortex);
        REQUIRE(force.name() == "Vortex");
    }

    SECTION("default parameters") {
        REQUIRE(force.center.x() == 0.5f);
        REQUIRE(force.center.y() == 0.5f);
        REQUIRE(force.center.z() == 0.0f);
        REQUIRE(force.axis.x() == 0.0f);
        REQUIRE(force.axis.y() == 1.0f);
        REQUIRE(force.axis.z() == 0.0f);
        REQUIRE(static_cast<float>(force.strength) == 0.0f);
        REQUIRE(static_cast<float>(force.falloff) == 1.0f);
        REQUIRE(static_cast<float>(force.radius) == 0.0f);  // unbounded by default
    }

    SECTION("compute returns zero when strength is zero") {
        force.strength = 0.0f;
        Particle p = makeTestParticle(glm::vec3(1, 0, 0));

        glm::vec3 result = force.compute(p, 0.0f, 0.016f);

        REQUIRE(result.x == 0.0f);
        REQUIRE(result.y == 0.0f);
        REQUIRE(result.z == 0.0f);
    }

    SECTION("positive strength creates CCW rotation (Y axis)") {
        force.center.set(0.0f, 0.0f, 0.0f);
        force.axis.set(0.0f, 1.0f, 0.0f);  // Y-up axis
        force.strength = 1.0f;
        force.falloff = 0.0f;  // No falloff

        // Particle at (1, 0, 0) should get tangential force in -Z direction
        Particle p = makeTestParticle(glm::vec3(1, 0, 0));
        glm::vec3 result = force.compute(p, 0.0f, 1.0f);

        // Cross(Y-axis, normalize(1,0,0)) = Cross((0,1,0), (1,0,0)) = (0,0,-1)
        REQUIRE_THAT(result.z, WithinAbs(-1.0f, 0.01f));
    }

    SECTION("compute returns zero when particle on axis") {
        force.center.set(0.0f, 0.0f, 0.0f);
        force.axis.set(0.0f, 1.0f, 0.0f);
        force.strength = 10.0f;

        // Particle directly above center (on the axis)
        Particle p = makeTestParticle(glm::vec3(0, 5, 0));
        glm::vec3 result = force.compute(p, 0.0f, 1.0f);

        // Distance from axis is 0, should return zero
        REQUIRE_THAT(result.x, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(result.z, WithinAbs(0.0f, 0.001f));
    }

    SECTION("radius=0 uses power falloff") {
        force.center.set(0.0f, 0.0f, 0.0f);
        force.axis.set(0.0f, 1.0f, 0.0f);
        force.strength = 1.0f;
        force.falloff = 1.0f;  // linear falloff
        force.radius = 0.0f;   // unbounded

        Particle p1 = makeTestParticle(glm::vec3(1, 0, 0));
        Particle p2 = makeTestParticle(glm::vec3(2, 0, 0));

        glm::vec3 r1 = force.compute(p1, 0.0f, 1.0f);
        glm::vec3 r2 = force.compute(p2, 0.0f, 1.0f);

        // With linear falloff (1/d), force at 2x distance should be ~0.5x
        REQUIRE_THAT(r2.z / r1.z, WithinRel(0.5f, 0.01f));
    }

    SECTION("radius>0 uses quadratic falloff") {
        force.center.set(0.0f, 0.0f, 0.0f);
        force.axis.set(0.0f, 1.0f, 0.0f);
        force.strength = 1.0f;
        force.radius = 2.0f;

        // At distance 1 (halfway), falloff = 1 - (0.5)² = 0.75
        Particle pHalf = makeTestParticle(glm::vec3(1, 0, 0));
        Particle pAtRadius = makeTestParticle(glm::vec3(2, 0, 0));
        Particle pBeyond = makeTestParticle(glm::vec3(3, 0, 0));

        glm::vec3 rHalf = force.compute(pHalf, 0.0f, 1.0f);
        glm::vec3 rAtRadius = force.compute(pAtRadius, 0.0f, 1.0f);
        glm::vec3 rBeyond = force.compute(pBeyond, 0.0f, 1.0f);

        // At halfway: falloff = 0.75
        REQUIRE_THAT(rHalf.z, WithinRel(-0.75f, 0.01f));

        // At radius: force is zero
        REQUIRE_THAT(rAtRadius.z, WithinAbs(0.0f, 0.0001f));

        // Beyond radius: force is zero
        REQUIRE_THAT(rBeyond.z, WithinAbs(0.0f, 0.0001f));
    }

    SECTION("getParam API") {
        force.center.set(1.0f, 2.0f, 3.0f);
        force.strength = 5.0f;
        force.radius = 4.0f;
        float out[4] = {0};

        REQUIRE(force.getParam("center", out));
        REQUIRE(out[0] == 1.0f);
        REQUIRE(out[1] == 2.0f);
        REQUIRE(out[2] == 3.0f);

        REQUIRE(force.getParam("strength", out));
        REQUIRE(out[0] == 5.0f);

        REQUIRE(force.getParam("radius", out));
        REQUIRE(out[0] == 4.0f);
    }

    SECTION("setParam API") {
        float axis[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        REQUIRE(force.setParam("axis", axis));
        REQUIRE(force.axis.x() == 1.0f);
        REQUIRE(force.axis.y() == 0.0f);

        float rad[4] = {5.0f, 0, 0, 0};
        REQUIRE(force.setParam("radius", rad));
        REQUIRE(static_cast<float>(force.radius) == 5.0f);
    }

    SECTION("params returns all declarations") {
        auto decls = force.params();
        REQUIRE(decls.size() == 6);  // center, axis, strength, falloff, radius, minDistance
    }

    SECTION("uniformSize is 48 bytes") {
        REQUIRE(force.uniformSize() == 48);
    }
}

// =============================================================================
// WindForce Tests
// =============================================================================

TEST_CASE("WindForce", "[forces][wind]") {
    WindForce force;

    SECTION("type and name") {
        REQUIRE(force.type() == ForceType::Wind);
        REQUIRE(force.name() == "Wind");
    }

    SECTION("default parameters") {
        REQUIRE(force.direction.x() == 1.0f);
        REQUIRE(force.direction.y() == 0.0f);
        REQUIRE(force.direction.z() == 0.0f);
        REQUIRE(static_cast<float>(force.strength) == 0.0f);
        REQUIRE(static_cast<float>(force.gustStrength) == 0.0f);
        REQUIRE(static_cast<float>(force.gustFrequency) == 1.0f);
    }

    SECTION("compute returns zero when strength is zero") {
        force.strength = 0.0f;
        Particle p = makeTestParticle(glm::vec3(0, 0, 0));

        glm::vec3 result = force.compute(p, 0.0f, 0.016f);

        REQUIRE(result.x == 0.0f);
        REQUIRE(result.y == 0.0f);
        REQUIRE(result.z == 0.0f);
    }

    SECTION("compute applies wind in direction") {
        force.direction.set(1.0f, 0.0f, 0.0f);
        force.strength = 10.0f;
        force.gustStrength = 0.0f;  // No gusts
        Particle p = makeTestParticle(glm::vec3(0, 0, 0));
        float dt = 0.1f;

        glm::vec3 result = force.compute(p, 0.0f, dt);

        // Wind pushes in +X direction
        REQUIRE_THAT(result.x, WithinAbs(10.0f * dt, 0.001f));
        REQUIRE_THAT(result.y, WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(result.z, WithinAbs(0.0f, 0.001f));
    }

    SECTION("gusts add variation") {
        force.direction.set(1.0f, 0.0f, 0.0f);
        force.strength = 1.0f;
        force.gustStrength = 0.5f;  // 50% gust variation
        force.gustFrequency = 1.0f;

        Particle p1 = makeTestParticle(glm::vec3(0, 0, 0));
        Particle p2 = makeTestParticle(glm::vec3(1, 1, 1));  // Different position

        glm::vec3 r1 = force.compute(p1, 0.0f, 1.0f);
        glm::vec3 r2 = force.compute(p2, 0.0f, 1.0f);

        // Both should have positive X force (wind direction)
        REQUIRE(r1.x > 0.0f);
        REQUIRE(r2.x > 0.0f);

        // Forces should differ due to gust variation at different positions
        REQUIRE(r1.x != r2.x);
    }

    SECTION("getParam API") {
        force.direction.set(0.0f, 1.0f, 0.0f);
        force.strength = 5.0f;
        float out[4] = {0};

        REQUIRE(force.getParam("direction", out));
        REQUIRE(out[0] == 0.0f);
        REQUIRE(out[1] == 1.0f);
        REQUIRE(out[2] == 0.0f);

        REQUIRE(force.getParam("strength", out));
        REQUIRE(out[0] == 5.0f);
    }

    SECTION("setParam API") {
        float dir[4] = {0.0f, -1.0f, 0.0f, 0.0f};
        REQUIRE(force.setParam("direction", dir));
        REQUIRE(force.direction.y() == -1.0f);

        float gust[4] = {0.8f, 0, 0, 0};
        REQUIRE(force.setParam("gustStrength", gust));
        REQUIRE(static_cast<float>(force.gustStrength) == 0.8f);
    }

    SECTION("params returns all declarations") {
        auto decls = force.params();
        REQUIRE(decls.size() == 4);
    }
}

// =============================================================================
// VelocityFieldForce Tests
// =============================================================================

TEST_CASE("VelocityFieldForce", "[forces][velocity_field]") {
    VelocityFieldForce force;

    SECTION("type and name") {
        REQUIRE(force.type() == ForceType::VelocityField);
        REQUIRE(force.name() == "VelocityField");
    }

    SECTION("default parameters") {
        REQUIRE(force.center.x() == 0.5f);
        REQUIRE(force.center.y() == 0.5f);
        REQUIRE(force.center.z() == 0.0f);
        REQUIRE(static_cast<float>(force.strength) == 0.0f);
        REQUIRE(static_cast<float>(force.scale) == 1.0f);
    }

    SECTION("compute returns zero when strength is zero") {
        force.strength = 0.0f;
        Particle p = makeTestParticle(glm::vec3(1, 0, 0));

        glm::vec3 result = force.compute(p, 0.0f, 0.016f);

        REQUIRE(result.x == 0.0f);
        REQUIRE(result.y == 0.0f);
        REQUIRE(result.z == 0.0f);
    }

    SECTION("Circular mode creates rotation") {
        force.mode = VelocityFieldMode::Circular;
        force.center.set(0.0f, 0.0f, 0.0f);
        force.strength = 1.0f;

        // Particle at (1, 0, 0) should get tangential velocity
        Particle p = makeTestParticle(glm::vec3(1, 0, 0));
        glm::vec3 result = force.compute(p, 0.0f, 1.0f);

        // Circular around Z creates perpendicular velocity
        // From (1,0), perpendicular is (0,1) or (0,-1)
        REQUIRE_THAT(std::abs(result.y), WithinRel(1.0f, 0.1f));
        REQUIRE_THAT(result.x, WithinAbs(0.0f, 0.1f));
    }

    SECTION("Radial mode pushes outward") {
        force.mode = VelocityFieldMode::Radial;
        force.center.set(0.0f, 0.0f, 0.0f);
        force.strength = 1.0f;

        Particle p = makeTestParticle(glm::vec3(2, 0, 0));
        glm::vec3 result = force.compute(p, 0.0f, 1.0f);

        // Radial should push outward from center
        REQUIRE(result.x > 0.0f);  // Pushed in +X (away from origin)
    }

    SECTION("Spiral mode combines circular and radial") {
        force.mode = VelocityFieldMode::Spiral;
        force.center.set(0.0f, 0.0f, 0.0f);
        force.strength = 1.0f;

        Particle p = makeTestParticle(glm::vec3(1, 0, 0));
        glm::vec3 result = force.compute(p, 0.0f, 1.0f);

        // Spiral should have both radial and tangential components
        bool hasXComponent = std::abs(result.x) > 0.01f;
        bool hasYComponent = std::abs(result.y) > 0.01f;
        REQUIRE((hasXComponent || hasYComponent));
    }

    SECTION("getParam API") {
        force.strength = 3.0f;
        float out[4] = {0};
        REQUIRE(force.getParam("strength", out));
        REQUIRE(out[0] == 3.0f);
    }

    SECTION("setParam API") {
        float str[4] = {2.5f, 0, 0, 0};
        REQUIRE(force.setParam("strength", str));
        REQUIRE(static_cast<float>(force.strength) == 2.5f);
    }
}

// =============================================================================
// ParticleSystem Force Stack Tests
// =============================================================================

TEST_CASE("ParticleSystem force stack", "[forces][particle_system]") {
    // Basic API tests without GPU context

    SECTION("addForce returns reference") {
        ParticleSystem ps;
        auto& grav = ps.addForce<GravityForce>();
        grav.direction.set(0.0f, -9.8f, 0.0f);
        REQUIRE(grav.direction.y() == -9.8f);
    }

    SECTION("clearForces removes all forces") {
        ParticleSystem ps;
        ps.addForce<GravityForce>();
        ps.addForce<DragForce>();
        ps.addForce<CurlNoiseForce>();

        REQUIRE(ps.forceCount() == 3);

        ps.clearForces();
        REQUIRE(ps.forceCount() == 0);
    }

    SECTION("getForce returns nullptr for missing force") {
        ParticleSystem ps;
        ps.addForce<GravityForce>();

        auto* drag = ps.getForce<DragForce>();
        REQUIRE(drag == nullptr);
    }

    SECTION("getForce returns pointer to existing force") {
        ParticleSystem ps;
        auto& added = ps.addForce<GravityForce>();
        added.direction.set(0.0f, -5.0f, 0.0f);

        auto* found = ps.getForce<GravityForce>();
        REQUIRE(found != nullptr);
        REQUIRE(found->direction.y() == -5.0f);
    }

    SECTION("multiple forces of same type") {
        ParticleSystem ps;
        auto& g1 = ps.addForce<GravityForce>();
        auto& g2 = ps.addForce<GravityForce>();
        g1.direction.set(0.0f, -10.0f, 0.0f);
        g2.direction.set(0.0f, 5.0f, 0.0f);

        REQUIRE(ps.forceCount() == 2);

        // getForce returns first match
        auto* found = ps.getForce<GravityForce>();
        REQUIRE(found->direction.y() == -10.0f);
    }
}
