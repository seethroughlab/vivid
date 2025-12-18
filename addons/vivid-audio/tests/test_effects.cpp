/**
 * @file test_effects.cpp
 * @brief Unit tests for audio effect operators
 *
 * Tests Delay, Echo, Reverb, Chorus, Flanger, Compressor, Limiter, Gate.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/audio/delay.h>
#include <vivid/audio/echo.h>
#include <vivid/audio/reverb.h>
#include <vivid/audio/chorus.h>
#include <vivid/audio/flanger.h>
#include <vivid/audio/compressor.h>
#include <vivid/audio/limiter.h>
#include <vivid/audio/gate.h>

using namespace vivid::audio;
using Catch::Matchers::WithinAbs;

// =============================================================================
// Delay Tests
// =============================================================================

TEST_CASE("Delay parameter defaults", "[audio][delay]") {
    Delay delay;

    SECTION("delayTime defaults to 250 ms") {
        REQUIRE_THAT(static_cast<float>(delay.delayTime), WithinAbs(250.0f, 0.001f));
    }

    SECTION("feedback defaults to 0.3") {
        REQUIRE_THAT(static_cast<float>(delay.feedback), WithinAbs(0.3f, 0.001f));
    }

    SECTION("mix defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(delay.mix), WithinAbs(0.5f, 0.001f));
    }
}

TEST_CASE("Delay parameter assignment", "[audio][delay]") {
    Delay delay;

    SECTION("all params can be set") {
        delay.delayTime = 500.0f;
        delay.feedback = 0.6f;
        delay.mix = 0.4f;

        REQUIRE_THAT(static_cast<float>(delay.delayTime), WithinAbs(500.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(delay.feedback), WithinAbs(0.6f, 0.001f));
        REQUIRE_THAT(static_cast<float>(delay.mix), WithinAbs(0.4f, 0.001f));
    }
}

TEST_CASE("Delay setParam/getParam", "[audio][delay]") {
    Delay delay;
    float out[4] = {0};

    SECTION("setParam updates delayTime") {
        float value[4] = {300.0f, 0, 0, 0};
        REQUIRE(delay.setParam("delayTime", value));
        REQUIRE(delay.getParam("delayTime", out));
        REQUIRE_THAT(out[0], WithinAbs(300.0f, 0.001f));
    }

    SECTION("setParam updates feedback") {
        float value[4] = {0.5f, 0, 0, 0};
        REQUIRE(delay.setParam("feedback", value));
        REQUIRE(delay.getParam("feedback", out));
        REQUIRE_THAT(out[0], WithinAbs(0.5f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(delay.setParam("nonexistent", value));
        REQUIRE_FALSE(delay.getParam("nonexistent", out));
    }
}

TEST_CASE("Delay params() declaration", "[audio][delay]") {
    Delay delay;
    auto params = delay.params();

    SECTION("has all 3 params") {
        REQUIRE(params.size() == 3);

        bool hasDelayTime = false, hasFeedback = false, hasMix = false;
        for (const auto& p : params) {
            if (p.name == "delayTime") {
                hasDelayTime = true;
                REQUIRE(p.minVal == 0.0f);
                REQUIRE(p.maxVal == 2000.0f);
            }
            if (p.name == "feedback") hasFeedback = true;
            if (p.name == "mix") hasMix = true;
        }
        REQUIRE(hasDelayTime);
        REQUIRE(hasFeedback);
        REQUIRE(hasMix);
    }
}

TEST_CASE("Delay name", "[audio][delay]") {
    Delay delay;
    REQUIRE(delay.name() == "Delay");
}

// =============================================================================
// Echo Tests
// =============================================================================

TEST_CASE("Echo parameter defaults", "[audio][echo]") {
    Echo echo;

    SECTION("delayTime defaults to 300 ms") {
        REQUIRE_THAT(static_cast<float>(echo.delayTime), WithinAbs(300.0f, 0.001f));
    }

    SECTION("decay defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(echo.decay), WithinAbs(0.5f, 0.001f));
    }

    SECTION("taps defaults to 4") {
        REQUIRE(static_cast<int>(echo.taps) == 4);
    }

    SECTION("mix defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(echo.mix), WithinAbs(0.5f, 0.001f));
    }
}

TEST_CASE("Echo setParam/getParam", "[audio][echo]") {
    Echo echo;
    float out[4] = {0};

    SECTION("setParam updates delayTime") {
        float value[4] = {400.0f, 0, 0, 0};
        REQUIRE(echo.setParam("delayTime", value));
        REQUIRE(echo.getParam("delayTime", out));
        REQUIRE_THAT(out[0], WithinAbs(400.0f, 0.001f));
    }

    SECTION("setParam updates decay") {
        float value[4] = {0.7f, 0, 0, 0};
        REQUIRE(echo.setParam("decay", value));
        REQUIRE(echo.getParam("decay", out));
        REQUIRE_THAT(out[0], WithinAbs(0.7f, 0.001f));
    }
}

TEST_CASE("Echo params() declaration", "[audio][echo]") {
    Echo echo;
    auto params = echo.params();

    SECTION("has all 4 params") {
        REQUIRE(params.size() == 4);
    }
}

TEST_CASE("Echo name", "[audio][echo]") {
    Echo echo;
    REQUIRE(echo.name() == "Echo");
}

// =============================================================================
// Reverb Tests
// =============================================================================

TEST_CASE("Reverb parameter defaults", "[audio][reverb]") {
    Reverb reverb;

    SECTION("roomSize defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(reverb.roomSize), WithinAbs(0.5f, 0.001f));
    }

    SECTION("damping defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(reverb.damping), WithinAbs(0.5f, 0.001f));
    }

    SECTION("width defaults to 1.0") {
        REQUIRE_THAT(static_cast<float>(reverb.width), WithinAbs(1.0f, 0.001f));
    }

    SECTION("mix defaults to 0.3") {
        REQUIRE_THAT(static_cast<float>(reverb.mix), WithinAbs(0.3f, 0.001f));
    }
}

TEST_CASE("Reverb parameter assignment", "[audio][reverb]") {
    Reverb reverb;

    SECTION("all params can be set") {
        reverb.roomSize = 0.8f;
        reverb.damping = 0.7f;
        reverb.width = 0.5f;
        reverb.mix = 0.4f;

        REQUIRE_THAT(static_cast<float>(reverb.roomSize), WithinAbs(0.8f, 0.001f));
        REQUIRE_THAT(static_cast<float>(reverb.damping), WithinAbs(0.7f, 0.001f));
        REQUIRE_THAT(static_cast<float>(reverb.width), WithinAbs(0.5f, 0.001f));
        REQUIRE_THAT(static_cast<float>(reverb.mix), WithinAbs(0.4f, 0.001f));
    }
}

TEST_CASE("Reverb setParam/getParam", "[audio][reverb]") {
    Reverb reverb;
    float out[4] = {0};

    SECTION("setParam updates roomSize") {
        float value[4] = {0.9f, 0, 0, 0};
        REQUIRE(reverb.setParam("roomSize", value));
        REQUIRE(reverb.getParam("roomSize", out));
        REQUIRE_THAT(out[0], WithinAbs(0.9f, 0.001f));
    }

    SECTION("setParam updates damping") {
        float value[4] = {0.8f, 0, 0, 0};
        REQUIRE(reverb.setParam("damping", value));
        REQUIRE(reverb.getParam("damping", out));
        REQUIRE_THAT(out[0], WithinAbs(0.8f, 0.001f));
    }
}

TEST_CASE("Reverb params() declaration", "[audio][reverb]") {
    Reverb reverb;
    auto params = reverb.params();

    SECTION("has all 4 params") {
        REQUIRE(params.size() == 4);

        std::vector<std::string> expected = {"roomSize", "damping", "width", "mix"};
        for (const auto& name : expected) {
            bool found = false;
            for (const auto& p : params) {
                if (p.name == name) found = true;
            }
            REQUIRE(found);
        }
    }
}

TEST_CASE("Reverb name", "[audio][reverb]") {
    Reverb reverb;
    REQUIRE(reverb.name() == "Reverb");
}

// =============================================================================
// Chorus Tests
// =============================================================================

TEST_CASE("Chorus basic tests", "[audio][chorus]") {
    Chorus chorus;

    SECTION("name returns 'Chorus'") {
        REQUIRE(chorus.name() == "Chorus");
    }

    SECTION("has rate param") {
        float out[4] = {0};
        REQUIRE(chorus.getParam("rate", out));
    }

    SECTION("has depth param") {
        float out[4] = {0};
        REQUIRE(chorus.getParam("depth", out));
    }

    SECTION("has mix param") {
        float out[4] = {0};
        REQUIRE(chorus.getParam("mix", out));
    }

    SECTION("params() returns declarations") {
        auto params = chorus.params();
        REQUIRE(params.size() >= 3);
    }
}

// =============================================================================
// Flanger Tests
// =============================================================================

TEST_CASE("Flanger basic tests", "[audio][flanger]") {
    Flanger flanger;

    SECTION("name returns 'Flanger'") {
        REQUIRE(flanger.name() == "Flanger");
    }

    SECTION("has rate param") {
        float out[4] = {0};
        REQUIRE(flanger.getParam("rate", out));
    }

    SECTION("has depth param") {
        float out[4] = {0};
        REQUIRE(flanger.getParam("depth", out));
    }

    SECTION("has feedback param") {
        float out[4] = {0};
        REQUIRE(flanger.getParam("feedback", out));
    }

    SECTION("params() returns declarations") {
        auto params = flanger.params();
        REQUIRE(params.size() >= 3);
    }
}

// =============================================================================
// Compressor Tests
// =============================================================================

TEST_CASE("Compressor parameter defaults", "[audio][compressor]") {
    Compressor comp;

    SECTION("threshold defaults to -12 dB") {
        REQUIRE_THAT(static_cast<float>(comp.threshold), WithinAbs(-12.0f, 0.001f));
    }

    SECTION("ratio defaults to 4") {
        REQUIRE_THAT(static_cast<float>(comp.ratio), WithinAbs(4.0f, 0.001f));
    }

    SECTION("attack defaults to 10 ms") {
        REQUIRE_THAT(static_cast<float>(comp.attack), WithinAbs(10.0f, 0.001f));
    }

    SECTION("release defaults to 100 ms") {
        REQUIRE_THAT(static_cast<float>(comp.release), WithinAbs(100.0f, 0.001f));
    }

    SECTION("makeupGain defaults to 0 dB") {
        REQUIRE_THAT(static_cast<float>(comp.makeupGain), WithinAbs(0.0f, 0.001f));
    }

    SECTION("knee defaults to 0") {
        REQUIRE_THAT(static_cast<float>(comp.knee), WithinAbs(0.0f, 0.001f));
    }

    SECTION("mix defaults to 1 (full wet)") {
        REQUIRE_THAT(static_cast<float>(comp.mix), WithinAbs(1.0f, 0.001f));
    }
}

TEST_CASE("Compressor parameter assignment", "[audio][compressor]") {
    Compressor comp;

    SECTION("all params can be set") {
        comp.threshold = -18.0f;
        comp.ratio = 8.0f;
        comp.attack = 5.0f;
        comp.release = 200.0f;
        comp.makeupGain = 6.0f;
        comp.knee = 6.0f;
        comp.mix = 0.5f;

        REQUIRE_THAT(static_cast<float>(comp.threshold), WithinAbs(-18.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(comp.ratio), WithinAbs(8.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(comp.attack), WithinAbs(5.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(comp.release), WithinAbs(200.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(comp.makeupGain), WithinAbs(6.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(comp.knee), WithinAbs(6.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(comp.mix), WithinAbs(0.5f, 0.001f));
    }
}

TEST_CASE("Compressor setParam/getParam", "[audio][compressor]") {
    Compressor comp;
    float out[4] = {0};

    SECTION("setParam updates threshold") {
        float value[4] = {-24.0f, 0, 0, 0};
        REQUIRE(comp.setParam("threshold", value));
        REQUIRE(comp.getParam("threshold", out));
        REQUIRE_THAT(out[0], WithinAbs(-24.0f, 0.001f));
    }

    SECTION("setParam updates ratio") {
        float value[4] = {10.0f, 0, 0, 0};
        REQUIRE(comp.setParam("ratio", value));
        REQUIRE(comp.getParam("ratio", out));
        REQUIRE_THAT(out[0], WithinAbs(10.0f, 0.001f));
    }
}

TEST_CASE("Compressor params() declaration", "[audio][compressor]") {
    Compressor comp;
    auto params = comp.params();

    SECTION("has all 7 params") {
        REQUIRE(params.size() == 7);

        std::vector<std::string> expected = {
            "threshold", "ratio", "attack", "release", "makeupGain", "knee", "mix"
        };

        for (const auto& name : expected) {
            bool found = false;
            for (const auto& p : params) {
                if (p.name == name) found = true;
            }
            REQUIRE(found);
        }
    }

    SECTION("threshold has correct range") {
        for (const auto& p : params) {
            if (p.name == "threshold") {
                REQUIRE(p.minVal == -60.0f);
                REQUIRE(p.maxVal == 0.0f);
            }
        }
    }

    SECTION("ratio has correct range") {
        for (const auto& p : params) {
            if (p.name == "ratio") {
                REQUIRE(p.minVal == 1.0f);
                REQUIRE(p.maxVal == 20.0f);
            }
        }
    }
}

TEST_CASE("Compressor name and state", "[audio][compressor]") {
    Compressor comp;

    SECTION("name returns 'Compressor'") {
        REQUIRE(comp.name() == "Compressor");
    }

    SECTION("getGainReduction returns 0 initially") {
        REQUIRE_THAT(comp.getGainReduction(), WithinAbs(0.0f, 0.001f));
    }
}

// =============================================================================
// Limiter Tests
// =============================================================================

TEST_CASE("Limiter basic tests", "[audio][limiter]") {
    Limiter limiter;

    SECTION("name returns 'Limiter'") {
        REQUIRE(limiter.name() == "Limiter");
    }

    SECTION("has ceiling param") {
        float out[4] = {0};
        REQUIRE(limiter.getParam("ceiling", out));
    }

    SECTION("has release param") {
        float out[4] = {0};
        REQUIRE(limiter.getParam("release", out));
    }

    SECTION("params() returns declarations") {
        auto params = limiter.params();
        REQUIRE(params.size() >= 2);
    }
}

// =============================================================================
// Gate Tests
// =============================================================================

TEST_CASE("Gate basic tests", "[audio][gate]") {
    Gate gate;

    SECTION("name returns 'Gate'") {
        REQUIRE(gate.name() == "Gate");
    }

    SECTION("has threshold param") {
        float out[4] = {0};
        REQUIRE(gate.getParam("threshold", out));
    }

    SECTION("has attack param") {
        float out[4] = {0};
        REQUIRE(gate.getParam("attack", out));
    }

    SECTION("has release param") {
        float out[4] = {0};
        REQUIRE(gate.getParam("release", out));
    }

    SECTION("params() returns declarations") {
        auto params = gate.params();
        REQUIRE(params.size() >= 3);
    }
}
