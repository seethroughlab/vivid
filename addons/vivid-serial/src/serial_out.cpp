#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vivid/serial/serial_out.h>
#include <vivid/operator_registry.h>
#include <imgui.h>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace vivid {
namespace serial {

REGISTER_ADDON_OPERATOR(SerialOut, "IO", "Serial output for Arduino and other devices", false, "vivid-serial");

SerialOut::SerialOut() {
    m_serial = std::make_unique<SerialPort>();
}

void SerialOut::init(Context& ctx) {
    if (!m_portName.empty() && !m_serial->isOpen()) {
        m_serial->open(m_portName, baudRate);
    }
}

void SerialOut::process(Context& ctx) {
    // Handle reconnection if port changed
    if (m_needsReconnect) {
        m_serial->close();
        if (!m_portName.empty()) {
            m_serial->open(m_portName, baudRate);
        }
        m_needsReconnect = false;
    }
}

void SerialOut::cleanup() {
    m_serial->close();
}

std::vector<ParamDecl> SerialOut::params() {
    return { baudRate.decl() };
}

bool SerialOut::getParam(const std::string& name, float out[4]) {
    if (name == "baudRate") {
        out[0] = static_cast<float>(baudRate);
        return true;
    }
    return false;
}

bool SerialOut::setParam(const std::string& name, const float value[4]) {
    if (name == "baudRate") {
        int newRate = static_cast<int>(value[0]);
        if (newRate != baudRate) {
            baudRate = newRate;
            m_needsReconnect = true;
        }
        return true;
    }
    return false;
}

void SerialOut::port(const std::string& portName) {
    if (portName != m_portName) {
        m_portName = portName;
        m_needsReconnect = true;
    }
}

bool SerialOut::isConnected() const {
    return m_serial && m_serial->isOpen();
}

void SerialOut::send(const uint8_t* data, size_t len) {
    if (m_serial && m_serial->isOpen()) {
        m_serial->write(data, len);
    }
}

void SerialOut::send(const std::string& data) {
    if (m_serial && m_serial->isOpen()) {
        m_serial->write(data);
    }
}

void SerialOut::sendLine(const std::string& line) {
    send(line + "\n");
}

void SerialOut::sendFloat(float value) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << value;
    sendLine(ss.str());
}

void SerialOut::sendInt(int value) {
    sendLine(std::to_string(value));
}

void SerialOut::sendCSV(const std::vector<float>& values) {
    if (values.empty()) return;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) ss << ",";
        ss << values[i];
    }
    sendLine(ss.str());
}

bool SerialOut::drawVisualization(ImDrawList* dl, float minX, float minY, float maxX, float maxY) {
    float w = maxX - minX;
    float h = maxY - minY;
    float cx = minX + w * 0.5f;
    float cy = minY + h * 0.5f;
    float r = std::min(w, h) * 0.35f;

    // Background circle
    bool connected = isConnected();
    ImU32 bgColor = connected ? IM_COL32(30, 30, 80, 255) : IM_COL32(60, 30, 30, 255);
    dl->AddCircleFilled(ImVec2(cx, cy), r, bgColor);
    dl->AddCircle(ImVec2(cx, cy), r, IM_COL32(100, 100, 100, 255), 32, 2.0f);

    // TX indicator
    ImU32 textColor = connected ? IM_COL32(100, 150, 255, 255) : IM_COL32(180, 180, 180, 255);

    const char* label = "TX";
    ImVec2 textSize = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(cx - textSize.x * 0.5f, cy - textSize.y * 0.5f - r * 0.15f), textColor, label);

    // Serial icon (USB plug)
    float iconY = cy + r * 0.15f;
    dl->AddRectFilled(ImVec2(cx - r * 0.2f, iconY), ImVec2(cx + r * 0.2f, iconY + r * 0.3f),
                      connected ? IM_COL32(100, 150, 255, 255) : IM_COL32(150, 150, 150, 255));

    return true;
}

} // namespace serial
} // namespace vivid
