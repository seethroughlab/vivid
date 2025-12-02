#pragma once

#include <vivid/vivid.h>
#include <string>
#include <vector>
#include <memory>

namespace vivid {
namespace spout {

/**
 * @brief Information about an available Spout sender.
 */
struct SenderInfo {
    std::string name;       ///< Sender name
    int width = 0;          ///< Texture width
    int height = 0;         ///< Texture height
};

/**
 * @brief Spout sender for sharing textures with other applications (Windows).
 *
 * Usage:
 * @code
 * spout::Sender sender("My Vivid Output");
 * sender.sendFrame(texture, ctx);
 * @endcode
 */
class Sender {
public:
    /**
     * @brief Create a Spout sender.
     * @param name Sender name (visible to other apps).
     */
    explicit Sender(const std::string& name = "Vivid");

    ~Sender();

    // Non-copyable
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;

    // Movable
    Sender(Sender&& other) noexcept;
    Sender& operator=(Sender&& other) noexcept;

    /**
     * @brief Check if sender is valid and running.
     */
    bool valid() const { return impl_ != nullptr; }

    /**
     * @brief Get the sender name.
     */
    const std::string& name() const { return name_; }

    /**
     * @brief Send a frame to connected receivers.
     * @param texture The texture to share.
     * @param ctx Context for GPU operations.
     *
     * This reads back the texture from GPU and publishes it via Spout.
     * Call this once per frame with your final output.
     */
    void sendFrame(const Texture& texture, Context& ctx);

    /**
     * @brief Check if any receivers are connected.
     */
    bool hasReceivers() const;

private:
    std::string name_;
    void* impl_ = nullptr;  // Opaque pointer to Windows implementation
};

/**
 * @brief Spout receiver for receiving textures from other applications (Windows).
 *
 * Usage:
 * @code
 * auto senders = spout::Receiver::listSenders();
 * spout::Receiver receiver(senders[0].name);
 * if (receiver.hasNewFrame()) {
 *     receiver.receiveFrame(texture, ctx);
 * }
 * @endcode
 */
class Receiver {
public:
    /**
     * @brief Create a disconnected receiver.
     */
    Receiver();

    /**
     * @brief Create a receiver connected to a specific sender.
     * @param senderName Sender name to connect to (empty for any).
     */
    explicit Receiver(const std::string& senderName);

    ~Receiver();

    // Non-copyable
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    // Movable
    Receiver(Receiver&& other) noexcept;
    Receiver& operator=(Receiver&& other) noexcept;

    /**
     * @brief Check if receiver is valid (successfully initialized).
     */
    bool valid() const { return impl_ != nullptr; }

    /**
     * @brief Check if receiver is connected.
     */
    bool connected() const;

    /**
     * @brief Check if a new frame is available.
     */
    bool hasNewFrame() const;

    /**
     * @brief Receive the latest frame into a texture.
     * @param texture Output texture (will be resized if needed).
     * @param ctx Context for GPU operations.
     * @return true if a frame was received.
     */
    bool receiveFrame(Texture& texture, Context& ctx);

    /**
     * @brief Get the frame dimensions.
     * @param width Output width.
     * @param height Output height.
     */
    void getFrameSize(int& width, int& height) const;

    /**
     * @brief Get the connected sender name.
     */
    const std::string& senderName() const;

    /**
     * @brief List all available Spout senders.
     */
    static std::vector<SenderInfo> listSenders();

    /**
     * @brief Print available senders to stdout.
     */
    static void printSenders();

private:
    void* impl_ = nullptr;  // Opaque pointer to Windows implementation
};

} // namespace spout
} // namespace vivid
