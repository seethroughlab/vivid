// Spout Receiver Implementation
// Receives textures from other applications via Spout (Windows)
// Uses the Spout2 SDK for texture sharing

#include <vivid/spout/spout.h>

#ifdef _WIN32
#include <SpoutLibrary.h>
#include <iostream>
#include <vector>
#include <cstring>

namespace vivid {
namespace spout {

// Internal implementation class
class ReceiverImpl {
public:
    SPOUTLIBRARY* spout = nullptr;
    std::string targetSender;
    std::string connectedSender;
    int frameWidth = 0;
    int frameHeight = 0;
    std::vector<uint8_t> pixelBuffer;
    bool isConnected = false;

    ReceiverImpl() {
        spout = GetSpout();
        if (!spout) {
            std::cerr << "[Spout] Failed to get Spout library\n";
            return;
        }

        // Enable Spout logging for debugging
        spout->EnableSpoutLog();
        spout->SetSpoutLogLevel(SPOUT_LOG_WARNING);
    }

    ReceiverImpl(const std::string& name) : ReceiverImpl() {
        targetSender = name;
        if (spout && !name.empty()) {
            // Set the sender name to connect to
            spout->SetReceiverName(name.c_str());
        }
    }

    ~ReceiverImpl() {
        if (spout) {
            if (isConnected) {
                spout->ReleaseReceiver();
            }
            spout->Release();
        }
    }

    bool checkConnection() {
        if (!spout) return false;

        // Check if we're connected
        if (spout->IsConnected()) {
            if (!isConnected) {
                // First connection
                frameWidth = spout->GetSenderWidth();
                frameHeight = spout->GetSenderHeight();

                const char* name = spout->GetSenderName();
                if (name) connectedSender = name;

                isConnected = true;
                std::cout << "[Spout] Connected to: " << connectedSender
                          << " (" << frameWidth << "x" << frameHeight << ")\n";
            }
            return true;
        } else {
            if (isConnected) {
                std::cout << "[Spout] Disconnected from: " << connectedSender << "\n";
                isConnected = false;
                connectedSender.clear();
            }
            return false;
        }
    }

    bool hasNewFrame() {
        if (!spout || !isConnected) return false;
        return spout->IsFrameNew();
    }

    bool receiveFrame(uint8_t* pixels, int& width, int& height) {
        if (!spout) return false;

        // Try to receive - this also handles connection
        // First call ReceiveImage with nullptr to trigger connection/update
        if (!spout->IsConnected()) {
            // Try to connect by calling ReceiveImage
            spout->ReceiveImage(nullptr, GL_RGBA);
            if (!spout->IsConnected()) {
                return false;
            }
        }

        // Check if sender dimensions changed
        if (spout->IsUpdated()) {
            frameWidth = spout->GetSenderWidth();
            frameHeight = spout->GetSenderHeight();

            const char* name = spout->GetSenderName();
            if (name) connectedSender = name;

            std::cout << "[Spout] Sender updated: " << connectedSender
                      << " (" << frameWidth << "x" << frameHeight << ")\n";
        }

        width = frameWidth;
        height = frameHeight;

        if (width <= 0 || height <= 0) {
            return false;
        }

        isConnected = true;

        // Receive the pixel data
        if (spout->ReceiveImage(pixels, GL_RGBA, false)) {
            return true;
        }

        return false;
    }

    void getFrameSize(int& width, int& height) const {
        width = frameWidth;
        height = frameHeight;
    }

    const std::string& getSenderName() const {
        return connectedSender;
    }
};

// Get list of active senders
static std::vector<SenderInfo> getSenderList() {
    std::vector<SenderInfo> result;

    SPOUTLIBRARY* spout = GetSpout();
    if (!spout) return result;

    int count = spout->GetSenderCount();
    for (int i = 0; i < count; i++) {
        char name[256] = {0};
        if (spout->GetSender(i, name, 256)) {
            SenderInfo info;
            info.name = name;

            // Get sender dimensions
            unsigned int width = 0, height = 0;
            HANDLE handle = nullptr;
            DWORD format = 0;
            if (spout->GetSenderInfo(name, width, height, handle, format)) {
                info.width = static_cast<int>(width);
                info.height = static_cast<int>(height);
            }

            result.push_back(info);
        }
    }

    spout->Release();
    return result;
}

// Receiver public interface

Receiver::Receiver() {
    impl_ = new ReceiverImpl();
    if (!static_cast<ReceiverImpl*>(impl_)->spout) {
        delete static_cast<ReceiverImpl*>(impl_);
        impl_ = nullptr;
    }
}

Receiver::Receiver(const std::string& senderName) {
    impl_ = new ReceiverImpl(senderName);
    if (!static_cast<ReceiverImpl*>(impl_)->spout) {
        delete static_cast<ReceiverImpl*>(impl_);
        impl_ = nullptr;
    }
}

Receiver::~Receiver() {
    if (impl_) {
        delete static_cast<ReceiverImpl*>(impl_);
    }
}

Receiver::Receiver(Receiver&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

Receiver& Receiver::operator=(Receiver&& other) noexcept {
    if (this != &other) {
        if (impl_) delete static_cast<ReceiverImpl*>(impl_);
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

bool Receiver::connected() const {
    if (!impl_) return false;
    return static_cast<ReceiverImpl*>(impl_)->checkConnection();
}

bool Receiver::hasNewFrame() const {
    if (!impl_) return false;
    return static_cast<ReceiverImpl*>(impl_)->hasNewFrame();
}

bool Receiver::receiveFrame(Texture& texture, Context& ctx) {
    if (!impl_) return false;

    auto* impl = static_cast<ReceiverImpl*>(impl_);

    int width = 0, height = 0;
    impl->getFrameSize(width, height);

    // If we don't know the size yet, try to connect first
    if (width <= 0 || height <= 0) {
        // Try a receive to get connected
        impl->pixelBuffer.resize(4096 * 4096 * 4);  // Max reasonable size
        if (!impl->receiveFrame(impl->pixelBuffer.data(), width, height)) {
            return false;
        }
    } else {
        impl->pixelBuffer.resize(width * height * 4);
        if (!impl->receiveFrame(impl->pixelBuffer.data(), width, height)) {
            return false;
        }
    }

    // Ensure texture is correct size
    if (!texture.valid() || texture.width != width || texture.height != height) {
        texture = ctx.createTexture(width, height);
    }

    // Upload to GPU
    ctx.uploadTexturePixels(texture, impl->pixelBuffer.data(), width, height);

    return true;
}

void Receiver::getFrameSize(int& width, int& height) const {
    if (!impl_) {
        width = height = 0;
        return;
    }
    static_cast<ReceiverImpl*>(impl_)->getFrameSize(width, height);
}

const std::string& Receiver::senderName() const {
    static std::string empty;
    if (!impl_) return empty;
    return static_cast<ReceiverImpl*>(impl_)->getSenderName();
}

std::vector<SenderInfo> Receiver::listSenders() {
    return getSenderList();
}

void Receiver::printSenders() {
    auto senders = listSenders();

    std::cout << "\n[Spout] Available senders:\n";
    std::cout << std::string(60, '-') << "\n";

    if (senders.empty()) {
        std::cout << "  (no senders found)\n";
    } else {
        for (size_t i = 0; i < senders.size(); i++) {
            std::cout << "  [" << i << "] " << senders[i].name;
            if (senders[i].width > 0 && senders[i].height > 0) {
                std::cout << " (" << senders[i].width << "x" << senders[i].height << ")";
            }
            std::cout << "\n";
        }
    }

    std::cout << std::string(60, '-') << "\n\n";
}

} // namespace spout
} // namespace vivid

#else
// Stub implementation for non-Windows platforms

namespace vivid {
namespace spout {

Receiver::Receiver() : impl_(nullptr) {}
Receiver::Receiver(const std::string&) : impl_(nullptr) {}
Receiver::~Receiver() {}
Receiver::Receiver(Receiver&& other) noexcept : impl_(nullptr) {}
Receiver& Receiver::operator=(Receiver&& other) noexcept { return *this; }
bool Receiver::connected() const { return false; }
bool Receiver::hasNewFrame() const { return false; }
bool Receiver::receiveFrame(Texture&, Context&) { return false; }
void Receiver::getFrameSize(int& width, int& height) const { width = height = 0; }
const std::string& Receiver::senderName() const { static std::string empty; return empty; }
std::vector<SenderInfo> Receiver::listSenders() { return {}; }
void Receiver::printSenders() {}

} // namespace spout
} // namespace vivid

#endif
