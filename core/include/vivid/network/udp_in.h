#pragma once

/**
 * @file udp_in.h
 * @brief UDP datagram receiver operator
 *
 * Non-blocking UDP receiver for custom protocols and hardware integration.
 * Data is received in a background thread and made available each frame.
 */

#include <vivid/operator.h>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>

namespace vivid::network {

/**
 * @brief UDP datagram receiver
 *
 * Receives raw UDP packets for custom protocols and hardware integration.
 * Data is received in a background thread and made available each frame.
 *
 * @par Example
 * @code
 * void setup(Context& ctx) {
 *     auto& chain = ctx.chain();
 *     chain.add<UdpIn>("sensor");
 *     chain.get<UdpIn>("sensor").port(5000);
 * }
 *
 * void update(Context& ctx) {
 *     auto& udp = ctx.chain().get<UdpIn>("sensor");
 *     if (udp.hasData()) {
 *         auto& bytes = udp.data();
 *         // Process bytes...
 *     }
 * }
 * @endcode
 *
 * @par Inputs
 * None (network receiver)
 *
 * @par Output
 * None (query data via hasData(), data(), etc.)
 */
class UdpIn : public Operator {
public:
    UdpIn();
    ~UdpIn() override;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /// @brief Set listening port
    void port(int port);

    /// @brief Set receive buffer size in bytes
    void bufferSize(int bytes);

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Query
    /// @{

    /// @brief Check if new data was received this frame
    bool hasData() const { return m_hasNewData; }

    /// @brief Check if receiver is listening
    bool isListening() const { return m_listening.load(); }

    /// @brief Get configured port number
    int getPort() const { return m_port; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Data Access
    /// @{

    /// @brief Get received data (valid until next process() call)
    const std::vector<uint8_t>& data() const { return m_readBuffer; }

    /// @brief Get size of received data in bytes
    size_t size() const { return m_readBuffer.size(); }

    /// @brief Interpret data as UTF-8 string
    std::string asString() const;

    /// @brief Interpret data as array of floats
    std::vector<float> asFloats() const;

    /// @brief Interpret data as array of 32-bit integers
    std::vector<int32_t> asInts() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Sender Info
    /// @{

    /// @brief Get IP address of last packet sender
    std::string senderAddress() const { return m_senderAddress; }

    /// @brief Get port of last packet sender
    int senderPort() const { return m_senderPort; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "UdpIn"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    /// @}

private:
    void startListening();
    void stopListening();
    void receiveThread();

    int m_port = 5000;
    int m_bufferSize = 65535;

    // Socket
    int m_socket = -1;
    std::atomic<bool> m_listening{false};
    std::thread m_thread;

    // Double-buffered data
    std::vector<uint8_t> m_writeBuffer;
    std::vector<uint8_t> m_readBuffer;
    std::mutex m_mutex;
    std::atomic<bool> m_hasNewData{false};

    // Sender info
    std::string m_senderAddress;
    int m_senderPort = 0;
};

} // namespace vivid::network
