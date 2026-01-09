#pragma once

#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace vivid {
namespace serial {

/// Low-level serial port wrapper with cross-platform support.
/// @note NOT thread-safe. Callers must synchronize access when used from multiple threads.
class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();

    // Disable copy
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // Enable move
    SerialPort(SerialPort&& other) noexcept;
    SerialPort& operator=(SerialPort&& other) noexcept;

    /// Open a serial port
    /// @param port Port name (e.g., "/dev/tty.usbmodem14201" on macOS, "COM3" on Windows)
    /// @param baudRate Baud rate (default 9600)
    /// @return true if successful
    bool open(const std::string& port, int baudRate = 9600);

    /// Close the serial port
    void close();

    /// Check if the port is open
    [[nodiscard]] bool isOpen() const;

    /// Get the port name
    [[nodiscard]] const std::string& portName() const { return m_portName; }

    /// Write raw bytes
    /// @return Number of bytes written
    [[nodiscard]] size_t write(const uint8_t* data, size_t len);

    /// Write a string
    [[nodiscard]] size_t write(const std::string& str);

    /// Read raw bytes (non-blocking)
    /// @return Number of bytes read
    [[nodiscard]] size_t read(uint8_t* buffer, size_t maxLen);

    /// Read a line (blocking until newline or timeout)
    /// @param timeoutMs Timeout in milliseconds (0 = no timeout)
    /// @return The line read (without newline), or empty string on timeout
    [[nodiscard]] std::string readLine(int timeoutMs = 1000);

    /// Get number of bytes available to read
    [[nodiscard]] size_t available() const;

    /// Flush input and output buffers
    void flush();

    /// Get list of available serial ports
    [[nodiscard]] static std::vector<std::string> availablePorts();

private:
    std::string m_portName;
    int m_baudRate = 9600;

#ifdef _WIN32
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
    int m_fd = -1;
#endif
};

} // namespace serial
} // namespace vivid
