#pragma once

#include <vivid/serial/serial_out.h>
#include <vivid/operator_registry.h>
#include <array>
#include <cstdint>

namespace vivid {
namespace serial {

/**
 * @brief Control DMX lighting fixtures via Enttec DMX USB Pro
 *
 * Sends DMX512 data to control stage lighting, LED fixtures, fog machines,
 * and other DMX-compatible hardware. Requires an Enttec DMX USB Pro or
 * compatible adapter connected via USB serial.
 *
 * @par Example
 * @code
 * auto& dmx = chain.add<DMXOut>("dmx");
 * dmx.port("/dev/tty.usbserial-EN123456");  // Enttec device
 *
 * // Set RGB fixture starting at channel 1
 * dmx.rgb(1, 255, 0, 127);  // Purple
 *
 * // Control individual channels
 * dmx.channel(10, 200);  // Dimmer at channel 10
 *
 * // Audio-reactive lighting
 * float bass = fft.band(0);
 * dmx.channel(1, static_cast<uint8_t>(bass * 255));
 * @endcode
 *
 * @see SerialOut, OscOut, MidiOut
 */
class DMXOut : public SerialOut {
public:
    // -------------------------------------------------------------------------
    /// @name Self-Description
    /// @{

    static OperatorDescriptor describe() {
        return OperatorDescriptor("DMXOut", "IO", "DMX lighting output via Enttec USB Pro")
            .output(OutputKind::Value)
            .inModule("vivid-serial")
            .withAliases({"DMX", "Lighting", "Enttec"})
            .withUsage(
                "auto& dmx = chain.add<DMXOut>(\"dmx\");\n"
                "dmx.port(\"/dev/tty.usbserial-EN123456\");  // Enttec device\n"
                "\n"
                "// Set RGB fixture starting at channel 1\n"
                "dmx.rgb(1, 255, 0, 127);  // Purple\n"
                "\n"
                "// Audio-reactive lighting\n"
                "float bass = fft.band(0);\n"
                "dmx.channel(1, static_cast<uint8_t>(bass * 255));\n"
            );
    }

    /// @}

    /// Universe number (1-16)
    Param<int> universe{"universe", 1, 1, 16};

    /// Starting channel offset (1-512)
    Param<int> startChannel{"startChannel", 1, 1, 512};

    DMXOut();

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    std::string name() const override { return "DMXOut"; }
    bool drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) override;

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
