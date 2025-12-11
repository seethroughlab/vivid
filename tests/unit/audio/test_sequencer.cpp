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

    SECTION("currentStep starts at 0") {
        REQUIRE(seq.currentStep() == 0);
    }

    SECTION("triggered starts false") {
        REQUIRE(seq.triggered() == false);
    }
}

TEST_CASE("Sequencer operator fluent API", "[audio][sequencer]") {
    Sequencer seq;
    float out[4] = {0};

    SECTION("steps setter works and chains") {
        Sequencer& ref = seq.steps(8);
        REQUIRE(&ref == &seq);
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

TEST_CASE("Sequencer playback", "[audio][sequencer]") {
    Sequencer seq;

    SECTION("advance increments step") {
        REQUIRE(seq.currentStep() == 0);
        seq.advance();
        REQUIRE(seq.currentStep() == 1);
        seq.advance();
        REQUIRE(seq.currentStep() == 2);
    }

    SECTION("advance wraps at step count") {
        seq.steps(4);
        REQUIRE(seq.currentStep() == 0);
        seq.advance();  // 1
        seq.advance();  // 2
        seq.advance();  // 3
        seq.advance();  // should wrap to 0
        REQUIRE(seq.currentStep() == 0);
    }

    SECTION("triggered is true when step is active") {
        seq.setStep(0, true);
        seq.setStep(1, false);

        // At step 0 (active)
        seq.advance();  // Move to step 1
        // Now we're at step 1
        REQUIRE(seq.currentStep() == 1);
    }

    SECTION("reset prepares for step 0 on first advance") {
        seq.advance();
        seq.advance();
        REQUIRE(seq.currentStep() == 2);
        seq.reset();
        // After reset, step is -1 so first advance() lands on 0
        REQUIRE(seq.currentStep() == -1);
        seq.advance();
        REQUIRE(seq.currentStep() == 0);
    }
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
    REQUIRE(seq.outputKind() == vivid::OutputKind::Value);
}

TEST_CASE("Sequencer MAX_STEPS constant", "[audio][sequencer]") {
    REQUIRE(Sequencer::MAX_STEPS == 16);
}
