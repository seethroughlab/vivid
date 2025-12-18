#pragma once

/**
 * @file osc_in.h
 * @brief OSC (Open Sound Control) receiver operator
 *
 * OSC is a protocol for communication between audio/visual software,
 * hardware controllers (TouchOSC, Lemur), and creative coding environments.
 */

#include <vivid/operator.h>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <variant>

namespace vivid::network {

/// @brief Single OSC argument (int, float, string, or blob)
using OscArg = std::variant<int32_t, float, std::string, std::vector<uint8_t>>;

/**
 * @brief Parsed OSC message with address and arguments
 */
struct OscMessage {
    std::string address;           ///< Address pattern (e.g., "/control/fader1")
    std::vector<OscArg> args;      ///< Message arguments

    /// @brief Get argument as int (returns 0 if wrong type or missing)
    int32_t intArg(size_t index) const;

    /// @brief Get argument as float (returns 0.0f if wrong type or missing)
    float floatArg(size_t index) const;

    /// @brief Get argument as string (returns "" if wrong type or missing)
    std::string stringArg(size_t index) const;

    /// @brief Get argument count
    size_t argCount() const { return args.size(); }
};

/**
 * @brief OSC (Open Sound Control) receiver
 *
 * Receives OSC messages from external software and hardware controllers.
 * Messages are received in a background thread and made available each frame.
 *
 * @par Example
 * @code
 * void setup(Context& ctx) {
 *     auto& chain = ctx.chain();
 *     chain.add<OscIn>("osc");
 *     chain.get<OscIn>("osc").port(8000);
 * }
 *
 * void update(Context& ctx) {
 *     auto& osc = ctx.chain().get<OscIn>("osc");
 *
 *     // Check for specific address
 *     if (osc.hasMessage("/fader/1")) {
 *         float value = osc.getFloat("/fader/1");
 *     }
 *
 *     // Iterate all messages this frame
 *     for (const auto& msg : osc.messages()) {
 *         std::cout << msg.address << " = " << msg.floatArg(0) << std::endl;
 *     }
 * }
 * @endcode
 *
 * @par Inputs
 * None (network receiver)
 *
 * @par Output
 * None (query messages via hasMessage(), getFloat(), etc.)
 */
class OscIn : public Operator {
public:
    OscIn();
    ~OscIn() override;

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

    /// @brief Check if receiver is listening
    bool isListening() const { return m_listening.load(); }

    /// @brief Get configured port number
    int getPort() const { return m_port; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Message Access
    /// @{

    /// @brief Check if a message was received for an address this frame
    bool hasMessage(const std::string& address) const;

    /// @brief Get latest float value for an address
    float getFloat(const std::string& address, float defaultVal = 0.0f) const;

    /// @brief Get latest int value for an address
    int32_t getInt(const std::string& address, int32_t defaultVal = 0) const;

    /// @brief Get all messages matching a pattern (supports wildcards)
    std::vector<OscMessage> getMessages(const std::string& pattern) const;

    /// @brief Get all messages received this frame
    const std::vector<OscMessage>& messages() const { return m_readMessages; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "OscIn"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    /// @}

private:
    void startListening();
    void stopListening();
    void receiveThread();

    // OSC parsing
    static bool parseOscPacket(const uint8_t* data, size_t size, std::vector<OscMessage>& out);
    static bool parseOscMessage(const uint8_t* data, size_t size, OscMessage& out);
    static bool parseOscBundle(const uint8_t* data, size_t size, std::vector<OscMessage>& out);
    static std::string readOscString(const uint8_t* data, size_t maxSize, size_t& bytesRead);
    static bool matchPattern(const std::string& pattern, const std::string& address);

    int m_port = 8000;
    int m_bufferSize = 65535;

    // Socket
    int m_socket = -1;
    std::atomic<bool> m_listening{false};
    std::thread m_thread;

    // Double-buffered messages
    std::vector<OscMessage> m_writeMessages;
    std::vector<OscMessage> m_readMessages;
    std::mutex m_mutex;
    std::atomic<bool> m_hasNewData{false};

    // Cache of latest values by address (for convenience getFloat/getInt)
    std::map<std::string, OscMessage> m_latestByAddress;
};

} // namespace vivid::network
