#pragma once

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/serial/serial_port.h>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

namespace vivid {
namespace serial {

/// Serial input operator for receiving data from Arduino and sensors
class SerialIn : public Operator {
public:
    /// Baud rate parameter (exposed to UI)
    Param<int> baudRate{"baudRate", 9600, 300, 115200};

    SerialIn();
    virtual ~SerialIn();

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "SerialIn"; }

    // Parameter interface
    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// Set the serial port to use
    void port(const std::string& portName);

    /// Get the current port name
    const std::string& port() const { return m_portName; }

    /// Check if connected
    bool isConnected() const;

    // Read data methods

    /// Check if new data is available since last frame
    bool hasData() const { return m_hasNewData; }

    /// Get the last line received
    const std::string& lastLine() const { return m_lastLine; }

    /// Get all lines received since last frame
    std::vector<std::string> getLines();

    /// Get parsed CSV values from the last line
    /// e.g., if Arduino sends "1.0,2.5,3.0\n", returns {1.0, 2.5, 3.0}
    const std::vector<float>& getValues() const { return m_values; }

    /// Get a specific value by index (returns 0 if index out of range)
    float getValue(int index = 0) const;

    /// Get value as output (for connecting to other operators)
    float value() const { return getValue(0); }

private:
    void startReadThread();
    void stopReadThread();
    void readThreadFunc();
    void parseCSV(const std::string& line);

    std::unique_ptr<SerialPort> m_serial;
    std::string m_portName;
    bool m_needsReconnect = false;

    // Read thread
    std::thread m_readThread;
    std::atomic<bool> m_running{false};

    // Data buffers (protected by mutex)
    mutable std::mutex m_mutex;
    std::string m_buffer;
    std::vector<std::string> m_lines;
    std::string m_lastLine;
    std::vector<float> m_values;
    bool m_hasNewData = false;
};

} // namespace serial
} // namespace vivid
