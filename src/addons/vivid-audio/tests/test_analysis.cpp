/**
 * @file test_analysis.cpp
 * @brief Unit tests for audio analysis operators
 *
 * Tests FFT, Levels, BeatDetect, BandSplit.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/audio/fft.h>
#include <vivid/audio/levels.h>
#include <vivid/audio/beat_detect.h>
#include <vivid/audio/band_split.h>

using namespace vivid::audio;
using Catch::Matchers::WithinAbs;

// =============================================================================
// FFT Tests
// =============================================================================

TEST_CASE("FFT parameter defaults", "[audio][fft]") {
    FFT fft;

    SECTION("smoothing defaults to 0.8") {
        REQUIRE_THAT(static_cast<float>(fft.smoothing), WithinAbs(0.8f, 0.001f));
    }

    SECTION("fftSize defaults to 1024") {
        REQUIRE(fft.fftSize() == 1024);
    }

    SECTION("binCount is fftSize / 2") {
        REQUIRE(fft.binCount() == 512);
    }
}

TEST_CASE("FFT parameter assignment", "[audio][fft]") {
    FFT fft;

    SECTION("smoothing can be set via direct assignment") {
        fft.smoothing = 0.95f;
        REQUIRE_THAT(static_cast<float>(fft.smoothing), WithinAbs(0.95f, 0.001f));
    }
}

TEST_CASE("FFT setParam/getParam", "[audio][fft]") {
    FFT fft;
    float out[4] = {0};

    SECTION("setParam updates smoothing") {
        float value[4] = {0.7f, 0, 0, 0};
        REQUIRE(fft.setParam("smoothing", value));
        REQUIRE(fft.getParam("smoothing", out));
        REQUIRE_THAT(out[0], WithinAbs(0.7f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(fft.setParam("nonexistent", value));
        REQUIRE_FALSE(fft.getParam("nonexistent", out));
    }
}

TEST_CASE("FFT params() declaration", "[audio][fft]") {
    FFT fft;
    auto params = fft.params();

    SECTION("has smoothing param") {
        bool hasSmoothing = false;
        for (const auto& p : params) {
            if (p.name == "smoothing") {
                hasSmoothing = true;
                REQUIRE_THAT(p.minVal, WithinAbs(0.0f, 0.01f));
                REQUIRE_THAT(p.maxVal, WithinAbs(1.0f, 0.01f));
            }
        }
        REQUIRE(hasSmoothing);
    }
}

TEST_CASE("FFT size configuration", "[audio][fft]") {
    FFT fft;

    SECTION("size can be changed to 512") {
        fft.setSize(512);
        REQUIRE(fft.fftSize() == 512);
        REQUIRE(fft.binCount() == 256);
    }

    SECTION("size can be changed to 2048") {
        fft.setSize(2048);
        REQUIRE(fft.fftSize() == 2048);
        REQUIRE(fft.binCount() == 1024);
    }

    SECTION("size can be changed to 4096") {
        fft.setSize(4096);
        REQUIRE(fft.fftSize() == 4096);
        REQUIRE(fft.binCount() == 2048);
    }
}

TEST_CASE("FFT name", "[audio][fft]") {
    FFT fft;
    REQUIRE(fft.name() == "FFT");
}

// =============================================================================
// Levels Tests
// =============================================================================

TEST_CASE("Levels parameter defaults", "[audio][levels]") {
    Levels levels;

    SECTION("smoothing defaults to 0.9") {
        REQUIRE_THAT(static_cast<float>(levels.smoothing), WithinAbs(0.9f, 0.001f));
    }
}

TEST_CASE("Levels parameter assignment", "[audio][levels]") {
    Levels levels;

    SECTION("smoothing can be set via direct assignment") {
        levels.smoothing = 0.8f;
        REQUIRE_THAT(static_cast<float>(levels.smoothing), WithinAbs(0.8f, 0.001f));
    }
}

TEST_CASE("Levels setParam/getParam", "[audio][levels]") {
    Levels levels;
    float out[4] = {0};

    SECTION("setParam updates smoothing") {
        float value[4] = {0.7f, 0, 0, 0};
        REQUIRE(levels.setParam("smoothing", value));
        REQUIRE(levels.getParam("smoothing", out));
        REQUIRE_THAT(out[0], WithinAbs(0.7f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(levels.setParam("nonexistent", value));
        REQUIRE_FALSE(levels.getParam("nonexistent", out));
    }
}

TEST_CASE("Levels params() declaration", "[audio][levels]") {
    Levels levels;
    auto params = levels.params();

    SECTION("has smoothing param") {
        bool hasSmoothing = false;
        for (const auto& p : params) {
            if (p.name == "smoothing") hasSmoothing = true;
        }
        REQUIRE(hasSmoothing);
    }
}

TEST_CASE("Levels name and values", "[audio][levels]") {
    Levels levels;

    SECTION("name returns 'Levels'") {
        REQUIRE(levels.name() == "Levels");
    }

    SECTION("rms() returns 0 initially") {
        REQUIRE_THAT(levels.rms(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("peak() returns 0 initially") {
        REQUIRE_THAT(levels.peak(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("rmsLeft() returns 0 initially") {
        REQUIRE_THAT(levels.rmsLeft(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("rmsRight() returns 0 initially") {
        REQUIRE_THAT(levels.rmsRight(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("rms is between 0 and 1") {
        float rms = levels.rms();
        REQUIRE(rms >= 0.0f);
        REQUIRE(rms <= 1.0f);
    }

    SECTION("peak is between 0 and 1") {
        float peak = levels.peak();
        REQUIRE(peak >= 0.0f);
        REQUIRE(peak <= 1.0f);
    }
}

// =============================================================================
// BeatDetect Tests
// =============================================================================

TEST_CASE("BeatDetect parameter defaults", "[audio][beatdetect]") {
    BeatDetect beatDetect;

    SECTION("sensitivity defaults to 1.5") {
        REQUIRE_THAT(static_cast<float>(beatDetect.sensitivity), WithinAbs(1.5f, 0.001f));
    }

    SECTION("decay defaults to 0.95") {
        REQUIRE_THAT(static_cast<float>(beatDetect.decay), WithinAbs(0.95f, 0.001f));
    }

    SECTION("holdTime defaults to 100 ms") {
        REQUIRE_THAT(static_cast<float>(beatDetect.holdTime), WithinAbs(100.0f, 0.001f));
    }
}

TEST_CASE("BeatDetect parameter assignment", "[audio][beatdetect]") {
    BeatDetect beatDetect;

    SECTION("sensitivity can be set via direct assignment") {
        beatDetect.sensitivity = 2.0f;
        REQUIRE_THAT(static_cast<float>(beatDetect.sensitivity), WithinAbs(2.0f, 0.001f));
    }

    SECTION("decay can be set via direct assignment") {
        beatDetect.decay = 0.9f;
        REQUIRE_THAT(static_cast<float>(beatDetect.decay), WithinAbs(0.9f, 0.001f));
    }

    SECTION("holdTime can be set via direct assignment") {
        beatDetect.holdTime = 200.0f;
        REQUIRE_THAT(static_cast<float>(beatDetect.holdTime), WithinAbs(200.0f, 0.001f));
    }
}

TEST_CASE("BeatDetect setParam/getParam", "[audio][beatdetect]") {
    BeatDetect beatDetect;
    float out[4] = {0};

    SECTION("setParam updates sensitivity") {
        float value[4] = {2.5f, 0, 0, 0};
        REQUIRE(beatDetect.setParam("sensitivity", value));
        REQUIRE(beatDetect.getParam("sensitivity", out));
        REQUIRE_THAT(out[0], WithinAbs(2.5f, 0.001f));
    }

    SECTION("setParam updates decay") {
        float value[4] = {0.85f, 0, 0, 0};
        REQUIRE(beatDetect.setParam("decay", value));
        REQUIRE(beatDetect.getParam("decay", out));
        REQUIRE_THAT(out[0], WithinAbs(0.85f, 0.001f));
    }

    SECTION("setParam updates holdTime") {
        float value[4] = {150.0f, 0, 0, 0};
        REQUIRE(beatDetect.setParam("holdTime", value));
        REQUIRE(beatDetect.getParam("holdTime", out));
        REQUIRE_THAT(out[0], WithinAbs(150.0f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(beatDetect.setParam("nonexistent", value));
        REQUIRE_FALSE(beatDetect.getParam("nonexistent", out));
    }
}

TEST_CASE("BeatDetect params() declaration", "[audio][beatdetect]") {
    BeatDetect beatDetect;
    auto params = beatDetect.params();

    SECTION("has all 3 params") {
        REQUIRE(params.size() >= 3);

        bool hasSensitivity = false, hasDecay = false, hasHoldTime = false;
        for (const auto& p : params) {
            if (p.name == "sensitivity") hasSensitivity = true;
            if (p.name == "decay") hasDecay = true;
            if (p.name == "holdTime") hasHoldTime = true;
        }
        REQUIRE(hasSensitivity);
        REQUIRE(hasDecay);
        REQUIRE(hasHoldTime);
    }
}

TEST_CASE("BeatDetect name and state", "[audio][beatdetect]") {
    BeatDetect beatDetect;

    SECTION("name returns 'BeatDetect'") {
        REQUIRE(beatDetect.name() == "BeatDetect");
    }

    SECTION("beat() returns false initially") {
        REQUIRE(beatDetect.beat() == false);
    }

    SECTION("energy() returns 0 initially") {
        REQUIRE_THAT(beatDetect.energy(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("rawEnergy() returns 0 initially") {
        REQUIRE_THAT(beatDetect.rawEnergy(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("intensity() returns 0 initially") {
        REQUIRE_THAT(beatDetect.intensity(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("timeSinceBeat() is positive") {
        REQUIRE(beatDetect.timeSinceBeat() > 0.0f);
    }
}

// =============================================================================
// BandSplit Tests
// =============================================================================

TEST_CASE("BandSplit parameter defaults", "[audio][bandsplit]") {
    BandSplit bandSplit;

    SECTION("smoothing defaults to 0.9") {
        REQUIRE_THAT(static_cast<float>(bandSplit.smoothing), WithinAbs(0.9f, 0.001f));
    }
}

TEST_CASE("BandSplit parameter assignment", "[audio][bandsplit]") {
    BandSplit bandSplit;

    SECTION("smoothing can be set via direct assignment") {
        bandSplit.smoothing = 0.95f;
        REQUIRE_THAT(static_cast<float>(bandSplit.smoothing), WithinAbs(0.95f, 0.001f));
    }
}

TEST_CASE("BandSplit setParam/getParam", "[audio][bandsplit]") {
    BandSplit bandSplit;
    float out[4] = {0};

    SECTION("setParam updates smoothing") {
        float value[4] = {0.8f, 0, 0, 0};
        REQUIRE(bandSplit.setParam("smoothing", value));
        REQUIRE(bandSplit.getParam("smoothing", out));
        REQUIRE_THAT(out[0], WithinAbs(0.8f, 0.001f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(bandSplit.setParam("nonexistent", value));
        REQUIRE_FALSE(bandSplit.getParam("nonexistent", out));
    }
}

TEST_CASE("BandSplit params() declaration", "[audio][bandsplit]") {
    BandSplit bandSplit;
    auto params = bandSplit.params();

    SECTION("has smoothing param") {
        bool hasSmoothing = false;
        for (const auto& p : params) {
            if (p.name == "smoothing") hasSmoothing = true;
        }
        REQUIRE(hasSmoothing);
    }
}

TEST_CASE("BandSplit name and bands", "[audio][bandsplit]") {
    BandSplit bandSplit;

    SECTION("name returns 'BandSplit'") {
        REQUIRE(bandSplit.name() == "BandSplit");
    }

    SECTION("subBass() returns 0 initially") {
        REQUIRE_THAT(bandSplit.subBass(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("bass() returns 0 initially") {
        REQUIRE_THAT(bandSplit.bass(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("lowMid() returns 0 initially") {
        REQUIRE_THAT(bandSplit.lowMid(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("mid() returns 0 initially") {
        REQUIRE_THAT(bandSplit.mid(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("highMid() returns 0 initially") {
        REQUIRE_THAT(bandSplit.highMid(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("high() returns 0 initially") {
        REQUIRE_THAT(bandSplit.high(), WithinAbs(0.0f, 0.001f));
    }

    SECTION("bands() returns valid pointer") {
        const float* bands = bandSplit.bands();
        REQUIRE(bands != nullptr);
    }
}

TEST_CASE("BandSplit values are bounded", "[audio][bandsplit]") {
    BandSplit bandSplit;

    SECTION("subBass is between 0 and 1") {
        float val = bandSplit.subBass();
        REQUIRE(val >= 0.0f);
        REQUIRE(val <= 1.0f);
    }

    SECTION("bass is between 0 and 1") {
        float val = bandSplit.bass();
        REQUIRE(val >= 0.0f);
        REQUIRE(val <= 1.0f);
    }

    SECTION("mid is between 0 and 1") {
        float val = bandSplit.mid();
        REQUIRE(val >= 0.0f);
        REQUIRE(val <= 1.0f);
    }

    SECTION("high is between 0 and 1") {
        float val = bandSplit.high();
        REQUIRE(val >= 0.0f);
        REQUIRE(val <= 1.0f);
    }
}
