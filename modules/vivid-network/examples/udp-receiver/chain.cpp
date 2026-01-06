// UDP Receiver Example
// Demonstrates receiving UDP packets and visualizing the data
//
// Send data with: echo "Hello from UDP" | nc -u 127.0.0.1 5000
// Or send floats: python3 -c "import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.sendto(bytes([0,0,128,63,0,0,0,64]), ('127.0.0.1', 5000))"

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/network/network.h>
#include <iostream>
#include <iomanip>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::network;

// Visualization state
static std::string g_lastMessage;
static float g_messageAge = 0.0f;
static std::vector<float> g_receivedFloats;
static size_t g_packetCount = 0;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // UDP receiver on port 5000
    auto& udp = chain.add<UdpIn>("udp");
    udp.port(5000);

    // Visual display
    auto& display = chain.add<Canvas>("display");
    display.setResolution(800, 600);

    chain.output("display");

    std::cout << "UDP Receiver listening on port 5000" << std::endl;
    std::cout << "Send data with: echo 'Hello' | nc -u 127.0.0.1 5000" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = static_cast<float>(ctx.dt());

    // Check for received UDP data
    auto& udp = chain.get<UdpIn>("udp");
    if (udp.hasData()) {
        g_packetCount++;
        g_messageAge = 0.0f;

        // Try to interpret as string
        g_lastMessage = udp.asString();

        // Also try to interpret as floats
        g_receivedFloats = udp.asFloats();

        std::cout << "[UDP] Received " << udp.size() << " bytes from "
                  << udp.senderAddress() << ":" << udp.senderPort() << std::endl;

        // Print as hex dump
        std::cout << "  Hex: ";
        auto& data = udp.data();
        for (size_t i = 0; i < std::min(data.size(), size_t(32)); i++) {
            std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)data[i] << " ";
        }
        if (data.size() > 32) std::cout << "...";
        std::cout << std::dec << std::endl;

        // Print as string if printable
        bool printable = true;
        for (char c : g_lastMessage) {
            if (c != '\n' && c != '\r' && (c < 32 || c > 126)) {
                printable = false;
                break;
            }
        }
        if (printable && !g_lastMessage.empty()) {
            std::cout << "  Text: \"" << g_lastMessage << "\"" << std::endl;
        }

        // Print as floats if valid
        if (!g_receivedFloats.empty()) {
            std::cout << "  Floats: ";
            for (float f : g_receivedFloats) {
                std::cout << f << " ";
            }
            std::cout << std::endl;
        }
    }

    // Decay message age
    g_messageAge += dt;

    // Draw visualization
    auto& canvas = chain.get<Canvas>("display");
    canvas.clear(0.05f, 0.05f, 0.1f, 1.0f);

    // Title
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("UDP Receiver - Port 5000", 20, 30);

    // Packet count
    std::string countStr = "Packets received: " + std::to_string(g_packetCount);
    canvas.fillStyle(0.7f, 0.7f, 0.7f, 1.0f);
    canvas.fillText(countStr, 20, 60);

    // Last message with fade
    if (!g_lastMessage.empty()) {
        float alpha = std::max(0.0f, 1.0f - g_messageAge * 0.2f);
        canvas.fillStyle(0.5f, 0.8f, 1.0f, alpha);
        canvas.fillText("Last message:", 20, 120);

        // Truncate long messages
        std::string displayMsg = g_lastMessage.substr(0, 50);
        if (g_lastMessage.length() > 50) displayMsg += "...";
        canvas.fillStyle(1.0f, 1.0f, 1.0f, alpha);
        canvas.fillText(displayMsg, 40, 150);
    }

    // Float visualization as bars
    if (!g_receivedFloats.empty()) {
        float alpha = std::max(0.0f, 1.0f - g_messageAge * 0.2f);
        canvas.fillStyle(0.5f, 0.8f, 1.0f, alpha);
        canvas.fillText("Float values:", 20, 220);

        float barWidth = 60.0f;
        float barMaxHeight = 200.0f;
        float startX = 40.0f;

        for (size_t i = 0; i < std::min(g_receivedFloats.size(), size_t(10)); i++) {
            float value = std::clamp(g_receivedFloats[i], 0.0f, 1.0f);
            float barHeight = value * barMaxHeight;
            float x = startX + i * (barWidth + 10);
            float y = 450 - barHeight;

            // Bar background
            canvas.fillStyle(0.2f, 0.2f, 0.2f, alpha);
            canvas.fillRect(x, 250, barWidth, barMaxHeight);

            // Bar fill
            canvas.fillStyle(0.3f + value * 0.5f, 0.8f - value * 0.3f, 0.3f, alpha);
            canvas.fillRect(x, y, barWidth, barHeight);

            // Value label
            char label[16];
            snprintf(label, sizeof(label), "%.2f", g_receivedFloats[i]);
            canvas.fillStyle(1.0f, 1.0f, 1.0f, alpha * 0.8f);
            canvas.fillText(label, x + 10, 470);
        }
    }

    // Connection indicator
    float pulse = (sin(static_cast<float>(ctx.time()) * 3.0f) + 1.0f) * 0.5f;
    if (udp.isListening()) {
        canvas.fillStyle(0.2f, 0.8f, 0.2f, 0.5f + pulse * 0.5f);
    } else {
        canvas.fillStyle(0.8f, 0.2f, 0.2f, 1.0f);
    }
    canvas.fillCircle(760, 30, 10, 16);
}

VIVID_CHAIN(setup, update)
