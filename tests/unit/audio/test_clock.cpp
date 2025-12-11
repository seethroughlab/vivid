/**
 * @file test_clock.cpp
 * @brief Unit tests for Clock operator
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/audio/clock.h>

using namespace vivid::audio;
using Catch::Matchers::WithinAbs;

TEST_CASE("Clock operator parameter defaults", "[audio][clock]") {
    Clock clock;
    float out[4] = {0};

    SECTION("bpm defaults to 120") {
        REQUIRE(clock.getParam("bpm", out));
        REQUIRE_THAT(out[0], WithinAbs(120.0f, 0.001f));
    }

    SECTION("swing defaults to 0") {
        REQUIRE(clock.getParam("swing", out));
        REQUIRE_THAT(out[0], WithinAbs(0.0f, 0.001f));
    }

    SECTION("getBpm returns correct value") {
        REQUIRE_THAT(clock.getBpm(), WithinAbs(120.0f, 0.001f));
    }

    SECTION("getSwing returns correct value") {
        REQUIRE_THAT(clock.getSwing(), WithinAbs(0.0f, 0.001f));
    }
}

TEST_CASE("Clock operator fluent API", "[audio][clock]") {
    Clock clock;
    float out[4] = {0};

    SECTION("bpm setter works and chains") {
        Clock& ref = clock.bpm(140.0f);
        REQUIRE(&ref == &clock);
        REQUIRE_THAT(clock.getBpm(), WithinAbs(140.0f, 0.001f));
    }

    SECTION("swing setter works and chains") {
        Clock& ref = clock.swing(0.5f);
        REQUIRE(&ref == &clock);
        REQUIRE_THAT(clock.getSwing(), WithinAbs(0.5f, 0.001f));
    }

    SECTION("division setter chains") {
        Clock& ref = clock.division(ClockDiv::Sixteenth);
        REQUIRE(&ref == &clock);
    }

    SECTION("method chaining works") {
        clock.bpm(90.0f).swing(0.25f).division(ClockDiv::Eighth);

        REQUIRE_THAT(clock.getBpm(), WithinAbs(90.0f, 0.001f));
        REQUIRE_THAT(clock.getSwing(), WithinAbs(0.25f, 0.001f));
    }
}

TEST_CASE("Clock operator state", "[audio][clock]") {
    Clock clock;

    SECTION("starts running by default") {
        REQUIRE(clock.isRunning() == true);
    }

    SECTION("stop/start work") {
        clock.stop();
        REQUIRE(clock.isRunning() == false);

        clock.start();
        REQUIRE(clock.isRunning() == true);
    }

    SECTION("triggerCount starts at 0") {
        REQUIRE(clock.triggerCount() == 0);
    }

    SECTION("triggered starts false") {
        REQUIRE(clock.triggered() == false);
    }

    SECTION("beat calculation") {
        // beat() returns triggerCount % 4
        // Since triggerCount is 0, beat should be 0
        REQUIRE(clock.beat() == 0);
    }

    SECTION("bar calculation") {
        // bar() returns triggerCount / 4
        // Since triggerCount is 0, bar should be 0
        REQUIRE(clock.bar() == 0);
    }
}

TEST_CASE("Clock operator setParam/getParam", "[audio][clock]") {
    Clock clock;
    float out[4] = {0};

    SECTION("setParam updates values") {
        float newBpm[4] = {180.0f, 0, 0, 0};
        REQUIRE(clock.setParam("bpm", newBpm));
        REQUIRE(clock.getParam("bpm", out));
        REQUIRE_THAT(out[0], WithinAbs(180.0f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float dummy[4] = {0};
        REQUIRE_FALSE(clock.getParam("nonexistent", out));
        REQUIRE_FALSE(clock.setParam("nonexistent", dummy));
    }
}

TEST_CASE("Clock operator params() declaration", "[audio][clock]") {
    Clock clock;
    auto params = clock.params();

    SECTION("has expected number of params") {
        REQUIRE(params.size() == 2);  // bpm, swing
    }

    SECTION("param names are correct") {
        std::vector<std::string> names;
        for (const auto& p : params) {
            names.push_back(p.name);
        }
        REQUIRE(std::find(names.begin(), names.end(), "bpm") != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "swing") != names.end());
    }
}

TEST_CASE("Clock operator name and output kind", "[audio][clock]") {
    Clock clock;
    REQUIRE(clock.name() == "Clock");
    REQUIRE(clock.outputKind() == vivid::OutputKind::Value);
}
