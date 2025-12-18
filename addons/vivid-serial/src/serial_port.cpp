#include <vivid/serial/serial_port.h>
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

#ifdef _WIN32
// Windows serial implementation
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")
#else
// POSIX serial implementation (macOS, Linux)
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/IOBSD.h>
#endif

namespace vivid {
namespace serial {

SerialPort::~SerialPort() {
    close();
}

SerialPort::SerialPort(SerialPort&& other) noexcept
    : m_portName(std::move(other.m_portName))
    , m_baudRate(other.m_baudRate)
#ifdef _WIN32
    , m_handle(other.m_handle)
#else
    , m_fd(other.m_fd)
#endif
{
#ifdef _WIN32
    other.m_handle = INVALID_HANDLE_VALUE;
#else
    other.m_fd = -1;
#endif
}

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept {
    if (this != &other) {
        close();
        m_portName = std::move(other.m_portName);
        m_baudRate = other.m_baudRate;
#ifdef _WIN32
        m_handle = other.m_handle;
        other.m_handle = INVALID_HANDLE_VALUE;
#else
        m_fd = other.m_fd;
        other.m_fd = -1;
#endif
    }
    return *this;
}

#ifdef _WIN32
// Windows implementation

bool SerialPort::open(const std::string& port, int baudRate) {
    close();

    std::string portPath = port;
    // Handle COM ports > 9
    if (port.find("COM") == 0 && port.length() > 4) {
        portPath = "\\\\.\\" + port;
    }

    m_handle = CreateFileA(
        portPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );

    if (m_handle == INVALID_HANDLE_VALUE) {
        std::cerr << "[SerialPort] Failed to open " << port << std::endl;
        return false;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);

    if (!GetCommState(m_handle, &dcb)) {
        close();
        return false;
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(m_handle, &dcb)) {
        close();
        return false;
    }

    // Set timeouts
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(m_handle, &timeouts);

    m_portName = port;
    m_baudRate = baudRate;

    std::cout << "[SerialPort] Opened " << port << " at " << baudRate << " baud" << std::endl;
    return true;
}

void SerialPort::close() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        std::cout << "[SerialPort] Closed " << m_portName << std::endl;
    }
}

bool SerialPort::isOpen() const {
    return m_handle != INVALID_HANDLE_VALUE;
}

size_t SerialPort::write(const uint8_t* data, size_t len) {
    if (!isOpen()) return 0;

    DWORD written = 0;
    WriteFile(m_handle, data, static_cast<DWORD>(len), &written, nullptr);
    return written;
}

size_t SerialPort::read(uint8_t* buffer, size_t maxLen) {
    if (!isOpen()) return 0;

    DWORD bytesRead = 0;
    ReadFile(m_handle, buffer, static_cast<DWORD>(maxLen), &bytesRead, nullptr);
    return bytesRead;
}

size_t SerialPort::available() const {
    if (!isOpen()) return 0;

    COMSTAT stat;
    DWORD errors;
    if (ClearCommError(m_handle, &errors, &stat)) {
        return stat.cbInQue;
    }
    return 0;
}

void SerialPort::flush() {
    if (isOpen()) {
        FlushFileBuffers(m_handle);
        PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    }
}

std::vector<std::string> SerialPort::availablePorts() {
    std::vector<std::string> ports;

    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
        &GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT
    );

    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return ports;
    }

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
        HKEY hKey = SetupDiOpenDevRegKey(
            deviceInfoSet, &deviceInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ
        );

        if (hKey != INVALID_HANDLE_VALUE) {
            char portName[256];
            DWORD size = sizeof(portName);
            DWORD type = 0;

            if (RegQueryValueExA(hKey, "PortName", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(portName), &size) == ERROR_SUCCESS) {
                if (type == REG_SZ && strncmp(portName, "COM", 3) == 0) {
                    ports.push_back(portName);
                }
            }
            RegCloseKey(hKey);
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return ports;
}

#else
// POSIX implementation (macOS, Linux)

static speed_t baudRateToSpeed(int baudRate) {
    switch (baudRate) {
        case 300:    return B300;
        case 600:    return B600;
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
#ifdef B250000
        case 250000: return B250000;
#endif
        default:     return B9600;
    }
}

bool SerialPort::open(const std::string& port, int baudRate) {
    close();

    m_fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        std::cerr << "[SerialPort] Failed to open " << port << ": " << strerror(errno) << std::endl;
        return false;
    }

    // Configure serial port
    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(m_fd, &tty) != 0) {
        std::cerr << "[SerialPort] tcgetattr failed: " << strerror(errno) << std::endl;
        close();
        return false;
    }

    speed_t speed = baudRateToSpeed(baudRate);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // 8N1 mode
    tty.c_cflag &= ~PARENB;  // No parity
    tty.c_cflag &= ~CSTOPB;  // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;      // 8 data bits

    tty.c_cflag |= CREAD | CLOCAL;  // Enable receiver, ignore modem control lines

    // Raw mode
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;

    // Non-blocking read
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(m_fd, TCSANOW, &tty) != 0) {
        std::cerr << "[SerialPort] tcsetattr failed: " << strerror(errno) << std::endl;
        close();
        return false;
    }

    // Clear any pending data
    tcflush(m_fd, TCIOFLUSH);

    m_portName = port;
    m_baudRate = baudRate;

    std::cout << "[SerialPort] Opened " << port << " at " << baudRate << " baud" << std::endl;
    return true;
}

void SerialPort::close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
        std::cout << "[SerialPort] Closed " << m_portName << std::endl;
    }
}

bool SerialPort::isOpen() const {
    return m_fd >= 0;
}

size_t SerialPort::write(const uint8_t* data, size_t len) {
    if (!isOpen()) return 0;

    ssize_t written = ::write(m_fd, data, len);
    return written > 0 ? static_cast<size_t>(written) : 0;
}

size_t SerialPort::read(uint8_t* buffer, size_t maxLen) {
    if (!isOpen()) return 0;

    ssize_t bytesRead = ::read(m_fd, buffer, maxLen);
    return bytesRead > 0 ? static_cast<size_t>(bytesRead) : 0;
}

size_t SerialPort::available() const {
    if (!isOpen()) return 0;

    int bytes = 0;
    if (ioctl(m_fd, FIONREAD, &bytes) == 0) {
        return static_cast<size_t>(bytes);
    }
    return 0;
}

void SerialPort::flush() {
    if (isOpen()) {
        tcflush(m_fd, TCIOFLUSH);
    }
}

#ifdef __APPLE__
// macOS port enumeration using IOKit

std::vector<std::string> SerialPort::availablePorts() {
    std::vector<std::string> ports;

    CFMutableDictionaryRef matchingDict = IOServiceMatching(kIOSerialBSDServiceValue);
    if (!matchingDict) return ports;

    CFDictionarySetValue(matchingDict,
                         CFSTR(kIOSerialBSDTypeKey),
                         CFSTR(kIOSerialBSDAllTypes));

    io_iterator_t iterator;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matchingDict, &iterator);
    if (kr != KERN_SUCCESS) return ports;

    io_object_t device;
    while ((device = IOIteratorNext(iterator))) {
        CFStringRef pathRef = (CFStringRef)IORegistryEntryCreateCFProperty(
            device, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0
        );

        if (pathRef) {
            char path[PATH_MAX];
            if (CFStringGetCString(pathRef, path, sizeof(path), kCFStringEncodingUTF8)) {
                ports.push_back(path);
            }
            CFRelease(pathRef);
        }

        IOObjectRelease(device);
    }

    IOObjectRelease(iterator);
    return ports;
}

#else
// Linux port enumeration

std::vector<std::string> SerialPort::availablePorts() {
    std::vector<std::string> ports;

    // Check /dev/ttyUSB* and /dev/ttyACM*
    DIR* dir = opendir("/dev");
    if (!dir) return ports;

    struct dirent* entry;
    while ((entry = readdir(dir))) {
        std::string name = entry->d_name;
        if (name.find("ttyUSB") == 0 || name.find("ttyACM") == 0 || name.find("ttyS") == 0) {
            ports.push_back("/dev/" + name);
        }
    }
    closedir(dir);

#ifdef USE_LIBSERIALPORT
    // If libserialport is available, use it for better enumeration
    struct sp_port** portList;
    if (sp_list_ports(&portList) == SP_OK) {
        ports.clear();
        for (int i = 0; portList[i] != nullptr; i++) {
            ports.push_back(sp_get_port_name(portList[i]));
        }
        sp_free_port_list(portList);
    }
#endif

    return ports;
}

#endif // __APPLE__

#endif // _WIN32

// Common implementations

size_t SerialPort::write(const std::string& str) {
    return write(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

std::string SerialPort::readLine(int timeoutMs) {
    std::string line;
    uint8_t ch;
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        if (read(&ch, 1) == 1) {
            if (ch == '\n') {
                // Remove trailing \r if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                return line;
            }
            line += static_cast<char>(ch);
        }

        if (timeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime
            ).count();
            if (elapsed >= timeoutMs) {
                return line;  // Return partial line on timeout
            }
        }

        // Small sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace serial
} // namespace vivid
