/**
 * @file test_chain.cpp
 * @brief Integration tests for Chain composition
 *
 * Tests that verify chain building and operator connections work correctly.
 * Note: These tests don't require GPU context - they test the API and data flow.
 */

#include <catch2/catch_test_macros.hpp>
#include <vivid/chain.h>
#include <vivid/effects/noise.h>
#include <vivid/effects/blur.h>
#include <vivid/effects/composite.h>

using namespace vivid;
using namespace vivid::effects;

TEST_CASE("Chain basic operations", "[integration][chain]") {
    Chain chain;

    SECTION("add operator returns reference") {
        Noise& noise = chain.add<Noise>("noise1");
        REQUIRE(noise.name() == "Noise");
    }

    SECTION("getByName finds operator") {
        chain.add<Noise>("myNoise");
        Operator* op = chain.getByName("myNoise");
        REQUIRE(op != nullptr);
        REQUIRE(op->name() == "Noise");
    }

    SECTION("getByName returns nullptr for unknown") {
        Operator* op = chain.getByName("nonexistent");
        REQUIRE(op == nullptr);
    }

    SECTION("get<T> returns typed reference") {
        chain.add<Blur>("myBlur");
        Blur& blur = chain.get<Blur>("myBlur");
        REQUIRE(blur.name() == "Blur");
    }

    SECTION("getName returns operator name") {
        Noise& noise = chain.add<Noise>("testNoise");
        std::string name = chain.getName(&noise);
        REQUIRE(name == "testNoise");
    }
}

TEST_CASE("Chain operator configuration", "[integration][chain]") {
    Chain chain;

    SECTION("fluent API works through chain.add") {
        Noise& noise = chain.add<Noise>("noise").scale(10.0f).speed(2.0f);

        float out[4] = {0};
        REQUIRE(noise.getParam("scale", out));
        REQUIRE(out[0] == 10.0f);

        REQUIRE(noise.getParam("speed", out));
        REQUIRE(out[0] == 2.0f);
    }

    SECTION("multiple operators can be added") {
        chain.add<Noise>("noise1");
        chain.add<Noise>("noise2");
        chain.add<Blur>("blur1");

        REQUIRE(chain.getByName("noise1") != nullptr);
        REQUIRE(chain.getByName("noise2") != nullptr);
        REQUIRE(chain.getByName("blur1") != nullptr);
    }
}

TEST_CASE("Chain output configuration", "[integration][chain]") {
    Chain chain;

    SECTION("output can be set by name") {
        chain.add<Noise>("noise");
        chain.output("noise");

        Operator* output = chain.getOutput();
        REQUIRE(output != nullptr);
        REQUIRE(output->name() == "Noise");
    }

    SECTION("output returns nullptr when not set") {
        chain.add<Noise>("noise");
        // Don't set output
        Operator* output = chain.getOutput();
        REQUIRE(output == nullptr);
    }
}
