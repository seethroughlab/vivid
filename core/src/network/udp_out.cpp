#include <vivid/network/udp_out.h>
#include <iostream>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int ssize_t;  // Windows uses int for socket returns
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

UdpOut::UdpOut() {
#ifdef _WIN32
    initWsa();
#endif
}

UdpOut::~UdpOut() {
    cleanup();
}

void UdpOut::host(const std::string& hostname) {
    if (m_host != hostname) {
        m_host = hostname;
        if (m_socket != -1) {
            destroySocket();
            createSocket();
        }
    }
}

void UdpOut::port(int port) {
    if (m_port != port) {
        m_port = port;
        if (m_socket != -1) {
            destroySocket();
            createSocket();
        }
    }
}

void UdpOut::broadcast(bool enabled) {
    m_broadcast = enabled;
    if (m_socket != -1) {
        int opt = enabled ? 1 : 0;
        setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof(opt));
    }
}

void UdpOut::send(const void* data, size_t size) {
    if (m_socket == -1) {
        createSocket();
        if (m_socket == -1) return;
    }

    // Resolve hostname
    struct sockaddr_in destAddr;
    std::memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(static_cast<uint16_t>(m_port));

    // Try as IP address first
    if (inet_pton(AF_INET, m_host.c_str(), &destAddr.sin_addr) != 1) {
        // Resolve hostname
        struct hostent* he = gethostbyname(m_host.c_str());
        if (he == nullptr) {
            std::cerr << "[UdpOut] Failed to resolve host: " << m_host << std::endl;
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
        m_packetsSent++;
        m_bytesSent += sent;
    } else {
        std::cerr << "[UdpOut] Send failed to " << m_host << ":" << m_port << std::endl;
    }
}

void UdpOut::send(const std::string& message) {
    send(message.data(), message.size());
}

void UdpOut::send(const std::vector<uint8_t>& bytes) {
    send(bytes.data(), bytes.size());
}

void UdpOut::send(const std::vector<float>& floats) {
    send(floats.data(), floats.size() * sizeof(float));
}

void UdpOut::send(const std::vector<int32_t>& ints) {
    send(ints.data(), ints.size() * sizeof(int32_t));
}

void UdpOut::init(Context& ctx) {
    createSocket();
}

void UdpOut::process(Context& ctx) {
    // Nothing to do each frame - sends happen on demand
}

void UdpOut::cleanup() {
    destroySocket();
}

void UdpOut::createSocket() {
    if (m_socket != -1) return;

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        std::cerr << "[UdpOut] Failed to create socket" << std::endl;
        m_socket = -1;
        return;
    }

    // Enable broadcast if requested
    if (m_broadcast) {
        int opt = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof(opt));
    }

    std::cout << "[UdpOut] Ready to send to " << m_host << ":" << m_port << std::endl;
}

void UdpOut::destroySocket() {
    if (m_socket != -1) {
        closesocket(m_socket);
        m_socket = -1;
    }
}

} // namespace vivid::network
