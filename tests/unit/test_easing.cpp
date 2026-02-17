/**
 * @file test_easing.cpp
 * @brief Unit tests for EasingCurve (header-only easing functions)
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/easing.h>
#include <string>

using namespace vivid;
using Catch::Matchers::WithinAbs;

TEST_CASE("EasingCurve boundary values", "[unit][easing]") {
    SECTION("all curves return 0 at t=0") {
        REQUIRE(EasingCurve::linear().apply(0.0f)    == 0.0f);
        REQUIRE(EasingCurve::easeIn().apply(0.0f)    == 0.0f);
        REQUIRE(EasingCurve::easeOut().apply(0.0f)   == 0.0f);
        REQUIRE(EasingCurve::easeInOut().apply(0.0f) == 0.0f);
    }

    SECTION("all curves return 1 at t=1") {
        REQUIRE(EasingCurve::linear().apply(1.0f)    == 1.0f);
        REQUIRE(EasingCurve::easeIn().apply(1.0f)    == 1.0f);
        REQUIRE(EasingCurve::easeOut().apply(1.0f)   == 1.0f);
        REQUIRE(EasingCurve::easeInOut().apply(1.0f) == 1.0f);
    }
}

TEST_CASE("EasingCurve known intermediates", "[unit][easing]") {
    SECTION("Linear(0.5) = 0.5") {
        REQUIRE(EasingCurve::linear().apply(0.5f) == 0.5f);
    }

    SECTION("EaseIn(0.5) = 0.25 (t*t)") {
        REQUIRE(EasingCurve::easeIn().apply(0.5f) == 0.25f);
    }

    SECTION("EaseOut(0.5) = 0.75 (t*(2-t))") {
        REQUIRE(EasingCurve::easeOut().apply(0.5f) == 0.75f);
    }

    SECTION("EaseInOut(0.5) = 0.5 (smoothstep)") {
        REQUIRE(EasingCurve::easeInOut().apply(0.5f) == 0.5f);
    }
}

TEST_CASE("EasingCurve qualitative properties", "[unit][easing]") {
    SECTION("EaseIn is slower than linear at start") {
        float t = 0.3f;
        REQUIRE(EasingCurve::easeIn().apply(t) < EasingCurve::linear().apply(t));
    }

    SECTION("EaseOut is faster than linear at start") {
        float t = 0.3f;
        REQUIRE(EasingCurve::easeOut().apply(t) > EasingCurve::linear().apply(t));
    }

    SECTION("EaseInOut is slower than linear at start") {
        float t = 0.2f;
        REQUIRE(EasingCurve::easeInOut().apply(t) < EasingCurve::linear().apply(t));
    }

    SECTION("EaseInOut is faster than linear near end") {
        float t = 0.8f;
        REQUIRE(EasingCurve::easeInOut().apply(t) > EasingCurve::linear().apply(t));
    }
}

TEST_CASE("EasingCurve string round-trip", "[unit][easing]") {
    SECTION("fromString(toString(x)) == x for all types") {
        auto check = [](EasingCurve curve) {
            auto roundTripped = EasingCurve::fromString(curve.toString());
            REQUIRE(roundTripped.type == curve.type);
        };

        check(EasingCurve::linear());
        check(EasingCurve::easeIn());
        check(EasingCurve::easeOut());
        check(EasingCurve::easeInOut());
    }

    SECTION("unknown strings fall back to Linear") {
        auto curve = EasingCurve::fromString("unknown");
        REQUIRE(curve.type == EasingType::Linear);

        curve = EasingCurve::fromString("");
        REQUIRE(curve.type == EasingType::Linear);

        curve = EasingCurve::fromString("cubic-bezier");
        REQUIRE(curve.type == EasingType::Linear);
    }
}

TEST_CASE("EasingCurve default construction", "[unit][easing]") {
    EasingCurve curve;
    REQUIRE(curve.type == EasingType::Linear);
    REQUIRE(curve.apply(0.5f) == 0.5f);
}
