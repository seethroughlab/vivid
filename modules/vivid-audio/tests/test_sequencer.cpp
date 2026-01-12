/**
 * @file test_sequencer.cpp
 * @brief Unit tests for Sequencer operator
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/audio/sequencer.h>

using namespace vivid::audio;
using Catch::Matchers::WithinAbs;

TEST_CASE("Sequencer operator parameter defaults", "[audio][sequencer]") {
    Sequencer seq;
    float out[4] = {0};

    SECTION("steps defaults to 16") {
        REQUIRE(seq.getParam("steps", out));
        REQUIRE(static_cast<int>(out[0]) == 16);
    }

    SECTION("currentStep starts at -1 (before first step)") {
        REQUIRE(seq.currentStep() == -1);
    }

    SECTION("triggered starts false") {
        REQUIRE(seq.triggered() == false);
    }
}

TEST_CASE("Sequencer operator public param API", "[audio][sequencer]") {
    Sequencer seq;
    float out[4] = {0};

    SECTION("steps assignment works") {
        seq.steps = 8;
        REQUIRE(seq.getParam("steps", out));
        REQUIRE(static_cast<int>(out[0]) == 8);
    }
}

TEST_CASE("Sequencer pattern editing", "[audio][sequencer]") {
    Sequencer seq;

    SECTION("setStep and getStep work") {
        REQUIRE(seq.getStep(0) == false);
        seq.setStep(0, true);
        REQUIRE(seq.getStep(0) == true);
    }

    SECTION("velocity is stored") {
        seq.setStep(0, true, 0.75f);
        REQUIRE_THAT(seq.getVelocity(0), WithinAbs(0.75f, 0.001f));
    }

    SECTION("default velocity is 1.0") {
        seq.setStep(0, true);
        REQUIRE_THAT(seq.getVelocity(0), WithinAbs(1.0f, 0.001f));
    }

    SECTION("clearPattern clears all steps") {
        seq.setStep(0, true);
        seq.setStep(4, true);
        seq.setStep(8, true);
        seq.clearPattern();
        REQUIRE(seq.getStep(0) == false);
        REQUIRE(seq.getStep(4) == false);
        REQUIRE(seq.getStep(8) == false);
    }

    SECTION("setPattern from bitmask") {
        // Pattern 0x1111 = steps 0, 4, 8, 12
        seq.setPattern(0x1111);
        REQUIRE(seq.getStep(0) == true);
        REQUIRE(seq.getStep(1) == false);
        REQUIRE(seq.getStep(2) == false);
        REQUIRE(seq.getStep(3) == false);
        REQUIRE(seq.getStep(4) == true);
        REQUIRE(seq.getStep(8) == true);
        REQUIRE(seq.getStep(12) == true);
    }
}

TEST_CASE("Sequencer playback state", "[audio][sequencer]") {
    Sequencer seq;

    SECTION("reset sets step to -1") {
        seq.reset();
        REQUIRE(seq.currentStep() == -1);
        REQUIRE(seq.triggered() == false);
    }

    // Note: advance() has been removed - sequencer now advances
    // automatically on audio thread via setTriggerSource()
}

TEST_CASE("Sequencer operator setParam/getParam", "[audio][sequencer]") {
    Sequencer seq;
    float out[4] = {0};

    SECTION("setParam updates values") {
        float newSteps[4] = {12.0f, 0, 0, 0};
        REQUIRE(seq.setParam("steps", newSteps));
        REQUIRE(seq.getParam("steps", out));
        REQUIRE(static_cast<int>(out[0]) == 12);
    }

    SECTION("unknown param returns false") {
        float dummy[4] = {0};
        REQUIRE_FALSE(seq.getParam("nonexistent", out));
        REQUIRE_FALSE(seq.setParam("nonexistent", dummy));
    }
}

TEST_CASE("Sequencer operator params() declaration", "[audio][sequencer]") {
    Sequencer seq;
    auto params = seq.params();

    SECTION("has expected number of params") {
        REQUIRE(params.size() == 1);  // steps
    }

    SECTION("param name is correct") {
        REQUIRE(params[0].name == "steps");
    }
}

TEST_CASE("Sequencer operator name and output kind", "[audio][sequencer]") {
    Sequencer seq;
    REQUIRE(seq.name() == "Sequencer");
    REQUIRE(seq.outputKind() == vivid::OutputKind::Audio);  // AudioOperator
}

TEST_CASE("Sequencer MAX_STEPS constant", "[audio][sequencer]") {
    REQUIRE(Sequencer::MAX_STEPS == 16);
}
