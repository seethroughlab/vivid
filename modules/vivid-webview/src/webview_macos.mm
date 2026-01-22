/**
 * @file webview_macos.mm
 * @brief macOS WebView backend using WKWebView
 *
 * Renders WKWebView content to a GPU texture using IOSurface-backed capture.
 * Supports transparent backgrounds for UI overlays and input forwarding.
 *
 * Implementation approach:
 * - Uses a hidden NSWindow containing WKWebView for offscreen rendering
 * - Captures frames via WKWebView's takeSnapshot API
 * - Draws snapshots to IOSurface-backed CGBitmapContext (GPU unified memory)
 * - Uploads to WGPU texture via wgpuQueueWriteTexture from IOSurface
 *
 * The IOSurface approach keeps pixel data in GPU-accessible unified memory,
 * eliminating the separate CPU buffer allocation and improving cache locality.
 */

#import <WebKit/WebKit.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreGraphics/CoreGraphics.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>

#include <vivid/webview/webview_backend.h>
#include <vivid/context.h>
#include <vivid/asset_loader.h>
#include <iostream>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

// Helper to create WGPUStringView from C string
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

// Objective-C declarations must be at global scope
/**
 * @brief Delegate to handle WKWebView navigation, loading, and script message events
 */
@interface VividWebViewDelegate : NSObject <WKNavigationDelegate, WKUIDelegate, WKScriptMessageHandler>
@property (nonatomic, assign) bool isLoading;
@property (nonatomic, assign) bool isReady;
@property (nonatomic, copy) NSString* currentUrl;
@property (nonatomic, copy) NSString* pageTitle;
@property (nonatomic, assign) bool canGoBack;
@property (nonatomic, assign) bool canGoForward;
@property (nonatomic, assign) std::unordered_map<std::string, std::function<void(const std::string&)>>* callbacks;
@end

@implementation VividWebViewDelegate

- (instancetype)init {
    self = [super init];
    if (self) {
        _isLoading = NO;
        _isReady = NO;
        _currentUrl = @"";
        _pageTitle = @"";
        _canGoBack = NO;
        _canGoForward = NO;
        _callbacks = nullptr;
    }
    return self;
}

- (void)webView:(WKWebView *)webView didStartProvisionalNavigation:(WKNavigation *)navigation {
    self.isLoading = YES;
    self.isReady = NO;
}

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation {
    self.isLoading = NO;
    self.isReady = YES;
    self.currentUrl = webView.URL.absoluteString ?: @"";
    self.pageTitle = webView.title ?: @"";
    self.canGoBack = webView.canGoBack;
    self.canGoForward = webView.canGoForward;
}

- (void)webView:(WKWebView *)webView didFailNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    self.isLoading = NO;
    std::cerr << "[WebView] Navigation failed: " << error.localizedDescription.UTF8String << std::endl;
}

- (void)webView:(WKWebView *)webView didFailProvisionalNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    self.isLoading = NO;
    std::cerr << "[WebView] Provisional navigation failed: " << error.localizedDescription.UTF8String << std::endl;
}

// WKScriptMessageHandler - receive messages from JavaScript
- (void)userContentController:(WKUserContentController *)userContentController
      didReceiveScriptMessage:(WKScriptMessage *)message {
    if (![message.name isEqualToString:@"vividCallback"]) return;

    NSDictionary* body = message.body;
    if (![body isKindOfClass:[NSDictionary class]]) return;

    NSString* name = body[@"name"];
    id args = body[@"args"];

    if (!name) return;

    // Handle console.log bridge
    if ([name isEqualToString:@"_console"]) {
        if ([args isKindOfClass:[NSString class]]) {
            std::cout << "[JS] " << [args UTF8String] << std::endl;
        }
        return;
    }

    if (!self.callbacks) return;

    std::string callbackName = name.UTF8String;
    auto it = self.callbacks->find(callbackName);
    if (it != self.callbacks->end()) {
        std::string argsStr = "";
        if ([args isKindOfClass:[NSString class]]) {
            argsStr = [args UTF8String];
        } else if (args) {
            NSError* error = nil;
            NSData* jsonData = [NSJSONSerialization dataWithJSONObject:args options:0 error:&error];
            if (jsonData && !error) {
                argsStr = [[NSString alloc] initWithData:jsonData encoding:NSUTF8StringEncoding].UTF8String;
            }
        }
        it->second(argsStr);
    }
}

@end

namespace vivid::webview {

/**
 * @brief macOS WebView backend implementation using WKWebView
 */
class WebViewMacOS : public WebViewBackend {
public:
    WebViewMacOS();
    ~WebViewMacOS() override;

    // Lifecycle
    bool init(Context& ctx, int width, int height) override;
    bool update(Context& ctx) override;
    void cleanup() override;

    // Content Loading
    void loadUrl(const std::string& url) override;
    void loadHtml(const std::string& html, const std::string& baseUrl) override;
    void reload() override;
    void stop() override;
    void goBack() override;
    void goForward() override;

    // Configuration
    void resize(int width, int height) override;
    void setTransparent(bool transparent) override;
    void setJavaScriptEnabled(bool enabled) override;
    void setZoom(float zoom) override;

    // JavaScript Interop
    void executeJS(const std::string& script,
                  std::function<void(const std::string&)> callback) override;
    void registerCallback(const std::string& name,
                          std::function<void(const std::string&)> callback) override;

    // Input Events
    void sendMouseEvent(MouseEventType type, float x, float y,
                        MouseButton button, float scrollDeltaX, float scrollDeltaY,
                        KeyModifiers modifiers) override;
    void sendKeyEvent(KeyEventType type, int keyCode, int scanCode,
                     uint32_t character, KeyModifiers modifiers) override;
    void setFocus(bool focused) override;

    // State Queries
    [[nodiscard]] bool isLoading() const override;
    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] std::string currentUrl() const override;
    [[nodiscard]] std::string pageTitle() const override;
    [[nodiscard]] bool canGoBack() const override;
    [[nodiscard]] bool canGoForward() const override;

    // GPU Texture Access
    [[nodiscard]] WGPUTexture texture() const override { return texture_; }
    [[nodiscard]] WGPUTextureView textureView() const override { return textureView_; }
    [[nodiscard]] int width() const override { return width_; }
    [[nodiscard]] int height() const override { return height_; }

private:
    void createTexture();
    void uploadFrame(const uint8_t* pixels, size_t bytesPerRow);
    void captureFrame();
    NSEventModifierFlags modifiersToNS(const KeyModifiers& mods);

    // WebView components
    NSWindow* window_ = nil;
    WKWebView* webView_ = nil;
    VividWebViewDelegate* delegate_ = nil;
    WKUserContentController* contentController_ = nil;

    // GPU resources
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;

    // Configuration
    int width_ = 1280;
    int height_ = 720;
    bool transparent_ = false;
    float zoom_ = 1.0f;
    bool javaScriptEnabled_ = true;
    bool focused_ = false;

    // IOSurface-based frame capture
    IOSurfaceRef ioSurface_ = nullptr;
    id<MTLDevice> metalDevice_ = nil;
    CGColorSpaceRef colorSpace_ = nullptr;
    size_t ioSurfaceBytesPerRow_ = 0;
    std::atomic<bool> captureInProgress_{false};
    std::atomic<bool> frameReady_{false};
    std::atomic<bool> isShuttingDown_{false};
    std::mutex captureMutex_;

    // JavaScript callbacks
    std::unordered_map<std::string, std::function<void(const std::string&)>> jsCallbacks_;

    // IOSurface management
    void createIOSurface();
    void releaseIOSurface();
};

WebViewMacOS::WebViewMacOS() = default;

WebViewMacOS::~WebViewMacOS() {
    cleanup();
}

bool WebViewMacOS::init(Context& ctx, int width, int height) {
    device_ = ctx.device();
    queue_ = ctx.queue();
    // Store device pixel dimensions for texture
    width_ = width;
    height_ = height;

    @autoreleasepool {
        // Create configuration
        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];

        // Set up content controller for JavaScript callbacks
        contentController_ = [[WKUserContentController alloc] init];
        config.userContentController = contentController_;

        // Enable JavaScript (use deprecated API for broad compatibility)
        WKPreferences* prefs = [[WKPreferences alloc] init];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        prefs.javaScriptEnabled = javaScriptEnabled_;
#pragma clang diagnostic pop
        config.preferences = prefs;

        // Allow loading remote resources from file:// URLs (needed for CDN scripts)
        // This uses private API via KVC - necessary for loading xterm.js/Monaco from CDN
        @try {
            [config setValue:@YES forKey:@"allowUniversalAccessFromFileURLs"];
        } @catch (NSException* e) {
            std::cerr << "[WebView] Warning: Could not set allowUniversalAccessFromFileURLs" << std::endl;
        }

        // Create WKWebView first
        NSRect webFrame = NSMakeRect(0, 0, width_, height_);
        webView_ = [[WKWebView alloc] initWithFrame:webFrame configuration:config];

        // WKWebView needs to be in a window for snapshots to work
        // Position completely off-screen (far below visible area)
        NSRect frame = NSMakeRect(-10000, -10000, width_, height_);
        window_ = [[NSPanel alloc] initWithContentRect:frame
                                             styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];  // Don't defer creation - needed for WebGL
        [(NSPanel*)window_ setFloatingPanel:NO];  // Not floating - stay behind
        [(NSPanel*)window_ setBecomesKeyOnlyIfNeeded:YES];
        [window_ setReleasedWhenClosed:NO];
        [window_ setExcludedFromWindowsMenu:YES];
        [window_ setIgnoresMouseEvents:YES];
        // Alpha will be set to 0 later to make window invisible while still rendering
        [window_ setOpaque:YES];  // Opaque for proper rendering
        [window_ setBackgroundColor:[NSColor whiteColor]];  // White background
        [window_ setLevel:NSNormalWindowLevel - 1];  // Behind normal windows
        [window_ setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces |
                                       NSWindowCollectionBehaviorStationary |
                                       NSWindowCollectionBehaviorIgnoresCycle];

        // Set up transparent background if needed
        if (transparent_) {
            [webView_ setValue:@NO forKey:@"drawsBackground"];
        }

        // Set up delegate
        delegate_ = [[VividWebViewDelegate alloc] init];
        delegate_.callbacks = &jsCallbacks_;
        webView_.navigationDelegate = delegate_;
        webView_.UIDelegate = delegate_;

        // Register script message handler for JavaScript callbacks
        [contentController_ addScriptMessageHandler:delegate_ name:@"vividCallback"];

        // Inject console.log bridge to capture JS logs
        NSString* consoleBridge = @"(function() {"
            @"  var origLog = console.log;"
            @"  console.log = function() {"
            @"    var args = Array.prototype.slice.call(arguments);"
            @"    var msg = args.map(function(a) { return String(a); }).join(' ');"
            @"    window.webkit.messageHandlers.vividCallback.postMessage({name: '_console', args: msg});"
            @"    origLog.apply(console, arguments);"
            @"  };"
            @"})();";
        WKUserScript* consoleScript = [[WKUserScript alloc] initWithSource:consoleBridge
                                                             injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                          forMainFrameOnly:YES];
        [contentController_ addUserScript:consoleScript];

        // Add webview to window
        [window_ setContentView:webView_];

        // Force layer-backed rendering for proper canvas/WebGL capture
        [webView_ setWantsLayer:YES];
        webView_.layer.drawsAsynchronously = NO;  // Synchronous drawing for capture

        // Make window invisible but still rendering (alpha=0)
        // Window must be ordered for WKWebView to render, but we capture via snapshots
        [window_ setAlphaValue:0.0];
        [window_ orderBack:nil];

        // Create GPU texture
        createTexture();
    }

    return true;
}

void WebViewMacOS::createIOSurface() {
    releaseIOSurface();

    // Create Metal device for IOSurface texture creation (optional, for future use)
    if (!metalDevice_) {
        metalDevice_ = MTLCreateSystemDefaultDevice();
    }

    // Create color space once
    if (!colorSpace_) {
        colorSpace_ = CGColorSpaceCreateDeviceRGB();
    }

    // Calculate bytes per row with 16-byte alignment for optimal GPU access
    size_t bytesPerPixel = 4;  // BGRA8
    ioSurfaceBytesPerRow_ = ((width_ * bytesPerPixel + 15) / 16) * 16;

    // Create IOSurface properties
    NSDictionary* properties = @{
        (NSString*)kIOSurfaceWidth: @(width_),
        (NSString*)kIOSurfaceHeight: @(height_),
        (NSString*)kIOSurfaceBytesPerElement: @(bytesPerPixel),
        (NSString*)kIOSurfaceBytesPerRow: @(ioSurfaceBytesPerRow_),
        (NSString*)kIOSurfacePixelFormat: @(kCVPixelFormatType_32BGRA),
    };

    ioSurface_ = IOSurfaceCreate((__bridge CFDictionaryRef)properties);
    if (!ioSurface_) {
        std::cerr << "[WebView] Failed to create IOSurface " << width_ << "x" << height_ << std::endl;
        return;
    }

    // Zero-initialize the IOSurface to avoid garbage on first frame
    IOSurfaceLock(ioSurface_, 0, nullptr);
    void* baseAddr = IOSurfaceGetBaseAddress(ioSurface_);
    memset(baseAddr, 0, ioSurfaceBytesPerRow_ * height_);
    IOSurfaceUnlock(ioSurface_, 0, nullptr);
}

void WebViewMacOS::releaseIOSurface() {
    if (ioSurface_) {
        CFRelease(ioSurface_);
        ioSurface_ = nullptr;
    }
}

void WebViewMacOS::createTexture() {
    if (texture_) {
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }
    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }

    WGPUTextureDescriptor desc = {};
    desc.label = toStringView("WebViewFrame");
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};
    desc.format = WGPUTextureFormat_BGRA8Unorm;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    texture_ = wgpuDeviceCreateTexture(device_, &desc);
    if (!texture_) {
        std::cerr << "[WebView] Failed to create texture" << std::endl;
        return;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = toStringView("WebViewFrameView");
    viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);

    // Create IOSurface for frame capture (replaces CPU pixel buffer)
    createIOSurface();
}

void WebViewMacOS::uploadFrame(const uint8_t* pixels, size_t bytesPerRow) {
    // Read from IOSurface and upload to GPU texture
    // The pixels parameter is ignored - we read directly from IOSurface
    // bytesPerRow is also ignored - we use ioSurfaceBytesPerRow_
    (void)pixels;
    (void)bytesPerRow;

    if (!ioSurface_ || !texture_) return;

    // Lock IOSurface for reading
    IOSurfaceLock(ioSurface_, kIOSurfaceLockReadOnly, nullptr);

    void* baseAddr = IOSurfaceGetBaseAddress(ioSurface_);
    size_t actualBytesPerRow = IOSurfaceGetBytesPerRow(ioSurface_);

    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = texture_;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = static_cast<uint32_t>(actualBytesPerRow);
    dataLayout.rowsPerImage = static_cast<uint32_t>(height_);

    WGPUExtent3D writeSize = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};

    // Upload from IOSurface memory (in GPU unified memory) to wgpu texture
    // This is faster than a separate CPU buffer because IOSurface is in
    // the GPU's unified memory space on Apple Silicon
    wgpuQueueWriteTexture(queue_, &destination, baseAddr,
                          actualBytesPerRow * height_, &dataLayout, &writeSize);

    IOSurfaceUnlock(ioSurface_, kIOSurfaceLockReadOnly, nullptr);
}

void WebViewMacOS::captureFrame() {
    if (!webView_ || !ioSurface_ || captureInProgress_.load()) {
        return;
    }

    captureInProgress_.store(true);

    @autoreleasepool {
        WKSnapshotConfiguration* snapshotConfig = [[WKSnapshotConfiguration alloc] init];
        snapshotConfig.rect = NSMakeRect(0, 0, width_, height_);
        snapshotConfig.snapshotWidth = @(width_);
        snapshotConfig.afterScreenUpdates = YES;  // Wait for canvas/WebGL content to render

        [webView_ takeSnapshotWithConfiguration:snapshotConfig
                              completionHandler:^(NSImage* image, NSError* error) {
            // Check if we're shutting down - don't access member variables if so
            if (isShuttingDown_.load()) {
                captureInProgress_.store(false);
                return;
            }

            if (error || !image) {
                if (error && !isShuttingDown_.load()) {
                    std::cerr << "[WebView] Snapshot failed: " << error.localizedDescription.UTF8String << std::endl;
                }
                captureInProgress_.store(false);
                return;
            }

            // Double-check shutdown state before accessing more resources
            if (isShuttingDown_.load()) {
                captureInProgress_.store(false);
                return;
            }

            // Convert NSImage to CGImage
            NSRect imageRect = NSMakeRect(0, 0, width_, height_);
            CGImageRef cgImage = [image CGImageForProposedRect:&imageRect context:nil hints:nil];

            if (!cgImage) {
                captureInProgress_.store(false);
                return;
            }

            // Lock IOSurface and create CGBitmapContext backed by it
            // This writes directly to GPU unified memory without intermediate buffer
            // Use try_lock to avoid crashes during shutdown when mutex may be invalid
            if (!isShuttingDown_.load()) {
                std::unique_lock<std::mutex> lock(captureMutex_, std::try_to_lock);
                if (!lock.owns_lock()) {
                    // Couldn't acquire lock - likely shutting down
                    captureInProgress_.store(false);
                    return;
                }
                if (!isShuttingDown_.load() && ioSurface_) {
                    IOSurfaceLock(ioSurface_, 0, nullptr);

                    void* baseAddr = IOSurfaceGetBaseAddress(ioSurface_);
                    size_t bytesPerRow = IOSurfaceGetBytesPerRow(ioSurface_);

                    // Use premultiplied alpha for proper compositing (BGRA format)
                    CGBitmapInfo bitmapInfo = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;

                    // Create CGContext backed by IOSurface memory
                    CGContextRef context = CGBitmapContextCreate(
                        baseAddr,
                        width_, height_,
                        8, bytesPerRow,
                        colorSpace_,
                        bitmapInfo
                    );

                    if (context) {
                        // Draw image directly to IOSurface-backed context
                        // WKWebView snapshot + CGBitmapContext with
                        // kCGBitmapByteOrder32Little produces correct orientation for WebGPU
                        CGContextDrawImage(context, CGRectMake(0, 0, width_, height_), cgImage);
                        CGContextRelease(context);
                        frameReady_.store(true);
                    }

                    IOSurfaceUnlock(ioSurface_, 0, nullptr);
                }
            }

            captureInProgress_.store(false);
        }];
    }
}

bool WebViewMacOS::update(Context& ctx) {
    if (!webView_ || isShuttingDown_.load()) {
        return false;
    }

    // Start a new capture if the previous one finished
    if (!captureInProgress_.load() && !isShuttingDown_.load()) {
        captureFrame();
    }

    // Upload the frame if ready (reads from IOSurface)
    if (frameReady_.exchange(false) && !isShuttingDown_.load()) {
        std::lock_guard<std::mutex> lock(captureMutex_);
        uploadFrame(nullptr, 0);  // Parameters ignored, reads from IOSurface
        return true;
    }

    return false;
}

void WebViewMacOS::cleanup() {
    // Signal shutdown to prevent snapshot callback from accessing mutex
    isShuttingDown_.store(true);

    // Wait for any in-progress capture to complete
    int waitCount = 0;
    while (captureInProgress_.load() && waitCount < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waitCount++;
    }

    @autoreleasepool {
        // Remove script message handler to break retain cycle
        if (contentController_) {
            [contentController_ removeScriptMessageHandlerForName:@"vividCallback"];
        }

        if (webView_) {
            [webView_ stopLoading];
            webView_.navigationDelegate = nil;
            webView_.UIDelegate = nil;
            webView_ = nil;
        }

        if (delegate_) {
            delegate_.callbacks = nullptr;
            delegate_ = nil;
        }
        contentController_ = nil;

        if (window_) {
            [window_ close];
            window_ = nil;
        }

        // Release Metal device
        metalDevice_ = nil;
    }

    // Release IOSurface resources
    releaseIOSurface();

    if (colorSpace_) {
        CGColorSpaceRelease(colorSpace_);
        colorSpace_ = nullptr;
    }

    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }
    if (texture_) {
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }

    device_ = nullptr;
    queue_ = nullptr;
}

void WebViewMacOS::loadUrl(const std::string& url) {
    if (!webView_) return;

    @autoreleasepool {
        NSString* urlString = [NSString stringWithUTF8String:url.c_str()];
        NSURL* nsUrl = [NSURL URLWithString:urlString];

        // Handle file:// URLs - resolve relative to project directory
        if ([urlString hasPrefix:@"file://"]) {
            // Remove file:// prefix
            NSString* path = [urlString substringFromIndex:7];
            // If not absolute, resolve relative to project directory
            if (![path hasPrefix:@"/"]) {
                auto& loader = vivid::AssetLoader::instance();
                std::string resolvedPath = (loader.projectDir() / path.UTF8String).string();
                path = [NSString stringWithUTF8String:resolvedPath.c_str()];
            }
            nsUrl = [NSURL fileURLWithPath:path];
            std::cout << "[WebView] Loading file: " << nsUrl.path.UTF8String << std::endl;
        }

        if (nsUrl) {
            if ([nsUrl isFileURL]) {
                // For file:// URLs, use loadFileURL to allow loading external resources
                // Grant read access to the directory containing the file
                NSURL* accessDir = [nsUrl URLByDeletingLastPathComponent];
                [webView_ loadFileURL:nsUrl allowingReadAccessToURL:accessDir];
            } else {
                NSURLRequest* request = [NSURLRequest requestWithURL:nsUrl];
                [webView_ loadRequest:request];
            }
        }
    }
}

void WebViewMacOS::loadHtml(const std::string& html, const std::string& baseUrl) {
    if (!webView_) return;

    @autoreleasepool {
        NSString* htmlString = [NSString stringWithUTF8String:html.c_str()];
        NSURL* base = nil;
        if (!baseUrl.empty()) {
            base = [NSURL URLWithString:[NSString stringWithUTF8String:baseUrl.c_str()]];
        }
        [webView_ loadHTMLString:htmlString baseURL:base];
    }
}

void WebViewMacOS::reload() {
    if (webView_) {
        [webView_ reload];
    }
}

void WebViewMacOS::stop() {
    if (webView_) {
        [webView_ stopLoading];
    }
}

void WebViewMacOS::goBack() {
    if (webView_ && webView_.canGoBack) {
        [webView_ goBack];
    }
}

void WebViewMacOS::goForward() {
    if (webView_ && webView_.canGoForward) {
        [webView_ goForward];
    }
}

void WebViewMacOS::resize(int width, int height) {
    if (width == width_ && height == height_) return;

    width_ = width;
    height_ = height;

    @autoreleasepool {
        if (window_) {
            NSRect frame = NSMakeRect(0, 0, width_, height_);
            [window_ setFrame:frame display:NO];
        }
        if (webView_) {
            [webView_ setFrame:NSMakeRect(0, 0, width_, height_)];
        }
    }

    createTexture();
}

void WebViewMacOS::setTransparent(bool transparent) {
    if (transparent_ == transparent) return;
    transparent_ = transparent;

    @autoreleasepool {
        if (window_) {
            [window_ setOpaque:!transparent_];
            if (transparent_) {
                [window_ setBackgroundColor:[NSColor clearColor]];
            } else {
                [window_ setBackgroundColor:[NSColor whiteColor]];
            }
        }
        if (webView_) {
            [webView_ setValue:@(!transparent_) forKey:@"drawsBackground"];
        }
    }
}

void WebViewMacOS::setJavaScriptEnabled(bool enabled) {
    javaScriptEnabled_ = enabled;
    // Note: WKWebView doesn't support changing this after creation
    // Would need to recreate the webview
}

void WebViewMacOS::setZoom(float zoom) {
    zoom_ = zoom;
    if (webView_) {
        webView_.magnification = zoom_;
    }
}

void WebViewMacOS::executeJS(const std::string& script,
                             std::function<void(const std::string&)> callback) {
    if (!webView_) {
        if (callback) callback("");
        return;
    }

    @autoreleasepool {
        NSString* scriptString = [NSString stringWithUTF8String:script.c_str()];
        [webView_ evaluateJavaScript:scriptString
                   completionHandler:^(id result, NSError* error) {
            if (callback) {
                if (error) {
                    callback("");
                } else if (result) {
                    // Convert result to JSON string
                    NSError* jsonError = nil;
                    NSData* jsonData = [NSJSONSerialization dataWithJSONObject:@{@"result": result}
                                                                       options:0
                                                                         error:&jsonError];
                    if (jsonData && !jsonError) {
                        NSString* jsonString = [[NSString alloc] initWithData:jsonData encoding:NSUTF8StringEncoding];
                        callback(jsonString.UTF8String);
                    } else {
                        callback([result description].UTF8String);
                    }
                } else {
                    callback("");
                }
            }
        }];
    }
}

void WebViewMacOS::registerCallback(const std::string& name,
                                    std::function<void(const std::string&)> callback) {
    jsCallbacks_[name] = callback;

    // Inject JavaScript bridge
    @autoreleasepool {
        NSString* script = [NSString stringWithFormat:
            @"if (!window.vivid) { window.vivid = {}; }"
            @"window.vivid['%@'] = function(args) {"
            @"  window.webkit.messageHandlers.vividCallback.postMessage({name: '%@', args: args});"
            @"};",
            [NSString stringWithUTF8String:name.c_str()],
            [NSString stringWithUTF8String:name.c_str()]
        ];

        WKUserScript* userScript = [[WKUserScript alloc] initWithSource:script
                                                          injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                       forMainFrameOnly:YES];
        [contentController_ addUserScript:userScript];
    }
}

NSEventModifierFlags WebViewMacOS::modifiersToNS(const KeyModifiers& mods) {
    NSEventModifierFlags flags = 0;
    if (mods.shift) flags |= NSEventModifierFlagShift;
    if (mods.ctrl) flags |= NSEventModifierFlagControl;
    if (mods.alt) flags |= NSEventModifierFlagOption;
    if (mods.meta) flags |= NSEventModifierFlagCommand;
    return flags;
}

void WebViewMacOS::sendMouseEvent(MouseEventType type, float x, float y,
                                  MouseButton button, float scrollDeltaX, float scrollDeltaY,
                                  KeyModifiers modifiers) {
    if (!webView_) return;

    // Don't send events until page is ready
    if (!delegate_.isReady) return;

    // Use JavaScript to dispatch mouse and pointer events
    // Pointer events are needed for form controls like sliders
    @autoreleasepool {
        // Coordinates are already in WebView's coordinate space (0 to width_/height_)
        // No scaling needed - the caller has already converted window coordinates
        // to WebView-relative coordinates
        float cssX = x;
        float cssY = y;

        // Clamp coordinates to viewport bounds
        cssX = std::max(0.0f, std::min(cssX, static_cast<float>(width_) - 1));
        cssY = std::max(0.0f, std::min(cssY, static_cast<float>(height_) - 1));

        NSString* mouseEventType = nil;
        NSString* pointerEventType = nil;
        int buttonNum = static_cast<int>(button);
        int buttons = 0;  // Bitmask of pressed buttons

        switch (type) {
            case MouseEventType::Move:
                mouseEventType = @"mousemove";
                pointerEventType = @"pointermove";
                buttons = 1;  // Assume left button held during drag
                break;
            case MouseEventType::Down:
                mouseEventType = @"mousedown";
                pointerEventType = @"pointerdown";
                buttons = 1 << buttonNum;
                break;
            case MouseEventType::Up:
                mouseEventType = @"mouseup";
                pointerEventType = @"pointerup";
                buttons = 0;
                break;
            case MouseEventType::Scroll:
                mouseEventType = @"wheel";
                pointerEventType = nil;  // No pointer equivalent for scroll
                break;
        }

        if (!mouseEventType) return;

        // Build modifier flags
        NSMutableArray* modArray = [NSMutableArray array];
        if (modifiers.shift) [modArray addObject:@"shiftKey: true"];
        if (modifiers.ctrl) [modArray addObject:@"ctrlKey: true"];
        if (modifiers.alt) [modArray addObject:@"altKey: true"];
        if (modifiers.meta) [modArray addObject:@"metaKey: true"];
        NSString* modString = [modArray componentsJoinedByString:@", "];

        // JavaScript that handles both mouse and pointer events with drag tracking
        NSString* script = [NSString stringWithFormat:
            @"(function() {"
            @"  var x = %f, y = %f;"
            @"  var mouseType = '%@';"
            @"  var pointerType = %@;"
            @"  var button = %d;"
            @"  var buttons = %d;"
            @"  var mods = {%@};"
            @"  if (!window._vividDragTarget) window._vividDragTarget = null;"
            @"  if (!window._vividDragInput) window._vividDragInput = null;"
            @"  function findInput(el) {"
            @"    while (el && el !== document.body) {"
            @"      if (el.tagName === 'INPUT') return el;"
            @"      el = el.parentElement;"
            @"    }"
            @"    return null;"
            @"  }"
            @"  var elem;"
            @"  if (mouseType === 'mousedown' || mouseType === 'pointerdown') {"
            @"    elem = document.elementFromPoint(x, y) || document.body;"
            @"    window._vividDragTarget = elem;"
            @"    window._vividDragInput = findInput(elem);"
            @"  } else if (mouseType === 'mouseup' || mouseType === 'pointerup') {"
            @"    elem = window._vividDragTarget || document.elementFromPoint(x, y) || document.body;"
            @"    window._vividDragTarget = null;"
            @"    window._vividDragInput = null;"
            @"  } else {"
            @"    elem = window._vividDragTarget || document.elementFromPoint(x, y) || document.body;"
            @"  }"
            @"  var mouseEvt = new MouseEvent(mouseType, {"
            @"    bubbles: true, cancelable: true, view: window,"
            @"    clientX: x, clientY: y,"
            @"    button: button, buttons: buttons,"
            @"    shiftKey: mods.shiftKey || false,"
            @"    ctrlKey: mods.ctrlKey || false,"
            @"    altKey: mods.altKey || false,"
            @"    metaKey: mods.metaKey || false"
            @"  });"
            @"  elem.dispatchEvent(mouseEvt);"
            @"  if (pointerType) {"
            @"    var pointerEvt = new PointerEvent(pointerType, {"
            @"      bubbles: true, cancelable: true, view: window,"
            @"      clientX: x, clientY: y,"
            @"      button: button, buttons: buttons,"
            @"      pointerId: 1, pointerType: 'mouse',"
            @"      isPrimary: true, pressure: buttons > 0 ? 0.5 : 0,"
            @"      shiftKey: mods.shiftKey || false,"
            @"      ctrlKey: mods.ctrlKey || false,"
            @"      altKey: mods.altKey || false,"
            @"      metaKey: mods.metaKey || false"
            @"    });"
            @"    elem.dispatchEvent(pointerEvt);"
            @"  }"
            @"  var inputEl = window._vividDragInput;"
            @"  if (inputEl && inputEl.type === 'range' && buttons > 0) {"
            @"    var rect = inputEl.getBoundingClientRect();"
            @"    var ratio = Math.max(0, Math.min(1, (x - rect.left) / rect.width));"
            @"    var min = parseFloat(inputEl.min) || 0;"
            @"    var max = parseFloat(inputEl.max) || 100;"
            @"    var newValue = min + ratio * (max - min);"
            @"    if (inputEl.value !== newValue) {"
            @"      inputEl.value = newValue;"
            @"      inputEl.dispatchEvent(new Event('input', {bubbles: true}));"
            @"    }"
            @"  }"
            @"})();",
            cssX, cssY,
            mouseEventType,
            pointerEventType ? [NSString stringWithFormat:@"'%@'", pointerEventType] : @"null",
            buttonNum, buttons,
            modString
        ];

        [webView_ evaluateJavaScript:script completionHandler:nil];

        // On mousedown, trigger focus on the clicked element
        if (type == MouseEventType::Down) {
            NSString* focusScript = [NSString stringWithFormat:
                @"(function() {"
                @"  var x = %f, y = %f;"
                @"  var elem = document.elementFromPoint(x, y);"
                @"  if (!elem) return;"
                @"  "
                @"  // Find focusable parent or element itself"
                @"  var focusable = elem;"
                @"  while (focusable && focusable !== document.body) {"
                @"    if (focusable.tabIndex >= 0 || "
                @"        focusable.tagName === 'INPUT' || "
                @"        focusable.tagName === 'TEXTAREA' || "
                @"        focusable.tagName === 'BUTTON' || "
                @"        focusable.tagName === 'SELECT' || "
                @"        focusable.contentEditable === 'true') {"
                @"      break;"
                @"    }"
                @"    focusable = focusable.parentElement;"
                @"  }"
                @"  if (focusable && focusable.focus) {"
                @"    focusable.focus();"
                @"  }"
                @"})();",
                cssX, cssY
            ];
            [webView_ evaluateJavaScript:focusScript completionHandler:nil];
        }

        // Dispatch click event after mouseup
        if (type == MouseEventType::Up && button == MouseButton::Left) {
            NSString* clickScript = [NSString stringWithFormat:
                @"(function() {"
                @"  var x = %f, y = %f;"
                @"  var elem = document.elementFromPoint(x, y) || document.body;"
                @"  var evt = new MouseEvent('click', {"
                @"    bubbles: true, cancelable: true, view: window,"
                @"    clientX: x, clientY: y"
                @"  });"
                @"  elem.dispatchEvent(evt);"
                @"})();",
                cssX, cssY
            ];
            [webView_ evaluateJavaScript:clickScript completionHandler:nil];
        }

        // Dispatch wheel event for scroll
        // GLFW gives scroll in "lines" (~1.0 per notch), but WheelEvent deltaMode:0 expects pixels
        // Scale by typical line height (~40px) and invert Y for natural scrolling direction
        if (type == MouseEventType::Scroll) {
            const float scrollScale = 40.0f;
            float scaledDeltaX = scrollDeltaX * scrollScale;
            float scaledDeltaY = -scrollDeltaY * scrollScale;  // Invert for natural scrolling

            NSString* wheelScript = [NSString stringWithFormat:
                @"(function() {"
                @"  var x = %f, y = %f;"
                @"  var elem = document.elementFromPoint(x, y) || document.body;"
                @"  var evt = new WheelEvent('wheel', {"
                @"    bubbles: true, cancelable: true, view: window,"
                @"    clientX: x, clientY: y,"
                @"    deltaX: %f, deltaY: %f, deltaMode: 0"
                @"  });"
                @"  elem.dispatchEvent(evt);"
                @"})();",
                cssX, cssY, scaledDeltaX, scaledDeltaY
            ];
            [webView_ evaluateJavaScript:wheelScript completionHandler:nil];
        }
    }
}

void WebViewMacOS::sendKeyEvent(KeyEventType type, int keyCode, int scanCode,
                               uint32_t character, KeyModifiers modifiers) {
    if (!webView_) return;

    @autoreleasepool {
        // For character input (typed text), dispatch InputEvent for proper text insertion
        // This is what Monaco Editor and xterm.js expect
        if (type == KeyEventType::Char && character > 0) {
            // Convert Unicode codepoint to UTF-16 string
            NSString* charStr;
            if (character <= 0xFFFF) {
                unichar c = static_cast<unichar>(character);
                charStr = [NSString stringWithCharacters:&c length:1];
            } else {
                // Handle characters outside BMP (emoji, etc) using surrogate pairs
                uint32_t cp = character - 0x10000;
                unichar surrogates[2] = {
                    static_cast<unichar>((cp >> 10) + 0xD800),
                    static_cast<unichar>((cp & 0x3FF) + 0xDC00)
                };
                charStr = [NSString stringWithCharacters:surrogates length:2];
            }

            // Escape for JavaScript string literal (backslash first, then single quotes)
            charStr = [charStr stringByReplacingOccurrencesOfString:@"\\" withString:@"\\\\"];
            charStr = [charStr stringByReplacingOccurrencesOfString:@"'" withString:@"\\'"];

            // For character input - use vividIDE API if available, else dispatch InputEvent
            NSString* inputScript = [NSString stringWithFormat:
                @"if (window.vividIDE && window.vividIDE.insertText) {"
                @"  window.vividIDE.insertText('%@');"
                @"} else {"
                @"  var el = document.activeElement;"
                @"  if (el) el.dispatchEvent(new InputEvent('beforeinput', {bubbles:true, data:'%@', inputType:'insertText'}));"
                @"}",
                charStr, charStr
            ];
            [webView_ evaluateJavaScript:inputScript completionHandler:^(id result, NSError* error) {
                if (error) {
                    std::cerr << "[WebView] Char script error: " << error.localizedDescription.UTF8String << std::endl;
                }
            }];
            return;
        }

        // For keydown/keyup events
        NSString* eventType = nil;
        switch (type) {
            case KeyEventType::Down: eventType = @"keydown"; break;
            case KeyEventType::Up: eventType = @"keyup"; break;
            case KeyEventType::Char: eventType = @"keypress"; break;
        }

        if (!eventType) return;

        // Map GLFW key codes to DOM key codes and key names
        // GLFW key codes: https://www.glfw.org/docs/latest/group__keys.html
        int domKeyCode = keyCode;
        NSString* key = @"Unidentified";

        // Map special keys to DOM key names
        switch (keyCode) {
            // Function keys
            case 256: key = @"Escape"; domKeyCode = 27; break;
            case 257: key = @"Enter"; domKeyCode = 13; break;
            case 258: key = @"Tab"; domKeyCode = 9; break;
            case 259: key = @"Backspace"; domKeyCode = 8; break;
            case 260: key = @"Insert"; domKeyCode = 45; break;
            case 261: key = @"Delete"; domKeyCode = 46; break;
            case 262: key = @"ArrowRight"; domKeyCode = 39; break;
            case 263: key = @"ArrowLeft"; domKeyCode = 37; break;
            case 264: key = @"ArrowDown"; domKeyCode = 40; break;
            case 265: key = @"ArrowUp"; domKeyCode = 38; break;
            case 266: key = @"PageUp"; domKeyCode = 33; break;
            case 267: key = @"PageDown"; domKeyCode = 34; break;
            case 268: key = @"Home"; domKeyCode = 36; break;
            case 269: key = @"End"; domKeyCode = 35; break;
            case 280: key = @"CapsLock"; domKeyCode = 20; break;
            case 281: key = @"ScrollLock"; domKeyCode = 145; break;
            case 282: key = @"NumLock"; domKeyCode = 144; break;
            case 283: key = @"PrintScreen"; domKeyCode = 44; break;
            case 284: key = @"Pause"; domKeyCode = 19; break;
            // F1-F12
            case 290: key = @"F1"; domKeyCode = 112; break;
            case 291: key = @"F2"; domKeyCode = 113; break;
            case 292: key = @"F3"; domKeyCode = 114; break;
            case 293: key = @"F4"; domKeyCode = 115; break;
            case 294: key = @"F5"; domKeyCode = 116; break;
            case 295: key = @"F6"; domKeyCode = 117; break;
            case 296: key = @"F7"; domKeyCode = 118; break;
            case 297: key = @"F8"; domKeyCode = 119; break;
            case 298: key = @"F9"; domKeyCode = 120; break;
            case 299: key = @"F10"; domKeyCode = 121; break;
            case 300: key = @"F11"; domKeyCode = 122; break;
            case 301: key = @"F12"; domKeyCode = 123; break;
            // Modifiers
            case 340: key = @"Shift"; domKeyCode = 16; break;
            case 341: key = @"Control"; domKeyCode = 17; break;
            case 342: key = @"Alt"; domKeyCode = 18; break;
            case 343: key = @"Meta"; domKeyCode = 91; break;
            case 344: key = @"Shift"; domKeyCode = 16; break;  // Right shift
            case 345: key = @"Control"; domKeyCode = 17; break;  // Right control
            case 346: key = @"Alt"; domKeyCode = 18; break;  // Right alt
            case 347: key = @"Meta"; domKeyCode = 93; break;  // Right meta
            default:
                // For printable ASCII characters, use the character itself
                if (keyCode >= 32 && keyCode <= 126) {
                    // GLFW uses ASCII for printable keys
                    unichar c = static_cast<unichar>(keyCode);
                    // Handle shift for letters
                    if (keyCode >= 65 && keyCode <= 90 && !modifiers.shift) {
                        c = c + 32;  // Convert to lowercase
                    }
                    key = [NSString stringWithCharacters:&c length:1];
                    domKeyCode = keyCode;
                }
                break;
        }

        // Override with actual character if provided
        if (character > 0 && character < 128) {
            unichar c = static_cast<unichar>(character);
            key = [NSString stringWithCharacters:&c length:1];
        }

        // Build modifier flags
        NSMutableArray* modArray = [NSMutableArray array];
        if (modifiers.shift) [modArray addObject:@"shiftKey: true"];
        if (modifiers.ctrl) [modArray addObject:@"ctrlKey: true"];
        if (modifiers.alt) [modArray addObject:@"altKey: true"];
        if (modifiers.meta) [modArray addObject:@"metaKey: true"];
        NSString* modString = [modArray componentsJoinedByString:@", "];

        // Dispatch keyboard event - use vividIDE API if available, else activeElement
        // If getKeyboardTarget returns null, try sendKeyDown for terminal escape sequences
        NSString* script = [NSString stringWithFormat:
            @"var el = (window.vividIDE && window.vividIDE.getKeyboardTarget) "
            @"  ? window.vividIDE.getKeyboardTarget() "
            @"  : (document.activeElement || document.body);"
            @"if (el) {"
            @"  el.dispatchEvent(new KeyboardEvent('%@', {"
            @"    bubbles:true, cancelable:true, keyCode:%d, which:%d, key:'%@', code:'%@'%@%@"
            @"  }));"
            @"} else if (window.vividIDE && window.vividIDE.sendKeyDown && '%@' === 'keydown') {"
            @"  window.vividIDE.sendKeyDown(%d, {shift:%@, ctrl:%@, alt:%@, meta:%@});"
            @"}",
            eventType, domKeyCode, domKeyCode, key, key,
            modString.length > 0 ? @", " : @"",
            modString,
            eventType, domKeyCode,
            modifiers.shift ? @"true" : @"false",
            modifiers.ctrl ? @"true" : @"false",
            modifiers.alt ? @"true" : @"false",
            modifiers.meta ? @"true" : @"false"
        ];

        [webView_ evaluateJavaScript:script completionHandler:nil];
    }
}

void WebViewMacOS::setFocus(bool focused) {
    focused_ = focused;
    // Don't make the backing window key - it's hidden/offscreen and we render to texture
    // Making it key would bring it to the foreground and make it visible
}

bool WebViewMacOS::isLoading() const {
    return delegate_ ? delegate_.isLoading : false;
}

bool WebViewMacOS::isReady() const {
    return delegate_ ? delegate_.isReady : false;
}

std::string WebViewMacOS::currentUrl() const {
    if (!delegate_ || !delegate_.currentUrl) return "";
    return delegate_.currentUrl.UTF8String;
}

std::string WebViewMacOS::pageTitle() const {
    if (!delegate_ || !delegate_.pageTitle) return "";
    return delegate_.pageTitle.UTF8String;
}

bool WebViewMacOS::canGoBack() const {
    return delegate_ ? delegate_.canGoBack : false;
}

bool WebViewMacOS::canGoForward() const {
    return delegate_ ? delegate_.canGoForward : false;
}

// Factory function
std::unique_ptr<WebViewBackend> createWebViewBackend() {
    return std::make_unique<WebViewMacOS>();
}

} // namespace vivid::webview
