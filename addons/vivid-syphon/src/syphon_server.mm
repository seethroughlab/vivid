// Syphon Server Implementation
// Shares Vivid textures with other applications via Syphon
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
class ServerImpl {
public:
    id server = nil;  // SyphonServer* loaded dynamically
    NSOpenGLContext* glContext = nil;
    GLuint textureId = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    std::vector<uint8_t> pixelBuffer;

    ServerImpl(const std::string& name) {
        // Load Syphon framework first
        if (!loadSyphonFramework()) {
            std::cerr << "[Syphon] Cannot create server - framework not loaded\n";
            return;
        }

        @autoreleasepool {
            // Create OpenGL context for Syphon
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
                return;
            }

            [glContext makeCurrentContext];

            // Get SyphonServer class dynamically
            Class SyphonServerClass = NSClassFromString(@"SyphonServer");
            if (!SyphonServerClass) {
                std::cerr << "[Syphon] SyphonServer class not found\n";
                return;
            }

            // Create Syphon server using runtime
            NSString* nsName = [NSString stringWithUTF8String:name.c_str()];

            // Call: [[SyphonServer alloc] initWithName:nsName context:cglContext options:nil]
            id allocated = [SyphonServerClass alloc];
            SEL initSel = NSSelectorFromString(@"initWithName:context:options:");

            // Use objc_msgSend for the init call
            typedef id (*InitMsgSend)(id, SEL, NSString*, CGLContextObj, NSDictionary*);
            InitMsgSend initMsg = (InitMsgSend)objc_msgSend;
            server = initMsg(allocated, initSel, nsName, glContext.CGLContextObj, nil);

            if (!server) {
                std::cerr << "[Syphon] Failed to create server\n";
                return;
            }

            // Create OpenGL texture
            glGenTextures(1, &textureId);

            std::cout << "[Syphon] Server created: " << name << "\n";
        }
    }

    ~ServerImpl() {
        @autoreleasepool {
            if (textureId) {
                [glContext makeCurrentContext];
                glDeleteTextures(1, &textureId);
            }
            if (server) {
                // Call [server stop]
                SEL stopSel = NSSelectorFromString(@"stop");
                if ([server respondsToSelector:stopSel]) {
                    ((void (*)(id, SEL))objc_msgSend)(server, stopSel);
                }
                [server release];
            }
            if (glContext) {
                [glContext release];
            }
        }
    }

    void publishFrame(const uint8_t* pixels, int width, int height) {
        if (!server || !glContext) return;

        @autoreleasepool {
            [glContext makeCurrentContext];

            // Resize texture if needed
            if (width != textureWidth || height != textureHeight) {
                glBindTexture(GL_TEXTURE_RECTANGLE_ARB, textureId);
                glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
                             width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                textureWidth = width;
                textureHeight = height;
            }

            // Upload pixels to texture (flip Y for OpenGL)
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB, textureId);

            // Flip vertically since OpenGL has origin at bottom-left
            std::vector<uint8_t> flipped(width * height * 4);
            for (int y = 0; y < height; y++) {
                memcpy(&flipped[(height - 1 - y) * width * 4],
                       &pixels[y * width * 4],
                       width * 4);
            }

            glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
                            width, height, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());

            // Publish via Syphon using runtime
            // [server publishFrameTexture:textureId textureTarget:GL_TEXTURE_RECTANGLE_ARB
            //                 imageRegion:NSMakeRect(0,0,w,h) textureDimensions:NSMakeSize(w,h) flipped:NO]
            SEL publishSel = NSSelectorFromString(@"publishFrameTexture:textureTarget:imageRegion:textureDimensions:flipped:");

            typedef void (*PublishMsgSend)(id, SEL, GLuint, GLenum, NSRect, NSSize, BOOL);
            PublishMsgSend publishMsg = (PublishMsgSend)objc_msgSend;
            publishMsg(server, publishSel, textureId, GL_TEXTURE_RECTANGLE_ARB,
                      NSMakeRect(0, 0, width, height), NSMakeSize(width, height), NO);

            glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
        }
    }

    bool hasClients() const {
        if (!server) return false;

        // Call [server hasClients]
        SEL hasClientsSel = NSSelectorFromString(@"hasClients");
        typedef BOOL (*HasClientsMsgSend)(id, SEL);
        HasClientsMsgSend hasClientsMsg = (HasClientsMsgSend)objc_msgSend;
        return hasClientsMsg(server, hasClientsSel);
    }
};

// Server public interface

Server::Server(const std::string& name) : name_(name) {
    impl_ = new ServerImpl(name);
    if (!static_cast<ServerImpl*>(impl_)->server) {
        delete static_cast<ServerImpl*>(impl_);
        impl_ = nullptr;
    }
}

Server::~Server() {
    if (impl_) {
        delete static_cast<ServerImpl*>(impl_);
    }
}

Server::Server(Server&& other) noexcept : name_(std::move(other.name_)), impl_(other.impl_) {
    other.impl_ = nullptr;
}

Server& Server::operator=(Server&& other) noexcept {
    if (this != &other) {
        if (impl_) delete static_cast<ServerImpl*>(impl_);
        name_ = std::move(other.name_);
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

void Server::publishFrame(const Texture& texture, Context& ctx) {
    if (!impl_ || !texture.valid()) return;

    auto* impl = static_cast<ServerImpl*>(impl_);

    // Read back texture pixels from GPU
    int width = texture.width;
    int height = texture.height;
    impl->pixelBuffer.resize(width * height * 4);

    ctx.readbackTexture(texture, impl->pixelBuffer.data());

    // Publish to Syphon
    impl->publishFrame(impl->pixelBuffer.data(), width, height);
}

bool Server::hasClients() const {
    if (!impl_) return false;
    return static_cast<ServerImpl*>(impl_)->hasClients();
}

} // namespace syphon
} // namespace vivid
