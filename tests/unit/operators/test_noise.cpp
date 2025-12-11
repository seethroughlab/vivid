/**
 * @file test_noise.cpp
 * @brief Unit tests for Noise operator
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/effects/noise.h>

using namespace vivid::effects;
using Catch::Matchers::WithinAbs;

TEST_CASE("Noise operator parameter defaults", "[operators][noise]") {
    Noise noise;
    float out[4] = {0};

    SECTION("scale defaults to 4.0") {
        REQUIRE(noise.getParam("scale", out));
        REQUIRE_THAT(out[0], WithinAbs(4.0f, 0.001f));
    }

    SECTION("speed defaults to 0.5") {
        REQUIRE(noise.getParam("speed", out));
        REQUIRE_THAT(out[0], WithinAbs(0.5f, 0.001f));
    }

    SECTION("octaves defaults to 4") {
        REQUIRE(noise.getParam("octaves", out));
        REQUIRE(static_cast<int>(out[0]) == 4);
    }

    SECTION("lacunarity defaults to 2.0") {
        REQUIRE(noise.getParam("lacunarity", out));
        REQUIRE_THAT(out[0], WithinAbs(2.0f, 0.001f));
    }

    SECTION("persistence defaults to 0.5") {
        REQUIRE(noise.getParam("persistence", out));
        REQUIRE_THAT(out[0], WithinAbs(0.5f, 0.001f));
    }

    SECTION("offset defaults to (0,0,0)") {
        REQUIRE(noise.getParam("offset", out));
        REQUIRE_THAT(out[0], WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(out[1], WithinAbs(0.0f, 0.001f));
        REQUIRE_THAT(out[2], WithinAbs(0.0f, 0.001f));
    }
}

TEST_CASE("Noise operator fluent API", "[operators][noise]") {
    Noise noise;
    float out[4] = {0};

    SECTION("scale setter works and chains") {
        Noise& ref = noise.scale(8.0f);
        REQUIRE(&ref == &noise);  // Returns self for chaining
        REQUIRE(noise.getParam("scale", out));
        REQUIRE_THAT(out[0], WithinAbs(8.0f, 0.001f));
    }

    SECTION("speed setter works and chains") {
        Noise& ref = noise.speed(2.0f);
        REQUIRE(&ref == &noise);
        REQUIRE(noise.getParam("speed", out));
        REQUIRE_THAT(out[0], WithinAbs(2.0f, 0.001f));
    }

    SECTION("octaves setter works and chains") {
        Noise& ref = noise.octaves(6);
        REQUIRE(&ref == &noise);
        REQUIRE(noise.getParam("octaves", out));
        REQUIRE(static_cast<int>(out[0]) == 6);
    }

    SECTION("method chaining works") {
        noise.scale(10.0f).speed(1.0f).octaves(2).lacunarity(3.0f).persistence(0.25f);

        REQUIRE(noise.getParam("scale", out));
        REQUIRE_THAT(out[0], WithinAbs(10.0f, 0.001f));

        REQUIRE(noise.getParam("speed", out));
        REQUIRE_THAT(out[0], WithinAbs(1.0f, 0.001f));

        REQUIRE(noise.getParam("octaves", out));
        REQUIRE(static_cast<int>(out[0]) == 2);
    }
}

TEST_CASE("Noise operator setParam/getParam", "[operators][noise]") {
    Noise noise;
    float out[4] = {0};

    SECTION("setParam updates values") {
        float newScale[4] = {12.0f, 0, 0, 0};
        REQUIRE(noise.setParam("scale", newScale));
        REQUIRE(noise.getParam("scale", out));
        REQUIRE_THAT(out[0], WithinAbs(12.0f, 0.001f));
    }

    SECTION("setParam offset sets xyz") {
        float newOffset[4] = {1.0f, 2.0f, 3.0f, 0};
        REQUIRE(noise.setParam("offset", newOffset));
        REQUIRE(noise.getParam("offset", out));
        REQUIRE_THAT(out[0], WithinAbs(1.0f, 0.001f));
        REQUIRE_THAT(out[1], WithinAbs(2.0f, 0.001f));
        REQUIRE_THAT(out[2], WithinAbs(3.0f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float dummy[4] = {0};
        REQUIRE_FALSE(noise.getParam("nonexistent", out));
        REQUIRE_FALSE(noise.setParam("nonexistent", dummy));
    }
}

TEST_CASE("Noise operator params() declaration", "[operators][noise]") {
    Noise noise;
    auto params = noise.params();

    SECTION("has expected number of params") {
        REQUIRE(params.size() == 6);  // scale, speed, octaves, lacunarity, persistence, offset
    }

    SECTION("param names are correct") {
        std::vector<std::string> names;
        for (const auto& p : params) {
            names.push_back(p.name);
        }
        REQUIRE(std::find(names.begin(), names.end(), "scale") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "speed") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "octaves") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "lacunarity") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "persistence") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "offset") != names.end());
    }
}

TEST_CASE("Noise operator name", "[operators][noise]") {
    Noise noise;
    REQUIRE(noise.name() == "Noise");
}
