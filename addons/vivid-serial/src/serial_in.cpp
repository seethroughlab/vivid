#include <vivid/serial/serial_in.h>
#include <vivid/operator_registry.h>
#include <sstream>
#include <iostream>

namespace vivid {
namespace serial {

REGISTER_ADDON_OPERATOR(SerialIn, "IO", "Serial input from Arduino and sensors", false, "vivid-serial");

SerialIn::SerialIn() {
    m_serial = std::make_unique<SerialPort>();
}

SerialIn::~SerialIn() {
    stopReadThread();
}

void SerialIn::init(Context& ctx) {
    if (!m_portName.empty() && !m_serial->isOpen()) {
        if (m_serial->open(m_portName, baudRate)) {
            startReadThread();
        }
    }
}

void SerialIn::process(Context& ctx) {
    // Handle reconnection if port changed
    if (m_needsReconnect) {
        stopReadThread();
        m_serial->close();
        if (!m_portName.empty()) {
            if (m_serial->open(m_portName, baudRate)) {
                startReadThread();
            }
        }
        m_needsReconnect = false;
    }

    // Parse latest line if we have new data
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_lines.empty()) {
            m_lastLine = m_lines.back();
            m_lines.clear();
            m_hasNewData = true;
            parseCSV(m_lastLine);
        } else {
            m_hasNewData = false;
        }
    }
}

void SerialIn::cleanup() {
    stopReadThread();
    m_serial->close();
}

std::vector<ParamDecl> SerialIn::params() {
    return { baudRate.decl() };
}

bool SerialIn::getParam(const std::string& name, float out[4]) {
    if (name == "baudRate") {
        out[0] = static_cast<float>(baudRate);
        return true;
    }
    return false;
}

bool SerialIn::setParam(const std::string& name, const float value[4]) {
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

void SerialIn::port(const std::string& portName) {
    if (portName != m_portName) {
        m_portName = portName;
        m_needsReconnect = true;
    }
}

bool SerialIn::isConnected() const {
    return m_serial && m_serial->isOpen();
}

std::vector<std::string> SerialIn::getLines() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> result = std::move(m_lines);
    m_lines.clear();
    return result;
}

float SerialIn::getValue(int index) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= 0 && index < static_cast<int>(m_values.size())) {
        return m_values[index];
    }
    return 0.0f;
}

void SerialIn::startReadThread() {
    if (m_running) return;

    m_running = true;
    m_readThread = std::thread(&SerialIn::readThreadFunc, this);
}

void SerialIn::stopReadThread() {
    if (!m_running) return;

    m_running = false;
    if (m_readThread.joinable()) {
        m_readThread.join();
    }
}

void SerialIn::readThreadFunc() {
    uint8_t buffer[256];

    while (m_running) {
        if (!m_serial || !m_serial->isOpen()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        size_t bytesRead = m_serial->read(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            std::lock_guard<std::mutex> lock(m_mutex);

            for (size_t i = 0; i < bytesRead; ++i) {
                char ch = static_cast<char>(buffer[i]);
                if (ch == '\n') {
                    // Remove trailing \r if present
                    if (!m_buffer.empty() && m_buffer.back() == '\r') {
                        m_buffer.pop_back();
                    }
                    if (!m_buffer.empty()) {
                        m_lines.push_back(m_buffer);
                    }
                    m_buffer.clear();
                } else {
                    m_buffer += ch;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void SerialIn::parseCSV(const std::string& line) {
    m_values.clear();

    std::istringstream ss(line);
    std::string token;

    while (std::getline(ss, token, ',')) {
        try {
            float value = std::stof(token);
            m_values.push_back(value);
        } catch (...) {
            // Skip non-numeric values
        }
    }
}

} // namespace serial
} // namespace vivid
