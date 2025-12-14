/**
 * @file test_blur.cpp
 * @brief Unit tests for Blur operator
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/effects/blur.h>
#include <algorithm>

using namespace vivid::effects;
using Catch::Matchers::WithinAbs;

TEST_CASE("Blur operator parameter defaults", "[operators][blur]") {
    Blur blur;
    float out[4] = {0};

    SECTION("radius defaults to 5.0") {
        REQUIRE(blur.getParam("radius", out));
        REQUIRE_THAT(out[0], WithinAbs(5.0f, 0.001f));
    }

    SECTION("passes defaults to 1") {
        REQUIRE(blur.getParam("passes", out));
        REQUIRE(static_cast<int>(out[0]) == 1);
    }
}

TEST_CASE("Blur operator direct assignment API", "[operators][blur]") {
    Blur blur;
    float out[4] = {0};

    SECTION("direct assignment works for radius") {
        blur.radius = 20.0f;
        REQUIRE(blur.getParam("radius", out));
        REQUIRE_THAT(out[0], WithinAbs(20.0f, 0.001f));
    }

    SECTION("direct assignment works for passes") {
        blur.passes = 3;
        REQUIRE(blur.getParam("passes", out));
        REQUIRE(static_cast<int>(out[0]) == 3);
    }

    SECTION("multiple assignments work") {
        blur.radius = 15.0f;
        blur.passes = 5;

        REQUIRE(blur.getParam("radius", out));
        REQUIRE_THAT(out[0], WithinAbs(15.0f, 0.001f));

        REQUIRE(blur.getParam("passes", out));
        REQUIRE(static_cast<int>(out[0]) == 5);
    }
}

TEST_CASE("Blur operator setParam/getParam", "[operators][blur]") {
    Blur blur;
    float out[4] = {0};

    SECTION("setParam updates values") {
        float newRadius[4] = {25.0f, 0, 0, 0};
        REQUIRE(blur.setParam("radius", newRadius));
        REQUIRE(blur.getParam("radius", out));
        REQUIRE_THAT(out[0], WithinAbs(25.0f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float dummy[4] = {0};
        REQUIRE_FALSE(blur.getParam("nonexistent", out));
        REQUIRE_FALSE(blur.setParam("nonexistent", dummy));
    }
}

TEST_CASE("Blur operator params() declaration", "[operators][blur]") {
    Blur blur;
    auto params = blur.params();

    SECTION("has expected number of params") {
        REQUIRE(params.size() == 2);  // radius, passes
    }

    SECTION("param names are correct") {
        std::vector<std::string> names;
        for (const auto& p : params) {
            names.push_back(p.name);
        }
        REQUIRE(std::find(names.begin(), names.end(), "radius") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "passes") != names.end());
    }
}

TEST_CASE("Blur operator name", "[operators][blur]") {
    Blur blur;
    REQUIRE(blur.name() == "Blur");
}
