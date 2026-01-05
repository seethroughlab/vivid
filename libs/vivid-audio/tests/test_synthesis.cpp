/**
 * @file test_synthesis.cpp
 * @brief Unit tests for audio synthesis operators
 *
 * Tests Oscillator, Synth, NoiseGen, and drum operators.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/audio/oscillator.h>
#include <vivid/audio/synth.h>
#include <vivid/audio/noise_gen.h>
#include <vivid/audio/kick.h>
#include <vivid/audio/snare.h>
#include <vivid/audio/hihat.h>
#include <vivid/audio/clap.h>

using namespace vivid::audio;
using Catch::Matchers::WithinAbs;

// =============================================================================
// Oscillator Tests
// =============================================================================

TEST_CASE("Oscillator parameter defaults", "[audio][oscillator]") {
    Oscillator osc;

    SECTION("frequency defaults to 440 Hz") {
        REQUIRE_THAT(static_cast<float>(osc.frequency), WithinAbs(440.0f, 0.001f));
    }

    SECTION("volume defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(osc.volume), WithinAbs(0.5f, 0.001f));
    }

    SECTION("detune defaults to 0") {
        REQUIRE_THAT(static_cast<float>(osc.detune), WithinAbs(0.0f, 0.001f));
    }

    SECTION("pulseWidth defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(osc.pulseWidth), WithinAbs(0.5f, 0.001f));
    }

    SECTION("stereoDetune defaults to 0") {
        REQUIRE_THAT(static_cast<float>(osc.stereoDetune), WithinAbs(0.0f, 0.001f));
    }
}

TEST_CASE("Oscillator parameter assignment", "[audio][oscillator]") {
    Oscillator osc;

    SECTION("frequency assignment") {
        osc.frequency = 880.0f;
        REQUIRE_THAT(static_cast<float>(osc.frequency), WithinAbs(880.0f, 0.001f));
    }

    SECTION("volume assignment") {
        osc.volume = 0.8f;
        REQUIRE_THAT(static_cast<float>(osc.volume), WithinAbs(0.8f, 0.001f));
    }

    SECTION("detune assignment") {
        osc.detune = 12.0f;  // 12 cents
        REQUIRE_THAT(static_cast<float>(osc.detune), WithinAbs(12.0f, 0.001f));
    }

    SECTION("pulseWidth assignment") {
        osc.pulseWidth = 0.25f;
        REQUIRE_THAT(static_cast<float>(osc.pulseWidth), WithinAbs(0.25f, 0.001f));
    }
}

TEST_CASE("Oscillator setParam/getParam", "[audio][oscillator]") {
    Oscillator osc;
    float out[4] = {0};

    SECTION("setParam updates frequency") {
        float value[4] = {220.0f, 0, 0, 0};
        REQUIRE(osc.setParam("frequency", value));
        REQUIRE(osc.getParam("frequency", out));
        REQUIRE_THAT(out[0], WithinAbs(220.0f, 0.001f));
    }

    SECTION("setParam updates volume") {
        float value[4] = {0.3f, 0, 0, 0};
        REQUIRE(osc.setParam("volume", value));
        REQUIRE(osc.getParam("volume", out));
        REQUIRE_THAT(out[0], WithinAbs(0.3f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(osc.setParam("nonexistent", value));
        REQUIRE_FALSE(osc.getParam("nonexistent", out));
    }
}

TEST_CASE("Oscillator params() declaration", "[audio][oscillator]") {
    Oscillator osc;
    auto params = osc.params();

    SECTION("has expected params") {
        REQUIRE(params.size() >= 4);

        bool hasFrequency = false, hasVolume = false, hasDetune = false;
        for (const auto& p : params) {
            if (p.name == "frequency") {
                hasFrequency = true;
                REQUIRE(p.type == vivid::ParamType::Float);
                REQUIRE(p.minVal == 20.0f);
                REQUIRE(p.maxVal == 20000.0f);
            }
            if (p.name == "volume") hasVolume = true;
            if (p.name == "detune") hasDetune = true;
        }
        REQUIRE(hasFrequency);
        REQUIRE(hasVolume);
        REQUIRE(hasDetune);
    }
}

TEST_CASE("Oscillator name and waveform", "[audio][oscillator]") {
    Oscillator osc;

    SECTION("name returns 'Oscillator'") {
        REQUIRE(osc.name() == "Oscillator");
    }

    SECTION("waveform can be set") {
        osc.waveform(Waveform::Sine);
        osc.waveform(Waveform::Triangle);
        osc.waveform(Waveform::Square);
        osc.waveform(Waveform::Saw);
        osc.waveform(Waveform::Pulse);
        REQUIRE(true);  // Compiles and runs
    }

    SECTION("reset clears phase") {
        osc.reset();
        REQUIRE(true);  // No crash
    }
}

// =============================================================================
// Synth Tests
// =============================================================================

TEST_CASE("Synth parameter defaults", "[audio][synth]") {
    Synth synth;

    SECTION("frequency defaults to 440 Hz") {
        REQUIRE_THAT(static_cast<float>(synth.frequency), WithinAbs(440.0f, 0.001f));
    }

    SECTION("volume defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(synth.volume), WithinAbs(0.5f, 0.001f));
    }

    SECTION("ADSR defaults") {
        REQUIRE_THAT(static_cast<float>(synth.attack), WithinAbs(0.01f, 0.001f));
        REQUIRE_THAT(static_cast<float>(synth.decay), WithinAbs(0.1f, 0.001f));
        REQUIRE_THAT(static_cast<float>(synth.sustain), WithinAbs(0.7f, 0.001f));
        REQUIRE_THAT(static_cast<float>(synth.release), WithinAbs(0.3f, 0.001f));
    }
}

TEST_CASE("Synth parameter assignment", "[audio][synth]") {
    Synth synth;

    SECTION("envelope parameters") {
        synth.attack = 0.05f;
        synth.decay = 0.2f;
        synth.sustain = 0.5f;
        synth.release = 0.5f;

        REQUIRE_THAT(static_cast<float>(synth.attack), WithinAbs(0.05f, 0.001f));
        REQUIRE_THAT(static_cast<float>(synth.decay), WithinAbs(0.2f, 0.001f));
        REQUIRE_THAT(static_cast<float>(synth.sustain), WithinAbs(0.5f, 0.001f));
        REQUIRE_THAT(static_cast<float>(synth.release), WithinAbs(0.5f, 0.001f));
    }
}

TEST_CASE("Synth setParam/getParam", "[audio][synth]") {
    Synth synth;
    float out[4] = {0};

    SECTION("setParam updates attack") {
        float value[4] = {0.1f, 0, 0, 0};
        REQUIRE(synth.setParam("attack", value));
        REQUIRE(synth.getParam("attack", out));
        REQUIRE_THAT(out[0], WithinAbs(0.1f, 0.001f));
    }

    SECTION("setParam updates sustain") {
        float value[4] = {0.8f, 0, 0, 0};
        REQUIRE(synth.setParam("sustain", value));
        REQUIRE(synth.getParam("sustain", out));
        REQUIRE_THAT(out[0], WithinAbs(0.8f, 0.001f));
    }
}

TEST_CASE("Synth params() declaration", "[audio][synth]") {
    Synth synth;
    auto params = synth.params();

    SECTION("has all 8 params") {
        REQUIRE(params.size() == 8);

        std::vector<std::string> expected = {
            "frequency", "volume", "detune", "pulseWidth",
            "attack", "decay", "sustain", "release"
        };

        for (const auto& name : expected) {
            bool found = false;
            for (const auto& p : params) {
                if (p.name == name) found = true;
            }
            REQUIRE(found);
        }
    }
}

TEST_CASE("Synth playback control", "[audio][synth]") {
    Synth synth;

    SECTION("name returns 'Synth'") {
        REQUIRE(synth.name() == "Synth");
    }

    SECTION("starts not playing") {
        REQUIRE(synth.isPlaying() == false);
    }

    SECTION("waveform can be set") {
        synth.setWaveform(Waveform::Saw);
        REQUIRE(true);
    }

    SECTION("reset works") {
        synth.reset();
        REQUIRE(synth.isPlaying() == false);
    }
}

// =============================================================================
// NoiseGen Tests
// =============================================================================

TEST_CASE("NoiseGen parameter defaults", "[audio][noisegen]") {
    NoiseGen noise;

    SECTION("volume defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(noise.volume), WithinAbs(0.5f, 0.001f));
    }
}

TEST_CASE("NoiseGen parameter assignment", "[audio][noisegen]") {
    NoiseGen noise;

    SECTION("volume assignment") {
        noise.volume = 0.3f;
        REQUIRE_THAT(static_cast<float>(noise.volume), WithinAbs(0.3f, 0.001f));
    }
}

TEST_CASE("NoiseGen setParam/getParam", "[audio][noisegen]") {
    NoiseGen noise;
    float out[4] = {0};

    SECTION("setParam updates volume") {
        float value[4] = {0.7f, 0, 0, 0};
        REQUIRE(noise.setParam("volume", value));
        REQUIRE(noise.getParam("volume", out));
        REQUIRE_THAT(out[0], WithinAbs(0.7f, 0.001f));
    }
}

TEST_CASE("NoiseGen params() declaration", "[audio][noisegen]") {
    NoiseGen noise;
    auto params = noise.params();

    SECTION("has volume param") {
        REQUIRE(params.size() == 1);
        REQUIRE(params[0].name == "volume");
        REQUIRE(params[0].type == vivid::ParamType::Float);
    }
}

TEST_CASE("NoiseGen color and name", "[audio][noisegen]") {
    NoiseGen noise;

    SECTION("name returns 'NoiseGen'") {
        REQUIRE(noise.name() == "NoiseGen");
    }

    SECTION("color can be set") {
        noise.setColor(NoiseColor::White);
        noise.setColor(NoiseColor::Pink);
        noise.setColor(NoiseColor::Brown);
        REQUIRE(true);  // Compiles and runs
    }
}

// =============================================================================
// Kick Drum Tests
// =============================================================================

TEST_CASE("Kick parameter defaults", "[audio][kick]") {
    Kick kick;

    SECTION("pitch defaults to 50 Hz") {
        REQUIRE_THAT(static_cast<float>(kick.pitch), WithinAbs(50.0f, 0.001f));
    }

    SECTION("pitchEnv defaults to 100") {
        REQUIRE_THAT(static_cast<float>(kick.pitchEnv), WithinAbs(100.0f, 0.001f));
    }

    SECTION("decay defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(kick.decay), WithinAbs(0.5f, 0.001f));
    }

    SECTION("click defaults to 0.3") {
        REQUIRE_THAT(static_cast<float>(kick.click), WithinAbs(0.3f, 0.001f));
    }

    SECTION("volume defaults to 0.8") {
        REQUIRE_THAT(static_cast<float>(kick.volume), WithinAbs(0.8f, 0.001f));
    }
}

TEST_CASE("Kick parameter assignment", "[audio][kick]") {
    Kick kick;

    SECTION("all params can be set") {
        kick.pitch = 60.0f;
        kick.pitchEnv = 150.0f;
        kick.pitchDecay = 0.15f;
        kick.decay = 0.6f;
        kick.click = 0.5f;
        kick.drive = 0.2f;

        REQUIRE_THAT(static_cast<float>(kick.pitch), WithinAbs(60.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(kick.pitchEnv), WithinAbs(150.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(kick.pitchDecay), WithinAbs(0.15f, 0.001f));
        REQUIRE_THAT(static_cast<float>(kick.decay), WithinAbs(0.6f, 0.001f));
        REQUIRE_THAT(static_cast<float>(kick.click), WithinAbs(0.5f, 0.001f));
        REQUIRE_THAT(static_cast<float>(kick.drive), WithinAbs(0.2f, 0.001f));
    }
}

TEST_CASE("Kick setParam/getParam", "[audio][kick]") {
    Kick kick;
    float out[4] = {0};

    SECTION("setParam updates pitch") {
        float value[4] = {70.0f, 0, 0, 0};
        REQUIRE(kick.setParam("pitch", value));
        REQUIRE(kick.getParam("pitch", out));
        REQUIRE_THAT(out[0], WithinAbs(70.0f, 0.001f));
    }

    SECTION("setParam updates decay") {
        float value[4] = {0.8f, 0, 0, 0};
        REQUIRE(kick.setParam("decay", value));
        REQUIRE(kick.getParam("decay", out));
        REQUIRE_THAT(out[0], WithinAbs(0.8f, 0.001f));
    }
}

TEST_CASE("Kick params() declaration", "[audio][kick]") {
    Kick kick;
    auto params = kick.params();

    SECTION("has all 7 params") {
        REQUIRE(params.size() == 7);

        std::vector<std::string> expected = {
            "pitch", "pitchEnv", "pitchDecay", "decay", "click", "drive", "volume"
        };

        for (const auto& name : expected) {
            bool found = false;
            for (const auto& p : params) {
                if (p.name == name) found = true;
            }
            REQUIRE(found);
        }
    }
}

TEST_CASE("Kick trigger and state", "[audio][kick]") {
    Kick kick;

    SECTION("name returns 'Kick'") {
        REQUIRE(kick.name() == "Kick");
    }

    SECTION("starts inactive") {
        REQUIRE(kick.isActive() == false);
    }

    SECTION("reset clears state") {
        kick.reset();
        REQUIRE(kick.isActive() == false);
    }
}

// =============================================================================
// Snare Drum Tests
// =============================================================================

TEST_CASE("Snare parameter defaults", "[audio][snare]") {
    Snare snare;

    SECTION("tone defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(snare.tone), WithinAbs(0.5f, 0.001f));
    }

    SECTION("noise defaults to 0.7") {
        REQUIRE_THAT(static_cast<float>(snare.noise), WithinAbs(0.7f, 0.001f));
    }

    SECTION("pitch defaults to 200 Hz") {
        REQUIRE_THAT(static_cast<float>(snare.pitch), WithinAbs(200.0f, 0.001f));
    }

    SECTION("snappy defaults to 0.5") {
        REQUIRE_THAT(static_cast<float>(snare.snappy), WithinAbs(0.5f, 0.001f));
    }
}

TEST_CASE("Snare parameter assignment", "[audio][snare]") {
    Snare snare;

    SECTION("all params can be set") {
        snare.tone = 0.4f;
        snare.noise = 0.8f;
        snare.pitch = 180.0f;
        snare.toneDecay = 0.15f;
        snare.noiseDecay = 0.25f;
        snare.snappy = 0.6f;

        REQUIRE_THAT(static_cast<float>(snare.tone), WithinAbs(0.4f, 0.001f));
        REQUIRE_THAT(static_cast<float>(snare.noise), WithinAbs(0.8f, 0.001f));
        REQUIRE_THAT(static_cast<float>(snare.pitch), WithinAbs(180.0f, 0.001f));
        REQUIRE_THAT(static_cast<float>(snare.toneDecay), WithinAbs(0.15f, 0.001f));
        REQUIRE_THAT(static_cast<float>(snare.noiseDecay), WithinAbs(0.25f, 0.001f));
        REQUIRE_THAT(static_cast<float>(snare.snappy), WithinAbs(0.6f, 0.001f));
    }
}

TEST_CASE("Snare setParam/getParam", "[audio][snare]") {
    Snare snare;
    float out[4] = {0};

    SECTION("setParam updates tone") {
        float value[4] = {0.6f, 0, 0, 0};
        REQUIRE(snare.setParam("tone", value));
        REQUIRE(snare.getParam("tone", out));
        REQUIRE_THAT(out[0], WithinAbs(0.6f, 0.001f));
    }

    SECTION("setParam updates noise") {
        float value[4] = {0.9f, 0, 0, 0};
        REQUIRE(snare.setParam("noise", value));
        REQUIRE(snare.getParam("noise", out));
        REQUIRE_THAT(out[0], WithinAbs(0.9f, 0.001f));
    }
}

TEST_CASE("Snare params() declaration", "[audio][snare]") {
    Snare snare;
    auto params = snare.params();

    SECTION("has all 7 params") {
        REQUIRE(params.size() == 7);
    }
}

TEST_CASE("Snare trigger and state", "[audio][snare]") {
    Snare snare;

    SECTION("name returns 'Snare'") {
        REQUIRE(snare.name() == "Snare");
    }

    SECTION("starts inactive") {
        REQUIRE(snare.isActive() == false);
    }

    SECTION("reset clears state") {
        snare.reset();
        REQUIRE(snare.isActive() == false);
    }
}

// =============================================================================
// HiHat Tests
// =============================================================================

TEST_CASE("HiHat basic tests", "[audio][hihat]") {
    HiHat hihat;

    SECTION("name returns 'HiHat'") {
        REQUIRE(hihat.name() == "HiHat");
    }

    SECTION("has decay param") {
        float out[4] = {0};
        REQUIRE(hihat.getParam("decay", out));
    }

    SECTION("has tone param") {
        float out[4] = {0};
        REQUIRE(hihat.getParam("tone", out));
    }

    SECTION("starts inactive") {
        REQUIRE(hihat.isActive() == false);
    }
}

// =============================================================================
// Clap Tests
// =============================================================================

TEST_CASE("Clap basic tests", "[audio][clap]") {
    Clap clap;

    SECTION("name returns 'Clap'") {
        REQUIRE(clap.name() == "Clap");
    }

    SECTION("has decay param") {
        float out[4] = {0};
        REQUIRE(clap.getParam("decay", out));
    }

    SECTION("has spread param") {
        float out[4] = {0};
        REQUIRE(clap.getParam("spread", out));
    }

    SECTION("starts inactive") {
        REQUIRE(clap.isActive() == false);
    }
}
