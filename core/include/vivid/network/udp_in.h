// UdpIn - Receive UDP datagrams
//
// Non-blocking UDP receiver for custom protocols and hardware integration.
// Data is received in a background thread and made available each frame.
//
// Usage:
//   chain.add<UdpIn>("sensor").port(5000);
//
//   void update(Context& ctx) {
//       auto& udp = chain.get<UdpIn>("sensor");
//       if (udp.hasData()) {
//           auto& bytes = udp.data();
//           // Process bytes...
//       }
//   }

#pragma once

#include <vivid/operator.h>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>

namespace vivid::network {

class UdpIn : public Operator {
public:
    UdpIn();
    ~UdpIn() override;

    // Configuration
    UdpIn& port(int port);
    UdpIn& bufferSize(int bytes);

    // Query state
    bool hasData() const { return m_hasNewData; }
    bool isListening() const { return m_listening.load(); }
    int getPort() const { return m_port; }

    // Access received data (valid until next process() call)
    const std::vector<uint8_t>& data() const { return m_readBuffer; }
    size_t size() const { return m_readBuffer.size(); }

    // Convenience accessors
    std::string asString() const;
    std::vector<float> asFloats() const;
    std::vector<int32_t> asInts() const;

    // Get sender info from last packet
    std::string senderAddress() const { return m_senderAddress; }
    int senderPort() const { return m_senderPort; }

    // Operator interface
    std::string name() const override { return "UdpIn"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

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
