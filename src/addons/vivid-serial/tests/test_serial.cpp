/**
 * @file test_serial.cpp
 * @brief Unit tests for serial operators
 *
 * Tests SerialOut, SerialIn, DMXOut configuration and state.
 * Note: These tests don't require actual serial hardware.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/serial/serial_out.h>
#include <vivid/serial/serial_in.h>
#include <vivid/serial/dmx_out.h>

using namespace vivid::serial;
using Catch::Matchers::WithinAbs;

// =============================================================================
// SerialOut Tests
// =============================================================================

TEST_CASE("SerialOut parameter defaults", "[serial][serialout]") {
    SerialOut serial;

    SECTION("baudRate defaults to 9600") {
        REQUIRE(static_cast<int>(serial.baudRate) == 9600);
    }

    SECTION("name returns 'SerialOut'") {
        REQUIRE(serial.name() == "SerialOut");
    }

    SECTION("not connected initially") {
        REQUIRE_FALSE(serial.isConnected());
    }

    SECTION("port is empty initially") {
        REQUIRE(serial.port().empty());
    }
}

TEST_CASE("SerialOut parameter assignment", "[serial][serialout]") {
    SerialOut serial;

    SECTION("baudRate can be changed") {
        serial.baudRate = 115200;
        REQUIRE(static_cast<int>(serial.baudRate) == 115200);
    }

    SECTION("port can be set") {
        serial.port("/dev/tty.usbmodem14201");
        REQUIRE(serial.port() == "/dev/tty.usbmodem14201");
    }
}

TEST_CASE("SerialOut setParam/getParam", "[serial][serialout]") {
    SerialOut serial;
    float out[4] = {0};

    SECTION("setParam updates baudRate") {
        float value[4] = {115200.0f, 0, 0, 0};
        REQUIRE(serial.setParam("baudRate", value));
        REQUIRE(serial.getParam("baudRate", out));
        REQUIRE_THAT(out[0], WithinAbs(115200.0f, 1.0f));
    }

    SECTION("unknown param returns false") {
        float value[4] = {0};
        REQUIRE_FALSE(serial.setParam("nonexistent", value));
        REQUIRE_FALSE(serial.getParam("nonexistent", out));
    }
}

TEST_CASE("SerialOut params() declaration", "[serial][serialout]") {
    SerialOut serial;
    auto params = serial.params();

    SECTION("has baudRate param") {
        bool hasBaudRate = false;
        for (const auto& p : params) {
            if (p.name == "baudRate") {
                hasBaudRate = true;
                REQUIRE(p.minVal == 300.0f);
                REQUIRE(p.maxVal == 115200.0f);
            }
        }
        REQUIRE(hasBaudRate);
    }
}

// =============================================================================
// SerialIn Tests
// =============================================================================

TEST_CASE("SerialIn parameter defaults", "[serial][serialin]") {
    SerialIn serial;

    SECTION("baudRate defaults to 9600") {
        REQUIRE(static_cast<int>(serial.baudRate) == 9600);
    }

    SECTION("name returns 'SerialIn'") {
        REQUIRE(serial.name() == "SerialIn");
    }

    SECTION("not connected initially") {
        REQUIRE_FALSE(serial.isConnected());
    }

    SECTION("no data initially") {
        REQUIRE_FALSE(serial.hasData());
    }

    SECTION("lastLine is empty initially") {
        REQUIRE(serial.lastLine().empty());
    }

    SECTION("values is empty initially") {
        REQUIRE(serial.getValues().empty());
    }

    SECTION("getValue returns 0 for any index") {
        REQUIRE(serial.getValue(0) == 0.0f);
        REQUIRE(serial.getValue(5) == 0.0f);
    }
}

TEST_CASE("SerialIn parameter assignment", "[serial][serialin]") {
    SerialIn serial;

    SECTION("baudRate can be changed") {
        serial.baudRate = 57600;
        REQUIRE(static_cast<int>(serial.baudRate) == 57600);
    }

    SECTION("port can be set") {
        serial.port("COM3");
        REQUIRE(serial.port() == "COM3");
    }
}

TEST_CASE("SerialIn setParam/getParam", "[serial][serialin]") {
    SerialIn serial;
    float out[4] = {0};

    SECTION("setParam updates baudRate") {
        float value[4] = {57600.0f, 0, 0, 0};
        REQUIRE(serial.setParam("baudRate", value));
        REQUIRE(serial.getParam("baudRate", out));
        REQUIRE_THAT(out[0], WithinAbs(57600.0f, 1.0f));
    }
}

TEST_CASE("SerialIn params() declaration", "[serial][serialin]") {
    SerialIn serial;
    auto params = serial.params();

    SECTION("has baudRate param") {
        bool hasBaudRate = false;
        for (const auto& p : params) {
            if (p.name == "baudRate") hasBaudRate = true;
        }
        REQUIRE(hasBaudRate);
    }
}

// =============================================================================
// DMXOut Tests
// =============================================================================

TEST_CASE("DMXOut parameter defaults", "[serial][dmx]") {
    DMXOut dmx;

    SECTION("universe defaults to 1") {
        REQUIRE(static_cast<int>(dmx.universe) == 1);
    }

    SECTION("startChannel defaults to 1") {
        REQUIRE(static_cast<int>(dmx.startChannel) == 1);
    }

    SECTION("name returns 'DMXOut'") {
        REQUIRE(dmx.name() == "DMXOut");
    }

    SECTION("dmxBuffer is 512 bytes") {
        REQUIRE(dmx.dmxBuffer().size() == 512);
    }

    SECTION("all channels start at 0") {
        for (int i = 0; i < 512; i++) {
            REQUIRE(dmx.getChannel(i + 1) == 0);
        }
    }
}

TEST_CASE("DMXOut parameter assignment", "[serial][dmx]") {
    DMXOut dmx;

    SECTION("universe can be changed") {
        dmx.universe = 2;
        REQUIRE(static_cast<int>(dmx.universe) == 2);
    }

    SECTION("startChannel can be changed") {
        dmx.startChannel = 100;
        REQUIRE(static_cast<int>(dmx.startChannel) == 100);
    }
}

TEST_CASE("DMXOut channel operations", "[serial][dmx]") {
    DMXOut dmx;

    SECTION("single channel can be set") {
        dmx.channel(1, 255);
        REQUIRE(dmx.getChannel(1) == 255);
    }

    SECTION("multiple channels can be set") {
        dmx.channel(10, 100);
        dmx.channel(11, 150);
        dmx.channel(12, 200);
        REQUIRE(dmx.getChannel(10) == 100);
        REQUIRE(dmx.getChannel(11) == 150);
        REQUIRE(dmx.getChannel(12) == 200);
    }

    SECTION("channels() sets consecutive values") {
        std::vector<uint8_t> values = {10, 20, 30, 40, 50};
        dmx.channels(100, values);
        REQUIRE(dmx.getChannel(100) == 10);
        REQUIRE(dmx.getChannel(101) == 20);
        REQUIRE(dmx.getChannel(102) == 30);
        REQUIRE(dmx.getChannel(103) == 40);
        REQUIRE(dmx.getChannel(104) == 50);
    }

    SECTION("rgb() sets 3 channels") {
        dmx.rgb(1, 255, 128, 64);
        REQUIRE(dmx.getChannel(1) == 255);
        REQUIRE(dmx.getChannel(2) == 128);
        REQUIRE(dmx.getChannel(3) == 64);
    }

    SECTION("rgbw() sets 4 channels") {
        dmx.rgbw(10, 200, 150, 100, 50);
        REQUIRE(dmx.getChannel(10) == 200);
        REQUIRE(dmx.getChannel(11) == 150);
        REQUIRE(dmx.getChannel(12) == 100);
        REQUIRE(dmx.getChannel(13) == 50);
    }

    SECTION("blackout() sets all channels to 0") {
        dmx.channel(1, 255);
        dmx.channel(100, 128);
        dmx.channel(512, 64);
        dmx.blackout();
        REQUIRE(dmx.getChannel(1) == 0);
        REQUIRE(dmx.getChannel(100) == 0);
        REQUIRE(dmx.getChannel(512) == 0);
    }
}

TEST_CASE("DMXOut setParam/getParam", "[serial][dmx]") {
    DMXOut dmx;
    float out[4] = {0};

    SECTION("setParam updates universe") {
        float value[4] = {3.0f, 0, 0, 0};
        REQUIRE(dmx.setParam("universe", value));
        REQUIRE(dmx.getParam("universe", out));
        REQUIRE_THAT(out[0], WithinAbs(3.0f, 0.1f));
    }

    SECTION("setParam updates startChannel") {
        float value[4] = {50.0f, 0, 0, 0};
        REQUIRE(dmx.setParam("startChannel", value));
        REQUIRE(dmx.getParam("startChannel", out));
        REQUIRE_THAT(out[0], WithinAbs(50.0f, 0.1f));
    }

    SECTION("inherits baudRate from SerialOut") {
        float value[4] = {115200.0f, 0, 0, 0};
        REQUIRE(dmx.setParam("baudRate", value));
        REQUIRE(dmx.getParam("baudRate", out));
        REQUIRE_THAT(out[0], WithinAbs(115200.0f, 1.0f));
    }
}

TEST_CASE("DMXOut params() declaration", "[serial][dmx]") {
    DMXOut dmx;
    auto params = dmx.params();

    SECTION("has universe param") {
        bool hasUniverse = false;
        for (const auto& p : params) {
            if (p.name == "universe") {
                hasUniverse = true;
                REQUIRE(p.minVal == 1.0f);
                REQUIRE(p.maxVal == 16.0f);
            }
        }
        REQUIRE(hasUniverse);
    }

    SECTION("has startChannel param") {
        bool hasStartChannel = false;
        for (const auto& p : params) {
            if (p.name == "startChannel") {
                hasStartChannel = true;
                REQUIRE(p.minVal == 1.0f);
                REQUIRE(p.maxVal == 512.0f);
            }
        }
        REQUIRE(hasStartChannel);
    }

    SECTION("has at least 3 params (baudRate + universe + startChannel)") {
        REQUIRE(params.size() >= 3);
    }
}
