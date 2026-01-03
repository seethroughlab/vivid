#include <vivid/network/osc_in.h>
#include <iostream>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    typedef int ssize_t;  // Windows uses int for socket returns
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

// =============================================================================
// OscMessage helpers
// =============================================================================

int32_t OscMessage::intArg(size_t index) const {
    if (index >= args.size()) return 0;
    if (auto* v = std::get_if<int32_t>(&args[index])) return *v;
    if (auto* v = std::get_if<float>(&args[index])) return static_cast<int32_t>(*v);
    return 0;
}

float OscMessage::floatArg(size_t index) const {
    if (index >= args.size()) return 0.0f;
    if (auto* v = std::get_if<float>(&args[index])) return *v;
    if (auto* v = std::get_if<int32_t>(&args[index])) return static_cast<float>(*v);
    return 0.0f;
}

std::string OscMessage::stringArg(size_t index) const {
    if (index >= args.size()) return "";
    if (auto* v = std::get_if<std::string>(&args[index])) return *v;
    return "";
}

// =============================================================================
// OscIn
// =============================================================================

#ifdef _WIN32
extern bool g_wsaInitialized;
extern void initWsa();
#endif

OscIn::OscIn() {
#ifdef _WIN32
    initWsa();
#endif
}

OscIn::~OscIn() {
    cleanup();
}

void OscIn::port(int port) {
    if (m_port != port) {
        m_port = port;
        if (m_listening.load()) {
            stopListening();
            startListening();
        }
    }
}

void OscIn::bufferSize(int bytes) {
    m_bufferSize = bytes;
}

bool OscIn::hasMessage(const std::string& address) const {
    for (const auto& msg : m_readMessages) {
        if (matchPattern(address, msg.address)) return true;
    }
    return false;
}

float OscIn::getFloat(const std::string& address, float defaultVal) const {
    // Check current frame messages first
    for (const auto& msg : m_readMessages) {
        if (matchPattern(address, msg.address) && msg.argCount() > 0) {
            return msg.floatArg(0);
        }
    }
    // Fall back to cached latest
    auto it = m_latestByAddress.find(address);
    if (it != m_latestByAddress.end() && it->second.argCount() > 0) {
        return it->second.floatArg(0);
    }
    return defaultVal;
}

int32_t OscIn::getInt(const std::string& address, int32_t defaultVal) const {
    for (const auto& msg : m_readMessages) {
        if (matchPattern(address, msg.address) && msg.argCount() > 0) {
            return msg.intArg(0);
        }
    }
    auto it = m_latestByAddress.find(address);
    if (it != m_latestByAddress.end() && it->second.argCount() > 0) {
        return it->second.intArg(0);
    }
    return defaultVal;
}

std::vector<OscMessage> OscIn::getMessages(const std::string& pattern) const {
    std::vector<OscMessage> result;
    for (const auto& msg : m_readMessages) {
        if (matchPattern(pattern, msg.address)) {
            result.push_back(msg);
        }
    }
    return result;
}

void OscIn::init(Context& ctx) {
    startListening();
}

void OscIn::process(Context& ctx) {
    // Swap buffers if new data is available
    if (m_hasNewData.load()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::swap(m_readMessages, m_writeMessages);
        m_writeMessages.clear();
        m_hasNewData.store(false);

        // Update cache
        for (const auto& msg : m_readMessages) {
            m_latestByAddress[msg.address] = msg;
        }
    } else {
        m_readMessages.clear();
    }
}

void OscIn::cleanup() {
    stopListening();
}

void OscIn::startListening() {
    if (m_listening.load()) return;

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        std::cerr << "[OscIn] Failed to create socket" << std::endl;
        return;
    }

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(m_socket, FIONBIO, &mode);
#else
    int flags = fcntl(m_socket, F_GETFL, 0);
    fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
#endif

    int reuse = 1;
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse));
#endif

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(m_port));

    if (bind(m_socket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[OscIn] Failed to bind to port " << m_port << std::endl;
        closesocket(m_socket);
        m_socket = -1;
        return;
    }

    std::cout << "[OscIn] Listening on port " << m_port << std::endl;

    m_listening.store(true);
    m_thread = std::thread(&OscIn::receiveThread, this);
}

void OscIn::stopListening() {
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

void OscIn::receiveThread() {
    std::vector<uint8_t> buffer(m_bufferSize);

    while (m_listening.load()) {
#ifdef _WIN32
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(m_socket, &readSet);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000;
        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#else
        struct pollfd pfd;
        pfd.fd = m_socket;
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, 10);
#endif

        if (ready <= 0) continue;

        ssize_t received = recvfrom(m_socket, reinterpret_cast<char*>(buffer.data()),
                                    static_cast<int>(buffer.size()), 0, nullptr, nullptr);

        if (received > 0) {
            std::vector<OscMessage> parsed;
            if (parseOscPacket(buffer.data(), received, parsed)) {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& msg : parsed) {
                    m_writeMessages.push_back(std::move(msg));
                }
                m_hasNewData.store(true);
            }
        }
    }
}

// =============================================================================
// OSC Parsing
// =============================================================================

// Helper to read 4-byte big-endian int
static int32_t readInt32BE(const uint8_t* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

// Helper to read 4-byte big-endian float
static float readFloatBE(const uint8_t* data) {
    int32_t i = readInt32BE(data);
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}

std::string OscIn::readOscString(const uint8_t* data, size_t maxSize, size_t& bytesRead) {
    std::string result;
    size_t i = 0;
    while (i < maxSize && data[i] != 0) {
        result += static_cast<char>(data[i]);
        i++;
    }
    // OSC strings are null-terminated and padded to 4-byte boundary
    bytesRead = ((i + 4) / 4) * 4;
    return result;
}

bool OscIn::parseOscPacket(const uint8_t* data, size_t size, std::vector<OscMessage>& out) {
    if (size < 4) return false;

    // Check if it's a bundle (starts with "#bundle")
    if (data[0] == '#') {
        return parseOscBundle(data, size, out);
    }

    // Otherwise it's a single message
    OscMessage msg;
    if (parseOscMessage(data, size, msg)) {
        out.push_back(std::move(msg));
        return true;
    }
    return false;
}

bool OscIn::parseOscMessage(const uint8_t* data, size_t size, OscMessage& out) {
    if (size < 4) return false;

    size_t offset = 0;

    // Read address pattern
    size_t addrBytes;
    out.address = readOscString(data, size, addrBytes);
    if (out.address.empty() || out.address[0] != '/') return false;
    offset += addrBytes;

    if (offset >= size) return true; // No type tag (valid but uncommon)

    // Read type tag string
    size_t typeBytes;
    std::string typeTags = readOscString(data + offset, size - offset, typeBytes);
    offset += typeBytes;

    if (typeTags.empty() || typeTags[0] != ',') return true; // No args

    // Parse arguments based on type tags
    for (size_t i = 1; i < typeTags.size() && offset < size; i++) {
        char type = typeTags[i];

        switch (type) {
            case 'i': // int32
                if (offset + 4 <= size) {
                    out.args.push_back(readInt32BE(data + offset));
                    offset += 4;
                }
                break;

            case 'f': // float32
                if (offset + 4 <= size) {
                    out.args.push_back(readFloatBE(data + offset));
                    offset += 4;
                }
                break;

            case 's': // string
            case 'S': // symbol (treat as string)
            {
                size_t strBytes;
                std::string str = readOscString(data + offset, size - offset, strBytes);
                out.args.push_back(std::move(str));
                offset += strBytes;
                break;
            }

            case 'b': // blob
                if (offset + 4 <= size) {
                    int32_t blobSize = readInt32BE(data + offset);
                    offset += 4;
                    if (offset + blobSize <= size) {
                        std::vector<uint8_t> blob(data + offset, data + offset + blobSize);
                        out.args.push_back(std::move(blob));
                        offset += ((blobSize + 3) / 4) * 4; // Pad to 4 bytes
                    }
                }
                break;

            case 'T': // True
                out.args.push_back(1);
                break;

            case 'F': // False
                out.args.push_back(0);
                break;

            case 'N': // Nil
            case 'I': // Infinitum
                // No data
                break;

            default:
                // Unknown type, skip
                break;
        }
    }

    return true;
}

bool OscIn::parseOscBundle(const uint8_t* data, size_t size, std::vector<OscMessage>& out) {
    if (size < 16) return false;

    // Verify "#bundle" header
    if (std::memcmp(data, "#bundle", 7) != 0) return false;

    // Skip header (8 bytes) + timetag (8 bytes)
    size_t offset = 16;

    // Parse bundle elements
    while (offset + 4 <= size) {
        int32_t elementSize = readInt32BE(data + offset);
        offset += 4;

        if (elementSize <= 0 || offset + elementSize > size) break;

        // Recursively parse element (could be message or nested bundle)
        parseOscPacket(data + offset, elementSize, out);
        offset += elementSize;
    }

    return true;
}

bool OscIn::matchPattern(const std::string& pattern, const std::string& address) {
    // Simple matching: exact match or wildcard '*' at end
    if (pattern == address) return true;

    // Wildcard at end: "/foo/*" matches "/foo/bar"
    if (pattern.size() > 1 && pattern.back() == '*') {
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        return address.compare(0, prefix.size(), prefix) == 0;
    }

    return false;
}

bool OscIn::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    float w = maxX - minX;
    float h = maxY - minY;
    float cx = minX + w * 0.5f;
    float cy = minY + h * 0.5f;
    float r = std::min(w, h) * 0.35f;

    // Background circle
    uint32_t bgColor = m_listening.load() ? VIZ_COL32(30, 80, 30, 255) : VIZ_COL32(60, 30, 30, 255);
    dl->AddCircleFilled(VizVec2(cx, cy), r, bgColor);
    dl->AddCircle(VizVec2(cx, cy), r, VIZ_COL32(100, 100, 100, 255), 32, 2.0f);

    // RX indicator with activity flash
    bool hasActivity = !m_readMessages.empty();
    uint32_t textColor = hasActivity ? VIZ_COL32(100, 255, 100, 255) : VIZ_COL32(180, 180, 180, 255);

    const char* label = "RX";
    VizTextSize textSize = dl->CalcTextSize(label);
    dl->AddText(VizVec2(cx - textSize.x * 0.5f, cy - textSize.y * 0.5f - r * 0.15f), textColor, label);

    // Port number below
    char portStr[16];
    snprintf(portStr, sizeof(portStr), ":%d", m_port);
    VizTextSize portSize = dl->CalcTextSize(portStr);
    dl->AddText(VizVec2(cx - portSize.x * 0.5f, cy + r * 0.15f), VIZ_COL32(150, 150, 150, 255), portStr);

    // Activity indicator dot
    if (hasActivity) {
        float dotR = r * 0.15f;
        dl->AddCircleFilled(VizVec2(cx + r * 0.6f, cy - r * 0.6f), dotR, VIZ_COL32(100, 255, 100, 255));
    }

    return true;
}

} // namespace vivid::network
