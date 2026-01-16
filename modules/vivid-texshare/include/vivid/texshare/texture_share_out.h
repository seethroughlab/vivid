#pragma once

/**
 * @file texture_share_out.h
 * @brief Texture output operator for sharing with other applications
 *
 * Uses Syphon on macOS, Spout on Windows to share textures
 * with other applications (VJ software, media servers, etc.)
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/operator_registry.h>
#include <vivid/texshare/export.h>
#include <memory>
#include <string>

namespace vivid::texshare {

class ShareBackend;  // Forward declaration

/**
 * @brief Share textures with other applications
 *
 * TextureShareOut publishes its input texture to a named server that
 * other applications can connect to. On macOS it uses Syphon, on
 * Windows it uses Spout.
 *
 * This is a pass-through operator - it forwards its input to output
 * unchanged while also sharing it externally.
 *
 * Example:
 * @code
 * auto& noise = chain.add<Noise>("noise");
 *
 * auto& share = chain.add<TextureShareOut>("share");
 * share.input("noise");
 * share.serverName = "My Vivid Output";
 *
 * chain.output("share");  // Pass through to display
 * @endcode
 *
 * @par Platform Support
 * - macOS: Syphon - works with Resolume, VDMX, MadMapper, etc.
 * - Windows: Spout - works with Resolume, TouchDesigner, etc.
 * - Linux: No-op (logs warning)
 */
class VIVID_TEXSHARE_API TextureShareOut : public vivid::effects::TextureOperator {
public:
    TextureShareOut();
    ~TextureShareOut() override;

    // Non-copyable
    TextureShareOut(const TextureShareOut&) = delete;
    TextureShareOut& operator=(const TextureShareOut&) = delete;

    /// @brief Server name visible to other applications (default: "Vivid")
    std::string serverName = "Vivid";

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "TextureShareOut"; }

    /**
     * @brief Pass-through: returns input texture view
     *
     * TextureShareOut doesn't create its own output - it forwards
     * the input texture unchanged.
     */
    WGPUTextureView outputView() const override;

    /**
     * @brief Pass-through: returns input texture
     */
    WGPUTexture outputTexture() const override;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Status
    /// @{

    /**
     * @brief Check if server is active and sharing
     * @return true if sharing is active
     */
    bool isSharing() const;

    /// @}

private:
    std::unique_ptr<ShareBackend> m_backend;
    std::string m_currentServerName;
    bool m_serverStarted = false;
};

} // namespace vivid::texshare
