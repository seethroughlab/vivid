#include <vivid/serial/dmx_out.h>
#include <vivid/operator_registry.h>
#include <iostream>
#include <cstring>

namespace vivid {
namespace serial {

REGISTER_ADDON_OPERATOR(DMXOut, "IO", "DMX lighting output via Enttec USB Pro", false, "vivid-serial");

DMXOut::DMXOut() : SerialOut() {
    // DMX requires 250000 baud for Enttec Pro
    baudRate = 250000;

    // Initialize DMX buffer to zero
    m_dmxBuffer.fill(0);
}

void DMXOut::init(Context& ctx) {
    // Call parent init
    SerialOut::init(ctx);

    // Send initial DMX frame
    if (isConnected()) {
        sendEnttecFrame();
    }
}

void DMXOut::process(Context& ctx) {
    // Call parent process for reconnection handling
    SerialOut::process(ctx);

    // Send DMX frame if dirty
    if (m_dirty && isConnected()) {
        sendEnttecFrame();
        m_dirty = false;
    }
}

std::vector<ParamDecl> DMXOut::params() {
    auto parentParams = SerialOut::params();
    parentParams.push_back(universe.decl());
    parentParams.push_back(startChannel.decl());
    return parentParams;
}

bool DMXOut::getParam(const std::string& name, float out[4]) {
    if (name == "universe") {
        out[0] = static_cast<float>(universe);
        return true;
    }
    if (name == "startChannel") {
        out[0] = static_cast<float>(startChannel);
        return true;
    }
    return SerialOut::getParam(name, out);
}

bool DMXOut::setParam(const std::string& name, const float value[4]) {
    if (name == "universe") {
        universe = static_cast<int>(value[0]);
        return true;
    }
    if (name == "startChannel") {
        startChannel = static_cast<int>(value[0]);
        return true;
    }
    return SerialOut::setParam(name, value);
}

void DMXOut::channel(int ch, uint8_t value) {
    if (ch >= 1 && ch <= 512) {
        m_dmxBuffer[ch - 1] = value;
        m_dirty = true;
    }
}

void DMXOut::channels(int start, const std::vector<uint8_t>& values) {
    for (size_t i = 0; i < values.size(); ++i) {
        int ch = start + static_cast<int>(i);
        if (ch >= 1 && ch <= 512) {
            m_dmxBuffer[ch - 1] = values[i];
        }
    }
    m_dirty = true;
}

void DMXOut::rgb(int startCh, uint8_t r, uint8_t g, uint8_t b) {
    channel(startCh, r);
    channel(startCh + 1, g);
    channel(startCh + 2, b);
}

void DMXOut::rgbw(int startCh, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    channel(startCh, r);
    channel(startCh + 1, g);
    channel(startCh + 2, b);
    channel(startCh + 3, w);
}

void DMXOut::blackout() {
    m_dmxBuffer.fill(0);
    m_dirty = true;
}

uint8_t DMXOut::getChannel(int ch) const {
    if (ch >= 1 && ch <= 512) {
        return m_dmxBuffer[ch - 1];
    }
    return 0;
}

void DMXOut::sendEnttecFrame() {
    if (!m_serial || !m_serial->isOpen()) return;

    // Enttec DMX USB Pro packet format:
    // START_BYTE | LABEL | DATA_LENGTH_LSB | DATA_LENGTH_MSB | DATA... | END_BYTE

    // For DMX, we send 513 bytes: 1 start code (0x00) + 512 channel values
    const uint16_t dataLength = 513;

    // Build the packet
    std::vector<uint8_t> packet;
    packet.reserve(5 + dataLength);

    packet.push_back(START_BYTE);           // 0x7E
    packet.push_back(SEND_DMX_LABEL);       // 0x06 = Send DMX Packet
    packet.push_back(dataLength & 0xFF);    // Length LSB
    packet.push_back(dataLength >> 8);      // Length MSB
    packet.push_back(0x00);                 // DMX start code

    // Add the 512 channel values
    packet.insert(packet.end(), m_dmxBuffer.begin(), m_dmxBuffer.end());

    packet.push_back(END_BYTE);             // 0xE7

    // Send the packet
    m_serial->write(packet.data(), packet.size());
}

bool DMXOut::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    float w = maxX - minX;
    float h = maxY - minY;
    float cx = minX + w * 0.5f;

    // Background
    bool connected = isConnected();
    uint32_t bgColor = connected ? VIZ_COL32(40, 30, 60, 255) : VIZ_COL32(60, 30, 30, 255);
    dl->AddRectFilled(VizVec2(minX, minY), VizVec2(maxX, maxY), bgColor);

    // DMX label
    const char* label = "DMX";
    VizTextSize textSize = dl->CalcTextSize(label);
    uint32_t textColor = connected ? VIZ_COL32(200, 100, 255, 255) : VIZ_COL32(150, 150, 150, 255);
    dl->AddText(VizVec2(cx - textSize.x * 0.5f, minY + 4), textColor, label);

    // Draw mini channel bars (first 16 channels as a preview)
    float barAreaTop = minY + 20;
    float barAreaBottom = maxY - 4;
    float barHeight = barAreaBottom - barAreaTop;
    float barWidth = w / 18.0f;
    float startX = minX + barWidth;

    for (int i = 0; i < 16 && i < 512; ++i) {
        int ch = static_cast<int>(startChannel) - 1 + i;
        if (ch < 0 || ch >= 512) continue;

        float val = m_dmxBuffer[ch] / 255.0f;
        float barX = startX + i * barWidth;
        float filledHeight = barHeight * val;

        // Bar background
        dl->AddRectFilled(VizVec2(barX, barAreaTop), VizVec2(barX + barWidth * 0.8f, barAreaBottom),
                          VIZ_COL32(30, 30, 30, 255));

        // Bar fill
        if (val > 0) {
            uint32_t barColor = VIZ_COL32(100 + static_cast<int>(155 * val), 50, 200, 255);
            dl->AddRectFilled(VizVec2(barX, barAreaBottom - filledHeight),
                              VizVec2(barX + barWidth * 0.8f, barAreaBottom), barColor);
        }
    }

    return true;
}

} // namespace serial
} // namespace vivid
