#pragma once

#include <vivid/serial/serial_out.h>
#include <array>
#include <cstdint>

namespace vivid {
namespace serial {

/// DMX output operator via Enttec DMX USB Pro
/// Inherits from SerialOut and adds DMX-specific functionality
class DMXOut : public SerialOut {
public:
    /// Universe number (1-16)
    Param<int> universe{"universe", 1, 1, 16};

    /// Starting channel offset (1-512)
    Param<int> startChannel{"startChannel", 1, 1, 512};

    DMXOut();

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    std::string name() const override { return "DMXOut"; }

    // Parameter interface
    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    // DMX-specific methods

    /// Set a single DMX channel (1-512)
    void channel(int ch, uint8_t value);

    /// Set multiple consecutive channels
    void channels(int start, const std::vector<uint8_t>& values);

    /// Set RGB fixture (3 channels starting at startCh)
    void rgb(int startCh, uint8_t r, uint8_t g, uint8_t b);

    /// Set RGBW fixture (4 channels starting at startCh)
    void rgbw(int startCh, uint8_t r, uint8_t g, uint8_t b, uint8_t w);

    /// Set all channels to zero (blackout)
    void blackout();

    /// Get current value of a channel
    uint8_t getChannel(int ch) const;

    /// Get the entire DMX buffer (512 channels)
    const std::array<uint8_t, 512>& dmxBuffer() const { return m_dmxBuffer; }

private:
    void sendEnttecFrame();

    std::array<uint8_t, 512> m_dmxBuffer{};
    bool m_dirty = true;

    // Enttec DMX USB Pro protocol constants
    static constexpr uint8_t START_BYTE = 0x7E;
    static constexpr uint8_t END_BYTE = 0xE7;
    static constexpr uint8_t SEND_DMX_LABEL = 6;
};

} // namespace serial
} // namespace vivid
