#include <vivid/network/udp_in.h>
#include <iostream>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

namespace vivid::network {

#ifdef _WIN32
static bool g_wsaInitialized = false;
static void initWsa() {
    if (!g_wsaInitialized) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        g_wsaInitialized = true;
    }
}
#endif

UdpIn::UdpIn() {
#ifdef _WIN32
    initWsa();
#endif
    m_writeBuffer.reserve(65535);
    m_readBuffer.reserve(65535);
}

UdpIn::~UdpIn() {
    cleanup();
}

void UdpIn::port(int port) {
    if (m_port != port) {
        m_port = port;
        if (m_listening.load()) {
            stopListening();
            startListening();
        }
    }
}

void UdpIn::bufferSize(int bytes) {
    m_bufferSize = bytes;
    m_writeBuffer.reserve(bytes);
    m_readBuffer.reserve(bytes);
}

std::string UdpIn::asString() const {
    return std::string(m_readBuffer.begin(), m_readBuffer.end());
}

std::vector<float> UdpIn::asFloats() const {
    std::vector<float> result;
    if (m_readBuffer.size() >= sizeof(float)) {
        size_t count = m_readBuffer.size() / sizeof(float);
        result.resize(count);
        std::memcpy(result.data(), m_readBuffer.data(), count * sizeof(float));
    }
    return result;
}

std::vector<int32_t> UdpIn::asInts() const {
    std::vector<int32_t> result;
    if (m_readBuffer.size() >= sizeof(int32_t)) {
        size_t count = m_readBuffer.size() / sizeof(int32_t);
        result.resize(count);
        std::memcpy(result.data(), m_readBuffer.data(), count * sizeof(int32_t));
    }
    return result;
}

void UdpIn::init(Context& ctx) {
    startListening();
}

void UdpIn::process(Context& ctx) {
    // Swap buffers if new data is available
    if (m_hasNewData.load()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::swap(m_readBuffer, m_writeBuffer);
        m_hasNewData.store(false);
    } else {
        // Clear read buffer if no new data
        m_readBuffer.clear();
    }
}

void UdpIn::cleanup() {
    stopListening();
}

void UdpIn::startListening() {
    if (m_listening.load()) return;

    // Create UDP socket
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        std::cerr << "[UdpIn] Failed to create socket" << std::endl;
        return;
    }

    // Set non-blocking mode
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(m_socket, FIONBIO, &mode);
#else
    int flags = fcntl(m_socket, F_GETFL, 0);
    fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
#endif

    // Allow address reuse
    int reuse = 1;
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse));
#endif

    // Bind to port
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(m_port));

    if (bind(m_socket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[UdpIn] Failed to bind to port " << m_port << std::endl;
        closesocket(m_socket);
        m_socket = -1;
        return;
    }

    std::cout << "[UdpIn] Listening on port " << m_port << std::endl;

    // Start receive thread
    m_listening.store(true);
    m_thread = std::thread(&UdpIn::receiveThread, this);
}

void UdpIn::stopListening() {
    if (!m_listening.load()) return;

    m_listening.store(false);

    if (m_socket != -1) {
        closesocket(m_socket);
        m_socket = -1;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void UdpIn::receiveThread() {
    std::vector<uint8_t> buffer(m_bufferSize);
    struct sockaddr_in senderAddr;
    socklen_t senderLen = sizeof(senderAddr);

    while (m_listening.load()) {
        // Poll with timeout to allow clean shutdown
#ifdef _WIN32
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(m_socket, &readSet);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; // 10ms
        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#else
        struct pollfd pfd;
        pfd.fd = m_socket;
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, 10); // 10ms timeout
#endif

        if (ready <= 0) continue;

        // Receive packet
        ssize_t received = recvfrom(
            m_socket,
            reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()),
            0,
            (struct sockaddr*)&senderAddr,
            &senderLen
        );

        if (received > 0) {
            // Store sender info
            char addrStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &senderAddr.sin_addr, addrStr, sizeof(addrStr));

            // Copy to write buffer
            std::lock_guard<std::mutex> lock(m_mutex);
            m_writeBuffer.assign(buffer.begin(), buffer.begin() + received);
            m_senderAddress = addrStr;
            m_senderPort = ntohs(senderAddr.sin_port);
            m_hasNewData.store(true);
        }
    }
}

} // namespace vivid::network
