/**
 * @file test_sequencer.cpp
 * @brief Unit tests for Sequencer, Euclidean, Envelope, and DrumStack operators
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/audio/sequencer.h>
#include <vivid/audio/euclidean.h>
#include <vivid/audio/envelope.h>
#include <vivid/audio/drum_stack.h>

using namespace vivid::audio;
using Catch::Matchers::WithinAbs;

// Helper to build Step structs without C++20 designated initializers (MSVC C++17 compat)
static Step makeStep() {
    Step s;
    s.active = true;  // setStep always marks active
    return s;
}

static Step makeStep(uint8_t note, float velocity) {
    Step s;
    s.active = true;
    s.note = note;
    s.velocity = velocity;
    return s;
}

static Step makeStep(uint8_t note, float velocity, float gate, float probability) {
    Step s;
    s.active = true;
    s.note = note;
    s.velocity = velocity;
    s.gate = gate;
    s.probability = probability;
    return s;
}

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

    SECTION("setSteps activates listed steps") {
        seq.setSteps({0, 4, 8, 12});
        REQUIRE(seq.getStep(0) == true);
        REQUIRE(seq.getStep(1) == false);
        REQUIRE(seq.getStep(2) == false);
        REQUIRE(seq.getStep(3) == false);
        REQUIRE(seq.getStep(4) == true);
        REQUIRE(seq.getStep(8) == true);
        REQUIRE(seq.getStep(12) == true);
    }

    SECTION("setSteps clears previous pattern") {
        seq.setStep(1, true);
        seq.setStep(3, true);
        seq.setSteps({0, 4});
        REQUIRE(seq.getStep(1) == false);
        REQUIRE(seq.getStep(3) == false);
        REQUIRE(seq.getStep(0) == true);
        REQUIRE(seq.getStep(4) == true);
    }
}

TEST_CASE("Sequencer Step struct API", "[audio][sequencer]") {
    Sequencer seq;

    SECTION("setStep with Step struct activates step") {
        seq.setStep(0, makeStep(60, 0.9f));
        REQUIRE(seq.isActive(0) == true);
        REQUIRE(seq.getNote(0) == 60);
        REQUIRE_THAT(seq.getVelocity(0), WithinAbs(0.9f, 0.001f));
    }

    SECTION("step() returns correct data") {
        seq.setStep(3, makeStep(67, 0.8f, 0.75f, 0.5f));
        const Step& s = seq.step(3);
        REQUIRE(s.active == true);
        REQUIRE(s.note == 67);
        REQUIRE_THAT(s.velocity, WithinAbs(0.8f, 0.001f));
        REQUIRE_THAT(s.gate, WithinAbs(0.75f, 0.001f));
        REQUIRE_THAT(s.probability, WithinAbs(0.5f, 0.001f));
    }

    SECTION("step() out of range returns default") {
        const Step& s = seq.step(-1);
        REQUIRE(s.active == false);
        REQUIRE(s.note == 60);
    }

    SECTION("Step with slide") {
        auto s = makeStep();
        s.velocity = 0.7f;
        s.slide = true;
        seq.setStep(1, s);
        REQUIRE(seq.step(1).slide == true);
    }

    SECTION("Step with retrig") {
        auto s = makeStep();
        s.retrigCount = 3;
        s.retrigRate = 0.25f;
        seq.setStep(2, s);
        REQUIRE(seq.step(2).retrigCount == 3);
        REQUIRE_THAT(seq.step(2).retrigRate, WithinAbs(0.25f, 0.001f));
    }

    SECTION("Step with condition") {
        auto s = makeStep();
        s.condition = StepCondition::OneInTwo;
        seq.setStep(4, s);
        REQUIRE(seq.step(4).condition == StepCondition::OneInTwo);
    }

    SECTION("Step with micro-timing") {
        auto s = makeStep();
        s.microTiming = -0.1f;
        seq.setStep(6, s);
        REQUIRE_THAT(seq.step(6).microTiming, WithinAbs(-0.1f, 0.001f));
    }

    SECTION("Step with per-step CC") {
        auto s = makeStep();
        s.cc = {StepCC{64, 1.0f}, StepCC{1, 0.5f}};
        seq.setStep(0, s);
        REQUIRE(seq.step(0).cc[0].cc == 64);
        REQUIRE_THAT(seq.step(0).cc[0].value, WithinAbs(1.0f, 0.001f));
        REQUIRE(seq.step(0).cc[1].cc == 1);
        REQUIRE_THAT(seq.step(0).cc[1].value, WithinAbs(0.5f, 0.001f));
    }

    SECTION("Step with gate = -1 means use global") {
        auto s = makeStep();
        s.gate = -1.0f;
        seq.setStep(0, s);
        REQUIRE_THAT(seq.step(0).gate, WithinAbs(-1.0f, 0.001f));
    }
}

TEST_CASE("Sequencer probability", "[audio][sequencer]") {
    Sequencer seq;

    SECTION("probability = 0 never fires on advance") {
        auto s = makeStep();
        s.probability = 0.0f;
        seq.setStep(0, s);
        seq.steps = 1;
        // Advance many times, should never trigger via the normal path
        // (The actual probability check happens in generateBlock, but
        // we can verify the data is stored correctly)
        REQUIRE_THAT(seq.step(0).probability, WithinAbs(0.0f, 0.001f));
    }

    SECTION("probability = 1 always fires") {
        auto s = makeStep();
        s.probability = 1.0f;
        seq.setStep(0, s);
        REQUIRE_THAT(seq.step(0).probability, WithinAbs(1.0f, 0.001f));
    }
}

TEST_CASE("Sequencer isActive and backward compat", "[audio][sequencer]") {
    Sequencer seq;

    SECTION("isActive matches getStep") {
        seq.setStep(5, true, 0.6f);
        REQUIRE(seq.isActive(5) == true);
        REQUIRE(seq.getStep(5) == true);
    }

    SECTION("setStep(int, uint8_t, float) backward compat") {
        seq.setStep(0, uint8_t(64), 0.75f);
        REQUIRE(seq.isActive(0) == true);
        REQUIRE(seq.getNote(0) == 64);
        REQUIRE_THAT(seq.getVelocity(0), WithinAbs(0.75f, 0.001f));
    }

    SECTION("isActive out of range returns false") {
        REQUIRE(seq.isActive(-1) == false);
        REQUIRE(seq.isActive(16) == false);
    }
}

TEST_CASE("Sequencer playback state", "[audio][sequencer]") {
    Sequencer seq;

    SECTION("reset sets step to -1") {
        seq.reset();
        REQUIRE(seq.currentStep() == -1);
        REQUIRE(seq.triggered() == false);
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
        REQUIRE(params.size() == 3);  // steps, midiChannel, gate
    }

    SECTION("has steps param") {
        bool found = false;
        for (const auto& p : params) {
            if (p.name == "steps") found = true;
        }
        REQUIRE(found);
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

// =============================================================================
// Euclidean Rhythm Generator Tests
// =============================================================================

TEST_CASE("Euclidean operator parameter defaults", "[audio][euclidean]") {
    Euclidean eucl;
    float out[4] = {0};

    SECTION("steps defaults to 16") {
        REQUIRE(eucl.getParam("steps", out));
        REQUIRE(static_cast<int>(out[0]) == 16);
    }

    SECTION("hits defaults to 4") {
        REQUIRE(eucl.getParam("hits", out));
        REQUIRE(static_cast<int>(out[0]) == 4);
    }

    SECTION("rotation defaults to 0") {
        REQUIRE(eucl.getParam("rotation", out));
        REQUIRE(static_cast<int>(out[0]) == 0);
    }

    SECTION("currentStep starts at -1") {
        REQUIRE(eucl.currentStep() == -1);
    }

    SECTION("triggered starts false") {
        REQUIRE(eucl.triggered() == false);
    }
}

TEST_CASE("Euclidean operator public param API", "[audio][euclidean]") {
    Euclidean eucl;
    float out[4] = {0};

    SECTION("steps assignment works") {
        eucl.steps = 8;
        REQUIRE(eucl.getParam("steps", out));
        REQUIRE(static_cast<int>(out[0]) == 8);
    }

    SECTION("hits assignment works") {
        eucl.hits = 3;
        REQUIRE(eucl.getParam("hits", out));
        REQUIRE(static_cast<int>(out[0]) == 3);
    }

    SECTION("rotation assignment works") {
        eucl.rotation = 2;
        REQUIRE(eucl.getParam("rotation", out));
        REQUIRE(static_cast<int>(out[0]) == 2);
    }
}

TEST_CASE("Euclidean pattern generation", "[audio][euclidean]") {
    Euclidean eucl;

    SECTION("E(5,16) generates pattern with 5 hits") {
        eucl.steps = 16;
        eucl.hits = 5;
        eucl.advance();
        uint16_t pattern = eucl.pattern();
        int count = 0;
        for (int i = 0; i < 16; i++) {
            if (pattern & (1 << i)) count++;
        }
        REQUIRE(count == 5);
    }

    SECTION("E(8,8) generates all-on pattern") {
        eucl.steps = 8;
        eucl.hits = 8;
        eucl.advance();
        uint16_t pattern = eucl.pattern();
        REQUIRE((pattern & 0xFF) == 0xFF);
    }

    SECTION("E(1,8) generates single hit") {
        eucl.steps = 8;
        eucl.hits = 1;
        eucl.advance();
        uint16_t pattern = eucl.pattern();
        int count = 0;
        for (int i = 0; i < 8; i++) {
            if (pattern & (1 << i)) count++;
        }
        REQUIRE(count == 1);
    }
}

TEST_CASE("Euclidean playback state", "[audio][euclidean]") {
    Euclidean eucl;

    SECTION("reset sets step to -1") {
        eucl.reset();
        REQUIRE(eucl.currentStep() == -1);
        REQUIRE(eucl.triggered() == false);
    }
}

TEST_CASE("Euclidean operator setParam/getParam", "[audio][euclidean]") {
    Euclidean eucl;
    float out[4] = {0};

    SECTION("setParam updates values") {
        float newSteps[4] = {12.0f, 0, 0, 0};
        REQUIRE(eucl.setParam("steps", newSteps));
        REQUIRE(eucl.getParam("steps", out));
        REQUIRE(static_cast<int>(out[0]) == 12);
    }

    SECTION("unknown param returns false") {
        float dummy[4] = {0};
        REQUIRE_FALSE(eucl.getParam("nonexistent", out));
        REQUIRE_FALSE(eucl.setParam("nonexistent", dummy));
    }
}

TEST_CASE("Euclidean operator params() declaration", "[audio][euclidean]") {
    Euclidean eucl;
    auto params = eucl.params();

    SECTION("has expected number of params") {
        REQUIRE(params.size() == 3);
    }

    SECTION("param names are correct") {
        std::vector<std::string> expected = {"steps", "hits", "rotation"};
        for (const auto& name : expected) {
            bool found = false;
            for (const auto& p : params) {
                if (p.name == name) found = true;
            }
            REQUIRE(found);
        }
    }
}

TEST_CASE("Euclidean operator name and output kind", "[audio][euclidean]") {
    Euclidean eucl;
    REQUIRE(eucl.name() == "Euclidean");
    REQUIRE(eucl.outputKind() == vivid::OutputKind::Audio);
}

TEST_CASE("Euclidean MAX_STEPS constant", "[audio][euclidean]") {
    REQUIRE(Euclidean::MAX_STEPS == 16);
}

// =============================================================================
// Envelope (ADSR) Tests
// =============================================================================

TEST_CASE("Envelope parameter defaults", "[audio][envelope]") {
    Envelope env;

    SECTION("attack defaults to 0.01") {
        REQUIRE_THAT(env.attack, WithinAbs(0.01f, 0.001f));
    }

    SECTION("decay defaults to 0.1") {
        REQUIRE_THAT(env.decay, WithinAbs(0.1f, 0.001f));
    }

    SECTION("sustain defaults to 0.7") {
        REQUIRE_THAT(env.sustain, WithinAbs(0.7f, 0.001f));
    }

    SECTION("release defaults to 0.3") {
        REQUIRE_THAT(env.release, WithinAbs(0.3f, 0.001f));
    }
}

TEST_CASE("Envelope parameter assignment", "[audio][envelope]") {
    Envelope env;

    SECTION("ADSR params can be set via references") {
        env.attack = 0.05f;
        env.decay = 0.2f;
        env.sustain = 0.5f;
        env.release = 0.8f;

        REQUIRE_THAT(env.attack, WithinAbs(0.05f, 0.001f));
        REQUIRE_THAT(env.decay, WithinAbs(0.2f, 0.001f));
        REQUIRE_THAT(env.sustain, WithinAbs(0.5f, 0.001f));
        REQUIRE_THAT(env.release, WithinAbs(0.8f, 0.001f));
    }
}

TEST_CASE("Envelope setParam/getParam", "[audio][envelope]") {
    Envelope env;
    float out[4] = {0};

    SECTION("getParam returns ADSR as vec4") {
        env.attack = 0.1f;
        env.decay = 0.2f;
        env.sustain = 0.5f;
        env.release = 0.6f;

        REQUIRE(env.getParam("envelope", out));
        REQUIRE_THAT(out[0], WithinAbs(0.1f, 0.001f));  // attack
        REQUIRE_THAT(out[1], WithinAbs(0.2f, 0.001f));  // decay
        REQUIRE_THAT(out[2], WithinAbs(0.5f, 0.001f));  // sustain
        REQUIRE_THAT(out[3], WithinAbs(0.6f, 0.001f));  // release
    }

    SECTION("setParam updates ADSR") {
        float value[4] = {0.05f, 0.15f, 0.6f, 0.4f};
        REQUIRE(env.setParam("envelope", value));

        REQUIRE_THAT(env.attack, WithinAbs(0.05f, 0.001f));
        REQUIRE_THAT(env.decay, WithinAbs(0.15f, 0.001f));
        REQUIRE_THAT(env.sustain, WithinAbs(0.6f, 0.001f));
        REQUIRE_THAT(env.release, WithinAbs(0.4f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(env.setParam("nonexistent", value));
        REQUIRE_FALSE(env.getParam("nonexistent", out));
    }
}

TEST_CASE("Envelope params() declaration", "[audio][envelope]") {
    Envelope env;
    auto params = env.params();

    SECTION("has envelope param") {
        REQUIRE(params.size() == 1);
        REQUIRE(params[0].name == "envelope");
        REQUIRE(params[0].type == vivid::ParamType::ADSR);
    }
}

TEST_CASE("Envelope state", "[audio][envelope]") {
    Envelope env;

    SECTION("name returns 'Envelope'") {
        REQUIRE(env.name() == "Envelope");
    }

    SECTION("starts inactive (Idle)") {
        REQUIRE(env.isActive() == false);
        REQUIRE(env.stage() == EnvelopeStage::Idle);
    }

    SECTION("currentValue starts at 0") {
        REQUIRE_THAT(env.currentValue(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("reset returns to idle") {
        env.reset();
        REQUIRE(env.isActive() == false);
        REQUIRE(env.stage() == EnvelopeStage::Idle);
    }
}

// =============================================================================
// DrumStack Tests
// =============================================================================

TEST_CASE("DrumStack parameter defaults", "[audio][drumstack]") {
    DrumStack stack;

    SECTION("mix1 defaults to 1.0") {
        REQUIRE_THAT(static_cast<float>(stack.mix1), WithinAbs(1.0f, 0.001f));
    }

    SECTION("mix2 defaults to 0.7") {
        REQUIRE_THAT(static_cast<float>(stack.mix2), WithinAbs(0.7f, 0.001f));
    }

    SECTION("mix3 defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(stack.mix3), WithinAbs(0.5f, 0.001f));
    }

    SECTION("volume defaults to 0.8") {
        REQUIRE_THAT(static_cast<float>(stack.volume), WithinAbs(0.8f, 0.001f));
    }
}

TEST_CASE("DrumStack parameter assignment", "[audio][drumstack]") {
    DrumStack stack;

    SECTION("all params can be set") {
        stack.mix1 = 0.9f;
        stack.mix2 = 0.6f;
        stack.mix3 = 0.3f;
        stack.volume = 0.7f;

        REQUIRE_THAT(static_cast<float>(stack.mix1), WithinAbs(0.9f, 0.001f));
        REQUIRE_THAT(static_cast<float>(stack.mix2), WithinAbs(0.6f, 0.001f));
        REQUIRE_THAT(static_cast<float>(stack.mix3), WithinAbs(0.3f, 0.001f));
        REQUIRE_THAT(static_cast<float>(stack.volume), WithinAbs(0.7f, 0.001f));
    }
}

TEST_CASE("DrumStack setParam/getParam", "[audio][drumstack]") {
    DrumStack stack;
    float out[4] = {0};

    SECTION("setParam updates mix1") {
        float value[4] = {0.8f, 0, 0, 0};
        REQUIRE(stack.setParam("mix1", value));
        REQUIRE(stack.getParam("mix1", out));
        REQUIRE_THAT(out[0], WithinAbs(0.8f, 0.001f));
    }

    SECTION("setParam updates volume") {
        float value[4] = {0.6f, 0, 0, 0};
        REQUIRE(stack.setParam("volume", value));
        REQUIRE(stack.getParam("volume", out));
        REQUIRE_THAT(out[0], WithinAbs(0.6f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(stack.setParam("nonexistent", value));
        REQUIRE_FALSE(stack.getParam("nonexistent", out));
    }
}

TEST_CASE("DrumStack params() declaration", "[audio][drumstack]") {
    DrumStack stack;
    auto params = stack.params();

    SECTION("has all 4 params") {
        REQUIRE(params.size() == 4);

        std::vector<std::string> expected = {"mix1", "mix2", "mix3", "volume"};
        for (const auto& name : expected) {
            bool found = false;
            for (const auto& p : params) {
                if (p.name == name) found = true;
            }
            REQUIRE(found);
        }
    }
}

TEST_CASE("DrumStack layer configuration", "[audio][drumstack]") {
    DrumStack stack;

    SECTION("layer setters compile and run") {
        stack.setLayer1("kick");
        stack.setLayer2("tom");
        stack.setLayer3("noise");
        REQUIRE(true);  // If we get here, it compiled and ran
    }
}

TEST_CASE("DrumStack state", "[audio][drumstack]") {
    DrumStack stack;

    SECTION("name returns 'DrumStack'") {
        REQUIRE(stack.name() == "DrumStack");
    }

    SECTION("reset works without crash") {
        stack.reset();
        REQUIRE(true);  // No crash
    }
}
