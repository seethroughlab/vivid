// Spout Sender Implementation
// Shares Vivid textures with other applications via Spout (Windows)
// Uses the Spout2 SDK for texture sharing

#include <vivid/spout/spout.h>

#ifdef _WIN32
#include <SpoutLibrary.h>
#include <iostream>
#include <vector>

namespace vivid {
namespace spout {

// Internal implementation class
class SenderImpl {
public:
    SPOUTLIBRARY* spout = nullptr;
    std::string name;
    int lastWidth = 0;
    int lastHeight = 0;
    std::vector<uint8_t> pixelBuffer;
    bool initialized = false;

    SenderImpl(const std::string& senderName) : name(senderName) {
        spout = GetSpout();
        if (!spout) {
            std::cerr << "[Spout] Failed to get Spout library\n";
            return;
        }

        // Enable Spout logging for debugging
        spout->EnableSpoutLog();
        spout->SetSpoutLogLevel(SPOUT_LOG_WARNING);

        std::cout << "[Spout] Sender created: " << name << "\n";
    }

    ~SenderImpl() {
        if (spout) {
            if (initialized) {
                spout->ReleaseSender();
            }
            spout->Release();
        }
    }

    void sendFrame(const uint8_t* pixels, int width, int height) {
        if (!spout) return;

        // Create or resize sender if needed
        if (!initialized || width != lastWidth || height != lastHeight) {
            if (initialized) {
                spout->ReleaseSender();
            }

            // Create sender with the specified name and size
            if (!spout->CreateSender(name.c_str(), width, height)) {
                std::cerr << "[Spout] Failed to create sender: " << name << "\n";
                return;
            }

            initialized = true;
            lastWidth = width;
            lastHeight = height;
            std::cout << "[Spout] Sender initialized: " << name
                      << " (" << width << "x" << height << ")\n";
        }

        // Send the RGBA pixels
        // Spout expects BGRA format by default, but SendImage can handle RGBA
        // The last parameter is whether the image is inverted (we send it right-side up)
        spout->SendImage(pixels, width, height, GL_RGBA, false);
    }

    bool hasReceivers() const {
        // Spout doesn't have a direct API for this, but we can check
        // if the sender is active
        return initialized;
    }
};

// Sender public interface

Sender::Sender(const std::string& name) : name_(name) {
    impl_ = new SenderImpl(name);
    if (!static_cast<SenderImpl*>(impl_)->spout) {
        delete static_cast<SenderImpl*>(impl_);
        impl_ = nullptr;
    }
}

Sender::~Sender() {
    if (impl_) {
        delete static_cast<SenderImpl*>(impl_);
    }
}

Sender::Sender(Sender&& other) noexcept : name_(std::move(other.name_)), impl_(other.impl_) {
    other.impl_ = nullptr;
}

Sender& Sender::operator=(Sender&& other) noexcept {
    if (this != &other) {
        if (impl_) delete static_cast<SenderImpl*>(impl_);
        name_ = std::move(other.name_);
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

void Sender::sendFrame(const Texture& texture, Context& ctx) {
    if (!impl_ || !texture.valid()) return;

    auto* impl = static_cast<SenderImpl*>(impl_);

    // Read back texture pixels from GPU
    int width = texture.width;
    int height = texture.height;
    impl->pixelBuffer.resize(width * height * 4);

    ctx.readbackTexture(texture, impl->pixelBuffer.data());

    // Send to Spout
    impl->sendFrame(impl->pixelBuffer.data(), width, height);
}

bool Sender::hasReceivers() const {
    if (!impl_) return false;
    return static_cast<SenderImpl*>(impl_)->hasReceivers();
}

} // namespace spout
} // namespace vivid

#else
// Stub implementation for non-Windows platforms

namespace vivid {
namespace spout {

Sender::Sender(const std::string& name) : name_(name), impl_(nullptr) {}
Sender::~Sender() {}
Sender::Sender(Sender&& other) noexcept : name_(std::move(other.name_)), impl_(nullptr) {}
Sender& Sender::operator=(Sender&& other) noexcept {
    name_ = std::move(other.name_);
    return *this;
}
void Sender::sendFrame(const Texture&, Context&) {}
bool Sender::hasReceivers() const { return false; }

} // namespace spout
} // namespace vivid

#endif
