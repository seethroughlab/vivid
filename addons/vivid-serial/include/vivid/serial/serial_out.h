#pragma once

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/serial/serial_port.h>
#include <memory>
#include <string>
#include <vector>

namespace vivid {
namespace serial {

/// Base serial output operator for sending data to Arduino and other devices
class SerialOut : public Operator {
public:
    /// Baud rate parameter (exposed to UI)
    Param<int> baudRate{"baudRate", 9600, 300, 115200};

    SerialOut();
    virtual ~SerialOut() = default;

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "SerialOut"; }
    bool drawVisualization(ImDrawList* dl, float minX, float minY, float maxX, float maxY) override;

    // Parameter interface
    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// Set the serial port to use
    /// @param portName e.g., "/dev/tty.usbmodem14201" or "COM3"
    void port(const std::string& portName);

    /// Get the current port name
    const std::string& port() const { return m_portName; }

    /// Check if connected
    bool isConnected() const;

    // Send data methods

    /// Send raw bytes
    void send(const uint8_t* data, size_t len);

    /// Send a string
    void send(const std::string& data);

    /// Send a string with newline appended
    void sendLine(const std::string& line);

    /// Send a float value as text
    void sendFloat(float value);

    /// Send an integer value as text
    void sendInt(int value);

    /// Send multiple values as CSV (comma-separated, with newline)
    /// e.g., sendCSV({1.0, 2.5, 3.0}) sends "1.0,2.5,3.0\n"
    void sendCSV(const std::vector<float>& values);

protected:
    std::unique_ptr<SerialPort> m_serial;
    std::string m_portName;
    bool m_needsReconnect = false;
};

} // namespace serial
} // namespace vivid
