// UdpOut - Send UDP datagrams
//
// Send raw UDP packets to a target host:port for hardware control,
// lighting systems (Artnet), or inter-application communication.
//
// Usage:
//   chain.add<UdpOut>("lights").host("192.168.1.100").port(6454);
//
//   void update(Context& ctx) {
//       auto& udp = chain.get<UdpOut>("lights");
//       udp.send(dmxData.data(), dmxData.size());
//   }

#pragma once

#include <vivid/operator.h>
#include <vector>
#include <string>

namespace vivid::network {

class UdpOut : public Operator {
public:
    UdpOut();
    ~UdpOut() override;

    // Configuration
    void host(const std::string& hostname);
    void port(int port);
    void broadcast(bool enabled);  // Enable broadcast mode

    // Send methods
    void send(const void* data, size_t size);
    void send(const std::string& message);
    void send(const std::vector<uint8_t>& bytes);
    void send(const std::vector<float>& floats);
    void send(const std::vector<int32_t>& ints);

    // Query state
    bool isReady() const { return m_socket != -1; }
    std::string getHost() const { return m_host; }
    int getPort() const { return m_port; }

    // Statistics
    size_t packetsSent() const { return m_packetsSent; }
    size_t bytesSent() const { return m_bytesSent; }

    // Operator interface
    std::string name() const override { return "UdpOut"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

private:
    void createSocket();
    void destroySocket();

    std::string m_host = "127.0.0.1";
    int m_port = 5000;
    bool m_broadcast = false;

    int m_socket = -1;

    // Statistics
    size_t m_packetsSent = 0;
    size_t m_bytesSent = 0;
};

} // namespace vivid::network
