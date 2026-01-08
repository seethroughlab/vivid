#pragma once

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>
#include <vivid/serial/serial_port.h>
#include <memory>
#include <string>
#include <vector>

namespace vivid {
namespace serial {

/**
 * @brief Send serial data to Arduino and microcontrollers
 *
 * Sends data over a serial port to control LEDs, motors, or other hardware.
 * Supports raw bytes, strings, and CSV-formatted values. Commonly used with
 * Arduino, ESP32, and similar microcontrollers.
 *
 * @par Example
 * @code
 * auto& arduino = chain.add<SerialOut>("arduino");
 * arduino.port("/dev/tty.usbmodem14201");  // or "COM3" on Windows
 * arduino.baudRate = 115200;
 *
 * // In update():
 * float r = levels.level(0);
 * float g = levels.level(1);
 * float b = levels.level(2);
 * arduino.sendCSV({r * 255, g * 255, b * 255});  // "R,G,B\n"
 * @endcode
 *
 * @see SerialIn, DMXOut, OscOut
 */
class SerialOut : public Operator {
public:
    // -------------------------------------------------------------------------
    /// @name Self-Description
    /// @{

    static OperatorDescriptor describe() {
        return OperatorDescriptor("SerialOut", "IO", "Serial output for Arduino and other devices")
            .output(OutputKind::Value)
            .inModule("vivid-serial")
            .withUsage(
                "auto& arduino = chain.add<SerialOut>(\"arduino\");\n"
                "arduino.port(\"/dev/tty.usbmodem14201\");  // or \"COM3\" on Windows\n"
                "arduino.baudRate = 115200;\n"
                "\n"
                "// In update():\n"
                "float r = levels.level(0);\n"
                "arduino.sendCSV({r * 255, g * 255, b * 255});  // \"R,G,B\\n\"\n"
            )
            .withExamples({{"modules/vivid-serial/examples/arduino-led"}});
    }

    /// @}

    /// Baud rate parameter (exposed to UI)
    Param<int> baudRate{"baudRate", 9600, 300, 115200};

    SerialOut();
    virtual ~SerialOut() = default;

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "SerialOut"; }
    bool drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) override;

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
