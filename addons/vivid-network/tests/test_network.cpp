/**
 * @file test_network.cpp
 * @brief Unit tests for network operators
 *
 * Tests OscIn, OscOut, UdpIn, UdpOut configuration and state.
 * Note: These tests don't require actual network connections.
 */

#include <catch2/catch_test_macros.hpp>
#include <vivid/network/osc_in.h>
#include <vivid/network/osc_out.h>
#include <vivid/network/udp_in.h>
#include <vivid/network/udp_out.h>

using namespace vivid::network;

// =============================================================================
// OscIn Tests
// =============================================================================

TEST_CASE("OscIn configuration defaults", "[network][osc]") {
    OscIn osc;

    SECTION("default port is 8000") {
        REQUIRE(osc.getPort() == 8000);
    }

    SECTION("not listening initially") {
        REQUIRE_FALSE(osc.isListening());
    }

    SECTION("name returns 'OscIn'") {
        REQUIRE(osc.name() == "OscIn");
    }

    SECTION("no messages initially") {
        REQUIRE(osc.messages().empty());
    }
}

TEST_CASE("OscIn port configuration", "[network][osc]") {
    OscIn osc;

    SECTION("port can be changed") {
        osc.port(9000);
        REQUIRE(osc.getPort() == 9000);
    }

    SECTION("port can be set to common values") {
        osc.port(8080);
        REQUIRE(osc.getPort() == 8080);

        osc.port(57120);  // SuperCollider default
        REQUIRE(osc.getPort() == 57120);
    }
}

TEST_CASE("OscIn message access", "[network][osc]") {
    OscIn osc;

    SECTION("hasMessage returns false when no messages") {
        REQUIRE_FALSE(osc.hasMessage("/fader/1"));
    }

    SECTION("getFloat returns default when no message") {
        REQUIRE(osc.getFloat("/fader/1") == 0.0f);
        REQUIRE(osc.getFloat("/fader/1", 0.5f) == 0.5f);
    }

    SECTION("getInt returns default when no message") {
        REQUIRE(osc.getInt("/button/1") == 0);
        REQUIRE(osc.getInt("/button/1", 42) == 42);
    }

    SECTION("getMessages returns empty for unknown pattern") {
        auto msgs = osc.getMessages("/unknown/*");
        REQUIRE(msgs.empty());
    }
}

// =============================================================================
// OscOut Tests
// =============================================================================

TEST_CASE("OscOut configuration defaults", "[network][osc]") {
    OscOut osc;

    SECTION("default host is 127.0.0.1") {
        REQUIRE(osc.getHost() == "127.0.0.1");
    }

    SECTION("default port is 9000") {
        REQUIRE(osc.getPort() == 9000);
    }

    SECTION("not ready initially (no socket)") {
        REQUIRE_FALSE(osc.isReady());
    }

    SECTION("name returns 'OscOut'") {
        REQUIRE(osc.name() == "OscOut");
    }

    SECTION("messages sent is 0 initially") {
        REQUIRE(osc.messagesSent() == 0);
    }
}

TEST_CASE("OscOut configuration", "[network][osc]") {
    OscOut osc;

    SECTION("host can be changed") {
        osc.host("192.168.1.100");
        REQUIRE(osc.getHost() == "192.168.1.100");
    }

    SECTION("port can be changed") {
        osc.port(8000);
        REQUIRE(osc.getPort() == 8000);
    }

    SECTION("multiple configuration changes") {
        osc.host("10.0.0.1");
        osc.port(12345);
        REQUIRE(osc.getHost() == "10.0.0.1");
        REQUIRE(osc.getPort() == 12345);
    }
}

// =============================================================================
// UdpIn Tests
// =============================================================================

TEST_CASE("UdpIn configuration defaults", "[network][udp]") {
    UdpIn udp;

    SECTION("default port is 5000") {
        REQUIRE(udp.getPort() == 5000);
    }

    SECTION("not listening initially") {
        REQUIRE_FALSE(udp.isListening());
    }

    SECTION("no data initially") {
        REQUIRE_FALSE(udp.hasData());
    }

    SECTION("data is empty initially") {
        REQUIRE(udp.data().empty());
        REQUIRE(udp.size() == 0);
    }

    SECTION("name returns 'UdpIn'") {
        REQUIRE(udp.name() == "UdpIn");
    }

    SECTION("sender info is empty initially") {
        REQUIRE(udp.senderAddress().empty());
        REQUIRE(udp.senderPort() == 0);
    }
}

TEST_CASE("UdpIn port configuration", "[network][udp]") {
    UdpIn udp;

    SECTION("port can be changed") {
        udp.port(6000);
        REQUIRE(udp.getPort() == 6000);
    }

    SECTION("port can be set to Artnet default") {
        udp.port(6454);  // Artnet
        REQUIRE(udp.getPort() == 6454);
    }
}

// =============================================================================
// UdpOut Tests
// =============================================================================

TEST_CASE("UdpOut configuration defaults", "[network][udp]") {
    UdpOut udp;

    SECTION("default host is 127.0.0.1") {
        REQUIRE(udp.getHost() == "127.0.0.1");
    }

    SECTION("default port is 5000") {
        REQUIRE(udp.getPort() == 5000);
    }

    SECTION("not ready initially (no socket)") {
        REQUIRE_FALSE(udp.isReady());
    }

    SECTION("name returns 'UdpOut'") {
        REQUIRE(udp.name() == "UdpOut");
    }

    SECTION("statistics are 0 initially") {
        REQUIRE(udp.packetsSent() == 0);
        REQUIRE(udp.bytesSent() == 0);
    }
}

TEST_CASE("UdpOut configuration", "[network][udp]") {
    UdpOut udp;

    SECTION("host can be changed") {
        udp.host("192.168.1.255");
        REQUIRE(udp.getHost() == "192.168.1.255");
    }

    SECTION("port can be changed") {
        udp.port(6454);
        REQUIRE(udp.getPort() == 6454);
    }

    SECTION("broadcast can be enabled") {
        // Just verify it doesn't crash - we can't check internal state
        udp.broadcast(true);
        udp.broadcast(false);
    }
}

// =============================================================================
// OscMessage Tests
// =============================================================================

TEST_CASE("OscMessage argument access", "[network][osc]") {
    OscMessage msg;
    msg.address = "/test";

    SECTION("empty message returns defaults") {
        REQUIRE(msg.argCount() == 0);
        REQUIRE(msg.floatArg(0) == 0.0f);
        REQUIRE(msg.intArg(0) == 0);
        REQUIRE(msg.stringArg(0).empty());
    }

    SECTION("float argument") {
        msg.args.push_back(0.75f);
        REQUIRE(msg.argCount() == 1);
        REQUIRE(msg.floatArg(0) == 0.75f);
    }

    SECTION("int argument") {
        msg.args.push_back(int32_t(42));
        REQUIRE(msg.argCount() == 1);
        REQUIRE(msg.intArg(0) == 42);
    }

    SECTION("string argument") {
        msg.args.push_back(std::string("hello"));
        REQUIRE(msg.argCount() == 1);
        REQUIRE(msg.stringArg(0) == "hello");
    }

    SECTION("multiple arguments") {
        msg.args.push_back(1.0f);
        msg.args.push_back(2.0f);
        msg.args.push_back(3.0f);
        REQUIRE(msg.argCount() == 3);
        REQUIRE(msg.floatArg(0) == 1.0f);
        REQUIRE(msg.floatArg(1) == 2.0f);
        REQUIRE(msg.floatArg(2) == 3.0f);
    }

    SECTION("out of bounds returns default") {
        msg.args.push_back(1.0f);
        REQUIRE(msg.floatArg(5) == 0.0f);
        REQUIRE(msg.intArg(5) == 0);
        REQUIRE(msg.stringArg(5).empty());
    }
}
