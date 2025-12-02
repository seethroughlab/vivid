// Syphon Client Implementation
// Receives textures from other applications via Syphon
// Uses dynamic class lookup to avoid link-time symbol references

#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#import <objc/message.h>

#include <vivid/syphon/syphon.h>
#include <iostream>
#include <vector>

namespace vivid {
namespace syphon {

// Load Syphon framework at runtime
static bool loadSyphonFramework() {
    static bool loaded = false;
    static bool success = false;
    if (loaded) return success;
    loaded = true;

    @autoreleasepool {
        // Try to find Syphon.framework in common locations
        NSArray* searchPaths = @[
            @SYPHON_FRAMEWORK_PATH,
            @"/Library/Frameworks/Syphon.framework",
            [@"~/Library/Frameworks/Syphon.framework" stringByExpandingTildeInPath],
        ];

        for (NSString* path in searchPaths) {
            NSBundle* bundle = [NSBundle bundleWithPath:path];
            if (bundle && [bundle load]) {
                std::cout << "[Syphon] Loaded framework from: " << [path UTF8String] << "\n";
                success = true;
                return true;
            }
        }

        std::cerr << "[Syphon] Failed to load Syphon.framework\n";
        return false;
    }
}

// Internal implementation class using dynamic Objective-C runtime
class ClientImpl {
public:
    id client = nil;  // SyphonClient* loaded dynamically
    NSOpenGLContext* glContext = nil;
    std::vector<uint8_t> pixelBuffer;
    int frameWidth = 0;
    int frameHeight = 0;
    bool hasNew = false;

    ClientImpl() {
        setupGLContext();
    }

    ClientImpl(NSDictionary* serverDescription) {
        setupGLContext();
        if (glContext && serverDescription) {
            connectToServer(serverDescription);
        }
    }

    ~ClientImpl() {
        @autoreleasepool {
            if (client) {
                SEL stopSel = NSSelectorFromString(@"stop");
                if ([client respondsToSelector:stopSel]) {
                    ((void (*)(id, SEL))objc_msgSend)(client, stopSel);
                }
                [client release];
            }
            if (glContext) {
                [glContext release];
            }
        }
    }

    void setupGLContext() {
        // Load Syphon framework first
        if (!loadSyphonFramework()) {
            std::cerr << "[Syphon] Cannot create client - framework not loaded\n";
            return;
        }

        @autoreleasepool {
            NSOpenGLPixelFormatAttribute attrs[] = {
                NSOpenGLPFAAccelerated,
                NSOpenGLPFAColorSize, 24,
                NSOpenGLPFAAlphaSize, 8,
                NSOpenGLPFADoubleBuffer,
                0
            };

            NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
            if (!pixelFormat) {
                std::cerr << "[Syphon] Failed to create pixel format\n";
                return;
            }

            glContext = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];
            [pixelFormat release];

            if (!glContext) {
                std::cerr << "[Syphon] Failed to create OpenGL context\n";
            }
        }
    }

    void connectToServer(NSDictionary* serverDescription) {
        @autoreleasepool {
            if (client) {
                SEL stopSel = NSSelectorFromString(@"stop");
                if ([client respondsToSelector:stopSel]) {
                    ((void (*)(id, SEL))objc_msgSend)(client, stopSel);
                }
                [client release];
                client = nil;
            }

            if (!glContext) return;

            // Get SyphonClient class dynamically
            Class SyphonClientClass = NSClassFromString(@"SyphonClient");
            if (!SyphonClientClass) {
                std::cerr << "[Syphon] SyphonClient class not found\n";
                return;
            }

            // Create weak reference to hasNew for the block
            __block bool* hasNewPtr = &hasNew;

            // Create Syphon client using runtime
            // [[SyphonClient alloc] initWithServerDescription:desc context:cglContext options:nil newFrameHandler:block]
            id allocated = [SyphonClientClass alloc];
            SEL initSel = NSSelectorFromString(@"initWithServerDescription:context:options:newFrameHandler:");

            typedef id (*InitMsgSend)(id, SEL, NSDictionary*, CGLContextObj, NSDictionary*, void(^)(id));
            InitMsgSend initMsg = (InitMsgSend)objc_msgSend;
            client = initMsg(allocated, initSel, serverDescription, glContext.CGLContextObj, nil, ^(id c) {
                *hasNewPtr = true;
            });

            if (client) {
                // Get server name and app name from description
                NSString* name = serverDescription[@"SyphonServerDescriptionNameKey"];
                if (!name) name = serverDescription[@"name"];
                NSString* app = serverDescription[@"SyphonServerDescriptionAppNameKey"];
                if (!app) app = serverDescription[@"appName"];
                std::cout << "[Syphon] Connected to: "
                          << (name ? [name UTF8String] : "unknown")
                          << " (" << (app ? [app UTF8String] : "unknown") << ")\n";
            }
        }
    }

    bool connected() const {
        if (!client) return false;
        SEL isValidSel = NSSelectorFromString(@"isValid");
        typedef BOOL (*IsValidMsgSend)(id, SEL);
        IsValidMsgSend isValidMsg = (IsValidMsgSend)objc_msgSend;
        return isValidMsg(client, isValidSel);
    }

    bool hasNewFrame() const {
        return hasNew && connected();
    }

    bool receiveFrame(uint8_t* pixels, int& width, int& height) {
        if (!client || !glContext) return false;

        @autoreleasepool {
            [glContext makeCurrentContext];

            // Call [client newFrameImage]
            SEL newFrameSel = NSSelectorFromString(@"newFrameImage");
            typedef id (*NewFrameMsgSend)(id, SEL);
            NewFrameMsgSend newFrameMsg = (NewFrameMsgSend)objc_msgSend;
            id image = newFrameMsg(client, newFrameSel);

            if (!image) return false;

            // Get textureSize
            SEL textureSizeSel = NSSelectorFromString(@"textureSize");
            typedef NSSize (*TextureSizeMsgSend)(id, SEL);
            TextureSizeMsgSend textureSizeMsg = (TextureSizeMsgSend)objc_msgSend;
            NSSize size = textureSizeMsg(image, textureSizeSel);

            width = static_cast<int>(size.width);
            height = static_cast<int>(size.height);
            frameWidth = width;
            frameHeight = height;

            // Get textureName
            SEL textureNameSel = NSSelectorFromString(@"textureName");
            typedef GLuint (*TextureNameMsgSend)(id, SEL);
            TextureNameMsgSend textureNameMsg = (TextureNameMsgSend)objc_msgSend;
            GLuint texId = textureNameMsg(image, textureNameSel);

            GLenum target = GL_TEXTURE_RECTANGLE_ARB;

            // Create FBO to read from texture
            GLuint fbo;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, texId, 0);

            // Read pixels
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);

            [image release];
            hasNew = false;

            // Flip Y (OpenGL origin is bottom-left)
            std::vector<uint8_t> row(width * 4);
            for (int y = 0; y < height / 2; y++) {
                int top = y * width * 4;
                int bottom = (height - 1 - y) * width * 4;
                memcpy(row.data(), &pixels[top], width * 4);
                memcpy(&pixels[top], &pixels[bottom], width * 4);
                memcpy(&pixels[bottom], row.data(), width * 4);
            }

            return true;
        }
    }

    void getFrameSize(int& width, int& height) const {
        width = frameWidth;
        height = frameHeight;
    }
};

// Get server directory class and list servers
static NSArray* getServerList() {
    if (!loadSyphonFramework()) return nil;

    Class SyphonServerDirectoryClass = NSClassFromString(@"SyphonServerDirectory");
    if (!SyphonServerDirectoryClass) return nil;

    // [SyphonServerDirectory sharedDirectory]
    SEL sharedSel = NSSelectorFromString(@"sharedDirectory");
    typedef id (*SharedMsgSend)(Class, SEL);
    SharedMsgSend sharedMsg = (SharedMsgSend)objc_msgSend;
    id directory = sharedMsg(SyphonServerDirectoryClass, sharedSel);

    if (!directory) return nil;

    // [directory servers]
    SEL serversSel = NSSelectorFromString(@"servers");
    typedef NSArray* (*ServersMsgSend)(id, SEL);
    ServersMsgSend serversMsg = (ServersMsgSend)objc_msgSend;
    return serversMsg(directory, serversSel);
}

// Client public interface

Client::Client() {
    impl_ = new ClientImpl();
}

Client::Client(const ServerInfo& server) {
    @autoreleasepool {
        // Find matching server
        NSArray* servers = getServerList();
        for (NSDictionary* desc in servers) {
            NSString* uuid = desc[@"SyphonServerDescriptionUUIDKey"];
            if (!uuid) uuid = desc[@"uuid"];
            if (uuid && [uuid UTF8String] == server.uuid) {
                impl_ = new ClientImpl(desc);
                return;
            }
        }
        // Not found, create disconnected client
        impl_ = new ClientImpl();
    }
}

Client::Client(const std::string& serverName, const std::string& appName) {
    @autoreleasepool {
        NSArray* servers = getServerList();
        NSString* nsServerName = serverName.empty() ? nil : [NSString stringWithUTF8String:serverName.c_str()];
        NSString* nsAppName = appName.empty() ? nil : [NSString stringWithUTF8String:appName.c_str()];

        for (NSDictionary* desc in servers) {
            NSString* name = desc[@"SyphonServerDescriptionNameKey"];
            if (!name) name = desc[@"name"];
            NSString* app = desc[@"SyphonServerDescriptionAppNameKey"];
            if (!app) app = desc[@"appName"];

            bool nameMatch = !nsServerName || [name isEqualToString:nsServerName];
            bool appMatch = !nsAppName || [app isEqualToString:nsAppName];

            if (nameMatch && appMatch) {
                impl_ = new ClientImpl(desc);
                return;
            }
        }

        // No matching server found
        std::cout << "[Syphon] No matching server found\n";
        impl_ = new ClientImpl();
    }
}

Client::~Client() {
    if (impl_) {
        delete static_cast<ClientImpl*>(impl_);
    }
}

Client::Client(Client&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

Client& Client::operator=(Client&& other) noexcept {
    if (this != &other) {
        if (impl_) delete static_cast<ClientImpl*>(impl_);
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

bool Client::connected() const {
    if (!impl_) return false;
    return static_cast<ClientImpl*>(impl_)->connected();
}

bool Client::hasNewFrame() const {
    if (!impl_) return false;
    return static_cast<ClientImpl*>(impl_)->hasNewFrame();
}

bool Client::receiveFrame(Texture& texture, Context& ctx) {
    if (!impl_) return false;

    auto* impl = static_cast<ClientImpl*>(impl_);

    int width, height;
    impl->getFrameSize(width, height);

    if (width <= 0 || height <= 0) {
        // Try to get frame to determine size
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

void Client::getFrameSize(int& width, int& height) const {
    if (!impl_) {
        width = height = 0;
        return;
    }
    static_cast<ClientImpl*>(impl_)->getFrameSize(width, height);
}

std::vector<ServerInfo> Client::listServers() {
    std::vector<ServerInfo> result;

    @autoreleasepool {
        NSArray* servers = getServerList();

        for (NSDictionary* desc in servers) {
            ServerInfo info;
            NSString* name = desc[@"SyphonServerDescriptionNameKey"];
            if (!name) name = desc[@"name"];
            NSString* app = desc[@"SyphonServerDescriptionAppNameKey"];
            if (!app) app = desc[@"appName"];
            NSString* uuid = desc[@"SyphonServerDescriptionUUIDKey"];
            if (!uuid) uuid = desc[@"uuid"];

            info.name = name ? [name UTF8String] : "";
            info.appName = app ? [app UTF8String] : "";
            info.uuid = uuid ? [uuid UTF8String] : "";
            result.push_back(info);
        }
    }

    return result;
}

void Client::printServers() {
    auto servers = listServers();

    std::cout << "\n[Syphon] Available servers:\n";
    std::cout << std::string(60, '-') << "\n";

    if (servers.empty()) {
        std::cout << "  (no servers found)\n";
    } else {
        for (size_t i = 0; i < servers.size(); i++) {
            std::cout << "  [" << i << "] " << servers[i].name
                      << " (" << servers[i].appName << ")\n";
        }
    }

    std::cout << std::string(60, '-') << "\n\n";
}

} // namespace syphon
} // namespace vivid
