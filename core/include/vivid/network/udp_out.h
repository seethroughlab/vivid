#pragma once

/**
 * @file udp_out.h
 * @brief UDP datagram sender operator
 *
 * Send raw UDP packets to a target host:port for hardware control,
 * lighting systems (Artnet), or inter-application communication.
 */

#include <vivid/operator.h>
#include <vector>
#include <string>

namespace vivid::network {

/**
 * @brief UDP datagram sender
 *
 * Sends raw UDP packets for hardware control, lighting systems (Artnet),
 * or inter-application communication.
 *
 * @par Example
 * @code
 * void setup(Context& ctx) {
 *     auto& chain = ctx.chain();
 *     chain.add<UdpOut>("lights");
 *     auto& udp = chain.get<UdpOut>("lights");
 *     udp.host("192.168.1.100");
 *     udp.port(6454);
 * }
 *
 * void update(Context& ctx) {
 *     auto& udp = ctx.chain().get<UdpOut>("lights");
 *     udp.send(dmxData.data(), dmxData.size());
 * }
 * @endcode
 *
 * @par Inputs
 * None (network sender)
 *
 * @par Output
 * None (sends data via UDP)
 */
class UdpOut : public Operator {
public:
    UdpOut();
    ~UdpOut() override;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /// @brief Set destination hostname or IP address
    void host(const std::string& hostname);

    /// @brief Set destination port
    void port(int port);

    /// @brief Enable broadcast mode (send to all devices on network)
    void broadcast(bool enabled);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Send Methods
    /// @{

    /// @brief Send raw binary data
    void send(const void* data, size_t size);

    /// @brief Send string message
    void send(const std::string& message);

    /// @brief Send byte vector
    void send(const std::vector<uint8_t>& bytes);

    /// @brief Send floats as binary data
    void send(const std::vector<float>& floats);

    /// @brief Send integers as binary data
    void send(const std::vector<int32_t>& ints);

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Query
    /// @{

    /// @brief Check if socket is ready to send
    bool isReady() const { return m_socket != -1; }

    /// @brief Get configured hostname
    std::string getHost() const { return m_host; }

    /// @brief Get configured port
    int getPort() const { return m_port; }

    /// @brief Get total packets sent
    size_t packetsSent() const { return m_packetsSent; }

    /// @brief Get total bytes sent
    size_t bytesSent() const { return m_bytesSent; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "UdpOut"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    /// @}

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
