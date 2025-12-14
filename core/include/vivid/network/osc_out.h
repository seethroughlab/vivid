// OscOut - Send OSC (Open Sound Control) messages
//
// Send OSC messages to audio/visual software, hardware, or other apps.
//
// Usage:
//   chain.add<OscOut>("osc").host("127.0.0.1").port(9000);
//
//   void update(Context& ctx) {
//       auto& osc = chain.get<OscOut>("osc");
//
//       // Send single float
//       osc.send("/fader/1", 0.75f);
//
//       // Send multiple args
//       osc.send("/xy", 0.5f, 0.3f);
//
//       // Send int
//       osc.send("/button/1", 1);
//   }

#pragma once

#include <vivid/operator.h>
#include <string>
#include <vector>
#include <cstdint>

namespace vivid::network {

class OscOut : public Operator {
public:
    OscOut();
    ~OscOut() override;

    // Configuration
    void host(const std::string& hostname);
    void port(int port);
    void broadcast(bool enabled);

    // Send methods - single value
    void send(const std::string& address);                          // No args
    void send(const std::string& address, float value);             // Single float
    void send(const std::string& address, int32_t value);           // Single int
    void send(const std::string& address, const std::string& str);  // Single string

    // Send methods - multiple args (up to 4 floats)
    void send(const std::string& address, float v1, float v2);
    void send(const std::string& address, float v1, float v2, float v3);
    void send(const std::string& address, float v1, float v2, float v3, float v4);

    // Send raw OSC message
    void sendRaw(const void* data, size_t size);

    // Query state
    bool isReady() const { return m_socket != -1; }
    std::string getHost() const { return m_host; }
    int getPort() const { return m_port; }

    // Statistics
    size_t messagesSent() const { return m_messagesSent; }

    // Operator interface
    std::string name() const override { return "OscOut"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

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
