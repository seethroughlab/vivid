#pragma once

/**
 * @file osc_out.h
 * @brief OSC (Open Sound Control) sender operator
 *
 * Send OSC messages to audio/visual software, hardware, or other apps.
 */

#include <vivid/operator.h>
#include <string>
#include <vector>
#include <cstdint>

namespace vivid::network {

/**
 * @brief OSC (Open Sound Control) sender
 *
 * Sends OSC messages to external software, hardware controllers, or other apps.
 * Supports single and multi-argument messages with float, int, and string types.
 *
 * @par Example
 * @code
 * void setup(Context& ctx) {
 *     auto& chain = ctx.chain();
 *     chain.add<OscOut>("osc");
 *     auto& osc = chain.get<OscOut>("osc");
 *     osc.host("127.0.0.1");
 *     osc.port(9000);
 * }
 *
 * void update(Context& ctx) {
 *     auto& osc = ctx.chain().get<OscOut>("osc");
 *
 *     // Send single float
 *     osc.send("/fader/1", 0.75f);
 *
 *     // Send multiple args
 *     osc.send("/xy", 0.5f, 0.3f);
 *
 *     // Send int
 *     osc.send("/button/1", 1);
 * }
 * @endcode
 *
 * @par Inputs
 * None (network sender)
 *
 * @par Output
 * None (sends messages via UDP)
 */
class OscOut : public Operator {
public:
    OscOut();
    ~OscOut() override;

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

    /// @brief Send message with no arguments
    void send(const std::string& address);

    /// @brief Send message with single float
    void send(const std::string& address, float value);

    /// @brief Send message with single int
    void send(const std::string& address, int32_t value);

    /// @brief Send message with single string
    void send(const std::string& address, const std::string& str);

    /// @brief Send message with two floats
    void send(const std::string& address, float v1, float v2);

    /// @brief Send message with three floats
    void send(const std::string& address, float v1, float v2, float v3);

    /// @brief Send message with four floats
    void send(const std::string& address, float v1, float v2, float v3, float v4);

    /// @brief Send raw OSC packet data
    void sendRaw(const void* data, size_t size);

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

    /// @brief Get total messages sent
    size_t messagesSent() const { return m_messagesSent; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "OscOut"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    /// @}

private:
    void createSocket();
    void destroySocket();

    // Build OSC message
    std::vector<uint8_t> buildMessage(const std::string& address,
                                      const std::string& typeTags,
                                      const std::vector<uint8_t>& argData);

    std::string m_host = "127.0.0.1";
    int m_port = 9000;
    bool m_broadcast = false;

    int m_socket = -1;
    size_t m_messagesSent = 0;
};

} // namespace vivid::network
