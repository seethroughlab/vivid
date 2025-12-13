#include <vivid/network/osc_out.h>
#include <iostream>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

namespace vivid::network {

#ifdef _WIN32
extern bool g_wsaInitialized;
extern void initWsa();
#endif

// Helper to write 4-byte big-endian int
static void writeInt32BE(std::vector<uint8_t>& out, int32_t value) {
    out.push_back((value >> 24) & 0xFF);
    out.push_back((value >> 16) & 0xFF);
    out.push_back((value >> 8) & 0xFF);
    out.push_back(value & 0xFF);
}

// Helper to write 4-byte big-endian float
static void writeFloatBE(std::vector<uint8_t>& out, float value) {
    int32_t i;
    std::memcpy(&i, &value, 4);
    writeInt32BE(out, i);
}

// Helper to write OSC string (null-terminated, padded to 4 bytes)
static void writeOscString(std::vector<uint8_t>& out, const std::string& str) {
    for (char c : str) {
        out.push_back(static_cast<uint8_t>(c));
    }
    out.push_back(0); // Null terminator

    // Pad to 4-byte boundary
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
}

OscOut::OscOut() {
#ifdef _WIN32
    initWsa();
#endif
}

OscOut::~OscOut() {
    cleanup();
}

OscOut& OscOut::host(const std::string& hostname) {
    if (m_host != hostname) {
        m_host = hostname;
        if (m_socket != -1) {
            destroySocket();
            createSocket();
        }
    }
    return *this;
}

OscOut& OscOut::port(int port) {
    if (m_port != port) {
        m_port = port;
        if (m_socket != -1) {
            destroySocket();
            createSocket();
        }
    }
    return *this;
}

OscOut& OscOut::broadcast(bool enabled) {
    m_broadcast = enabled;
    if (m_socket != -1) {
        int opt = enabled ? 1 : 0;
        setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof(opt));
    }
    return *this;
}

void OscOut::send(const std::string& address) {
    std::vector<uint8_t> argData;
    auto msg = buildMessage(address, ",", argData);
    sendRaw(msg.data(), msg.size());
}

void OscOut::send(const std::string& address, float value) {
    std::vector<uint8_t> argData;
    writeFloatBE(argData, value);
    auto msg = buildMessage(address, ",f", argData);
    sendRaw(msg.data(), msg.size());
}

void OscOut::send(const std::string& address, int32_t value) {
    std::vector<uint8_t> argData;
    writeInt32BE(argData, value);
    auto msg = buildMessage(address, ",i", argData);
    sendRaw(msg.data(), msg.size());
}

void OscOut::send(const std::string& address, const std::string& str) {
    std::vector<uint8_t> argData;
    writeOscString(argData, str);
    auto msg = buildMessage(address, ",s", argData);
    sendRaw(msg.data(), msg.size());
}

void OscOut::send(const std::string& address, float v1, float v2) {
    std::vector<uint8_t> argData;
    writeFloatBE(argData, v1);
    writeFloatBE(argData, v2);
    auto msg = buildMessage(address, ",ff", argData);
    sendRaw(msg.data(), msg.size());
}

void OscOut::send(const std::string& address, float v1, float v2, float v3) {
    std::vector<uint8_t> argData;
    writeFloatBE(argData, v1);
    writeFloatBE(argData, v2);
    writeFloatBE(argData, v3);
    auto msg = buildMessage(address, ",fff", argData);
    sendRaw(msg.data(), msg.size());
}

void OscOut::send(const std::string& address, float v1, float v2, float v3, float v4) {
    std::vector<uint8_t> argData;
    writeFloatBE(argData, v1);
    writeFloatBE(argData, v2);
    writeFloatBE(argData, v3);
    writeFloatBE(argData, v4);
    auto msg = buildMessage(address, ",ffff", argData);
    sendRaw(msg.data(), msg.size());
}

void OscOut::sendRaw(const void* data, size_t size) {
    if (m_socket == -1) {
        createSocket();
        if (m_socket == -1) return;
    }

    struct sockaddr_in destAddr;
    std::memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(static_cast<uint16_t>(m_port));

    if (inet_pton(AF_INET, m_host.c_str(), &destAddr.sin_addr) != 1) {
        struct hostent* he = gethostbyname(m_host.c_str());
        if (he == nullptr) {
            std::cerr << "[OscOut] Failed to resolve host: " << m_host << std::endl;
            return;
        }
        std::memcpy(&destAddr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    ssize_t sent = sendto(
        m_socket,
        reinterpret_cast<const char*>(data),
        static_cast<int>(size),
        0,
        (struct sockaddr*)&destAddr,
        sizeof(destAddr)
    );

    if (sent > 0) {
        m_messagesSent++;
    }
}

std::vector<uint8_t> OscOut::buildMessage(const std::string& address,
                                          const std::string& typeTags,
                                          const std::vector<uint8_t>& argData) {
    std::vector<uint8_t> msg;

    // Address pattern
    writeOscString(msg, address);

    // Type tag string
    writeOscString(msg, typeTags);

    // Arguments
    msg.insert(msg.end(), argData.begin(), argData.end());

    return msg;
}

void OscOut::init(Context& ctx) {
    createSocket();
}

void OscOut::process(Context& ctx) {
    // Nothing to do each frame - sends happen on demand
}

void OscOut::cleanup() {
    destroySocket();
}

void OscOut::createSocket() {
    if (m_socket != -1) return;

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        std::cerr << "[OscOut] Failed to create socket" << std::endl;
        m_socket = -1;
        return;
    }

    if (m_broadcast) {
        int opt = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof(opt));
    }

    std::cout << "[OscOut] Ready to send to " << m_host << ":" << m_port << std::endl;
}

void OscOut::destroySocket() {
    if (m_socket != -1) {
        closesocket(m_socket);
        m_socket = -1;
    }
}

} // namespace vivid::network
