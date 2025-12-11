/**
 * @file test_composite.cpp
 * @brief Unit tests for Composite operator
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/effects/composite.h>

using namespace vivid::effects;
using Catch::Matchers::WithinAbs;

TEST_CASE("Composite operator parameter defaults", "[operators][composite]") {
    Composite comp;
    float out[4] = {0};

    SECTION("opacity defaults to 1.0") {
        REQUIRE(comp.getParam("opacity", out));
        REQUIRE_THAT(out[0], WithinAbs(1.0f, 0.001f));
    }

    SECTION("inputCount starts at 0") {
        REQUIRE(comp.inputCount() == 0);
    }
}

TEST_CASE("Composite operator fluent API", "[operators][composite]") {
    Composite comp;
    float out[4] = {0};

    SECTION("opacity setter works and chains") {
        Composite& ref = comp.opacity(0.5f);
        REQUIRE(&ref == &comp);
        REQUIRE(comp.getParam("opacity", out));
        REQUIRE_THAT(out[0], WithinAbs(0.5f, 0.001f));
    }

    SECTION("mode setter chains") {
        Composite& ref = comp.mode(BlendMode::Add);
        REQUIRE(&ref == &comp);
    }

    SECTION("method chaining works") {
        comp.mode(BlendMode::Multiply).opacity(0.75f);

        REQUIRE(comp.getParam("opacity", out));
        REQUIRE_THAT(out[0], WithinAbs(0.75f, 0.001f));
    }
}

TEST_CASE("Composite operator setParam/getParam", "[operators][composite]") {
    Composite comp;
    float out[4] = {0};

    SECTION("setParam updates values") {
        float newOpacity[4] = {0.25f, 0, 0, 0};
        REQUIRE(comp.setParam("opacity", newOpacity));
        REQUIRE(comp.getParam("opacity", out));
        REQUIRE_THAT(out[0], WithinAbs(0.25f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float dummy[4] = {0};
        REQUIRE_FALSE(comp.getParam("nonexistent", out));
        REQUIRE_FALSE(comp.setParam("nonexistent", dummy));
    }
}

TEST_CASE("Composite operator params() declaration", "[operators][composite]") {
    Composite comp;
    auto params = comp.params();

    SECTION("has expected number of params") {
        REQUIRE(params.size() == 1);  // opacity only
    }

    SECTION("param name is correct") {
        REQUIRE(params[0].name == "opacity");
    }
}

TEST_CASE("Composite blend mode names", "[operators][composite]") {
    REQUIRE(std::string(Composite::modeName(BlendMode::Over)) == "Over");
    REQUIRE(std::string(Composite::modeName(BlendMode::Add)) == "Add");
    REQUIRE(std::string(Composite::modeName(BlendMode::Multiply)) == "Multiply");
    REQUIRE(std::string(Composite::modeName(BlendMode::Screen)) == "Screen");
    REQUIRE(std::string(Composite::modeName(BlendMode::Overlay)) == "Overlay");
    REQUIRE(std::string(Composite::modeName(BlendMode::Difference)) == "Difference");
}

TEST_CASE("Composite operator name", "[operators][composite]") {
    Composite comp;
    REQUIRE(comp.name() == "Composite");
}

TEST_CASE("Composite max inputs constant", "[operators][composite]") {
    REQUIRE(COMPOSITE_MAX_INPUTS == 8);
}
