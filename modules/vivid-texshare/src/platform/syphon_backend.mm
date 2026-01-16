/**
 * @file syphon_backend.mm
 * @brief Syphon texture sharing backend for macOS
 *
 * Uses the Syphon Metal framework for texture sharing between applications.
 * Textures are converted via CPU staging buffers between WebGPU and Metal.
 */

#ifdef __APPLE__

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

// Syphon framework headers
#import <Syphon/SyphonMetalServer.h>
#import <Syphon/SyphonMetalClient.h>
#import <Syphon/SyphonServerDirectory.h>

#include <vivid/texshare/platform/syphon_backend.h>
#include <iostream>
#include <vector>

namespace vivid::texshare {

// Helper to create WGPUStringView
static WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

SyphonBackend::SyphonBackend() {
    // Get the default Metal device
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    m_metalDevice = (void*)CFBridgingRetain(device);
    if (!m_metalDevice) {
        std::cerr << "[SyphonBackend] Failed to create Metal device" << std::endl;
    }
}

SyphonBackend::~SyphonBackend() {
    destroyServer();
    disconnect();

    if (m_commandQueue) {
        CFRelease(m_commandQueue);
        m_commandQueue = nullptr;
    }

    if (m_metalDevice) {
        CFRelease(m_metalDevice);
        m_metalDevice = nullptr;
    }

    releaseReceivedTexture();
}

void* SyphonBackend::getMetalDevice() {
    return m_metalDevice;
}

void* SyphonBackend::getCommandQueue() {
    if (!m_commandQueue && m_metalDevice) {
        id<MTLDevice> device = (__bridge id<MTLDevice>)m_metalDevice;
        id<MTLCommandQueue> queue = [device newCommandQueue];
        m_commandQueue = (void*)CFBridgingRetain(queue);
    }
    return m_commandQueue;
}

// =============================================================================
// Server (Output)
// =============================================================================

bool SyphonBackend::createServer(const std::string& name) {
    if (!m_metalDevice) {
        return false;
    }

    // Clean up existing server
    destroyServer();

    @autoreleasepool {
        id<MTLDevice> device = (__bridge id<MTLDevice>)m_metalDevice;
        NSString* serverName = [NSString stringWithUTF8String:name.c_str()];

        SyphonMetalServer* server = [[SyphonMetalServer alloc] initWithName:serverName
                                                                     device:device
                                                                    options:nil];

        if (!server) {
            std::cerr << "[SyphonBackend] Failed to create Syphon server" << std::endl;
            return false;
        }

        m_server = (void*)CFBridgingRetain(server);
        m_serverName = name;

        std::cout << "[SyphonBackend] Server created: " << name << std::endl;
        return true;
    }
}

void SyphonBackend::publishTexture(WGPUTexture texture, int width, int height) {
    if (!m_server || !texture || width <= 0 || height <= 0) {
        return;
    }

    @autoreleasepool {
        SyphonMetalServer* server = (__bridge SyphonMetalServer*)m_server;
        id<MTLDevice> device = (__bridge id<MTLDevice>)m_metalDevice;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)getCommandQueue();

        if (!queue) {
            return;
        }

        // Create Metal texture for publishing
        // Note: Full implementation would copy WebGPU texture data to this Metal texture
        // For now we create a blank texture to validate the pipeline
        MTLTextureDescriptor* mtlDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                           width:width
                                                                                          height:height
                                                                                       mipmapped:NO];
        mtlDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;

        id<MTLTexture> mtlTexture = [device newTextureWithDescriptor:mtlDesc];
        if (!mtlTexture) {
            return;
        }

        // Publish the texture using correct API
        id<MTLCommandBuffer> cmdBuffer = [queue commandBuffer];
        NSRect region = NSMakeRect(0, 0, width, height);
        [server publishFrameTexture:mtlTexture
                    onCommandBuffer:cmdBuffer
                        imageRegion:region
                            flipped:NO];
        [cmdBuffer commit];
    }
}

void SyphonBackend::destroyServer() {
    if (m_server) {
        @autoreleasepool {
            SyphonMetalServer* server = (__bridge SyphonMetalServer*)m_server;
            [server stop];
            CFRelease(m_server);
            m_server = nullptr;
        }
        std::cout << "[SyphonBackend] Server stopped: " << m_serverName << std::endl;
    }
}

bool SyphonBackend::isServerActive() const {
    return m_server != nullptr;
}

// =============================================================================
// Client (Input)
// =============================================================================

bool SyphonBackend::connectToServer(const std::string& serverName) {
    if (!m_metalDevice) {
        return false;
    }

    disconnect();

    @autoreleasepool {
        // Find the server in the directory
        NSArray* servers = [[SyphonServerDirectory sharedDirectory] servers];
        NSDictionary* targetServer = nil;

        for (NSDictionary* serverDict in servers) {
            NSString* name = serverDict[SyphonServerDescriptionNameKey];
            if ([name isEqualToString:[NSString stringWithUTF8String:serverName.c_str()]]) {
                targetServer = serverDict;
                break;
            }
        }

        if (!targetServer) {
            std::cout << "[SyphonBackend] Server not found: " << serverName << std::endl;
            return false;
        }

        // Create client
        id<MTLDevice> device = (__bridge id<MTLDevice>)m_metalDevice;

        SyphonMetalClient* client = [[SyphonMetalClient alloc] initWithServerDescription:targetServer
                                                                                   device:device
                                                                                  options:nil
                                                                         newFrameHandler:^(SyphonMetalClient* client) {
            // Frame handler - called when new frame is available
            // Note: This is called on a background thread
        }];

        if (!client) {
            std::cerr << "[SyphonBackend] Failed to create Syphon client" << std::endl;
            return false;
        }

        m_client = (void*)CFBridgingRetain(client);
        std::cout << "[SyphonBackend] Connected to server: " << serverName << std::endl;
        return true;
    }
}

bool SyphonBackend::hasNewFrame() {
    if (!m_client) {
        return false;
    }

    @autoreleasepool {
        SyphonMetalClient* client = (__bridge SyphonMetalClient*)m_client;
        return [client hasNewFrame];
    }
}

WGPUTexture SyphonBackend::receiveTexture(WGPUDevice device) {
    if (!m_client) {
        return nullptr;
    }

    @autoreleasepool {
        SyphonMetalClient* client = (__bridge SyphonMetalClient*)m_client;

        id<MTLTexture> mtlTexture = [client newFrameImage];
        if (!mtlTexture) {
            return nullptr;
        }

        // Get texture dimensions
        m_receivedWidth = (int)[mtlTexture width];
        m_receivedHeight = (int)[mtlTexture height];

        // Read texture data to CPU
        size_t bytesPerRow = m_receivedWidth * 4;  // BGRA8
        std::vector<uint8_t> pixelData(bytesPerRow * m_receivedHeight);

        [mtlTexture getBytes:pixelData.data()
                 bytesPerRow:bytesPerRow
                  fromRegion:MTLRegionMake2D(0, 0, m_receivedWidth, m_receivedHeight)
                 mipmapLevel:0];

        // Release the Metal texture (we own it from newFrameImage)
        // Note: In non-ARC, we need to release manually
        [mtlTexture release];

        // Release previous WebGPU texture
        releaseReceivedTexture();

        // Create WebGPU texture
        WGPUTextureDescriptor desc = {};
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        desc.dimension = WGPUTextureDimension_2D;
        desc.size.width = m_receivedWidth;
        desc.size.height = m_receivedHeight;
        desc.size.depthOrArrayLayers = 1;
        desc.format = WGPUTextureFormat_BGRA8Unorm;  // Match Syphon's format
        desc.mipLevelCount = 1;
        desc.sampleCount = 1;

        m_receivedTexture = wgpuDeviceCreateTexture(device, &desc);
        if (!m_receivedTexture) {
            return nullptr;
        }

        // Upload pixel data
        WGPUExtent3D extent = {(uint32_t)m_receivedWidth, (uint32_t)m_receivedHeight, 1};
        WGPUTexelCopyBufferLayout layout = {};
        layout.offset = 0;
        layout.bytesPerRow = (uint32_t)bytesPerRow;
        layout.rowsPerImage = m_receivedHeight;

        WGPUTexelCopyTextureInfo destInfo = {};
        destInfo.texture = m_receivedTexture;
        destInfo.mipLevel = 0;
        destInfo.origin = {0, 0, 0};
        destInfo.aspect = WGPUTextureAspect_All;

        wgpuQueueWriteTexture(
            wgpuDeviceGetQueue(device),
            &destInfo,
            pixelData.data(),
            pixelData.size(),
            &layout,
            &extent
        );

        // Create view
        WGPUTextureViewDescriptor viewDesc = {};
        viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.baseMipLevel = 0;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        viewDesc.aspect = WGPUTextureAspect_All;

        m_receivedView = wgpuTextureCreateView(m_receivedTexture, &viewDesc);

        m_hasNewFrame = true;
        return m_receivedTexture;
    }
}

void SyphonBackend::getTextureSize(int& width, int& height) const {
    width = m_receivedWidth;
    height = m_receivedHeight;
}

void SyphonBackend::disconnect() {
    if (m_client) {
        @autoreleasepool {
            SyphonMetalClient* client = (__bridge SyphonMetalClient*)m_client;
            [client stop];
            CFRelease(m_client);
            m_client = nullptr;
        }
        std::cout << "[SyphonBackend] Disconnected" << std::endl;
    }
    releaseReceivedTexture();
}

bool SyphonBackend::isConnected() const {
    if (!m_client) {
        return false;
    }

    @autoreleasepool {
        SyphonMetalClient* client = (__bridge SyphonMetalClient*)m_client;
        return [client isValid];
    }
}

void SyphonBackend::releaseReceivedTexture() {
    if (m_receivedView) {
        wgpuTextureViewRelease(m_receivedView);
        m_receivedView = nullptr;
    }
    if (m_receivedTexture) {
        wgpuTextureRelease(m_receivedTexture);
        m_receivedTexture = nullptr;
    }
}

// =============================================================================
// Discovery
// =============================================================================

std::vector<ServerInfo> SyphonBackend::listServers() {
    std::vector<ServerInfo> result;

    @autoreleasepool {
        NSArray* servers = [[SyphonServerDirectory sharedDirectory] servers];

        for (NSDictionary* serverDict in servers) {
            ServerInfo info;
            NSString* name = serverDict[SyphonServerDescriptionNameKey];
            NSString* appName = serverDict[SyphonServerDescriptionAppNameKey];

            if (name) {
                info.name = [name UTF8String];
            }
            if (appName) {
                info.appName = [appName UTF8String];
            }

            result.push_back(info);
        }
    }

    return result;
}

// =============================================================================
// Factory
// =============================================================================

std::unique_ptr<ShareBackend> ShareBackend::create() {
    return std::make_unique<SyphonBackend>();
}

} // namespace vivid::texshare

#endif // __APPLE__
