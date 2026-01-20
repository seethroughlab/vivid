/**
 * @file webview_macos.mm
 * @brief macOS WebView backend using WKWebView
 *
 * Renders WKWebView content to a GPU texture using snapshot-based capture.
 * Supports transparent backgrounds for UI overlays and input forwarding.
 *
 * Implementation approach:
 * - Uses a hidden NSWindow containing WKWebView for offscreen rendering
 * - Captures frames via WKWebView's takeSnapshot API
 * - Uploads BGRA pixels to WGPU texture via CPU copy
 *
 * Future optimization: IOSurface sharing for GPU-direct path
 */

#import <WebKit/WebKit.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreGraphics/CoreGraphics.h>

#include <vivid/webview/webview_backend.h>
#include <vivid/context.h>
#include <vivid/asset_loader.h>
#include <iostream>
#include <vector>
#include <mutex>
#include <atomic>

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
    if (!self.callbacks) return;

    NSDictionary* body = message.body;
    if (![body isKindOfClass:[NSDictionary class]]) return;

    NSString* name = body[@"name"];
    id args = body[@"args"];

    if (!name) return;

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

    // Frame capture
    std::vector<uint8_t> pixelBuffer_;
    std::atomic<bool> captureInProgress_{false};
    std::atomic<bool> frameReady_{false};
    std::atomic<bool> isShuttingDown_{false};
    std::mutex captureMutex_;

    // JavaScript callbacks
    std::unordered_map<std::string, std::function<void(const std::string&)>> jsCallbacks_;
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

        // Create WKWebView first
        NSRect webFrame = NSMakeRect(0, 0, width_, height_);
        webView_ = [[WKWebView alloc] initWithFrame:webFrame configuration:config];

        // WKWebView needs to be in a window for snapshots to work
        // Create a utility panel window that won't interfere with main app
        NSRect frame = NSMakeRect(0, 0, width_, height_);
        window_ = [[NSPanel alloc] initWithContentRect:frame
                                             styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                               backing:NSBackingStoreBuffered
                                                 defer:YES];
        [(NSPanel*)window_ setFloatingPanel:YES];
        [(NSPanel*)window_ setBecomesKeyOnlyIfNeeded:YES];
        [window_ setReleasedWhenClosed:NO];
        [window_ setExcludedFromWindowsMenu:YES];
        [window_ setIgnoresMouseEvents:YES];
        [window_ setAlphaValue:0.0];  // Fully transparent
        [window_ setOpaque:NO];
        [window_ setBackgroundColor:[NSColor clearColor]];
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

        // Add webview to window
        [window_ setContentView:webView_];

        // Create GPU texture
        createTexture();
    }

    return true;
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

    // Initialize pixel buffer
    pixelBuffer_.resize(width_ * height_ * 4);
}

void WebViewMacOS::uploadFrame(const uint8_t* pixels, size_t bytesPerRow) {
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = texture_;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = static_cast<uint32_t>(bytesPerRow);
    dataLayout.rowsPerImage = static_cast<uint32_t>(height_);

    WGPUExtent3D writeSize = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};

    wgpuQueueWriteTexture(queue_, &destination, pixels,
                          bytesPerRow * height_, &dataLayout, &writeSize);
}

void WebViewMacOS::captureFrame() {
    if (!webView_ || captureInProgress_.load()) {
        return;
    }

    captureInProgress_.store(true);

    @autoreleasepool {
        WKSnapshotConfiguration* snapshotConfig = [[WKSnapshotConfiguration alloc] init];
        snapshotConfig.rect = NSMakeRect(0, 0, width_, height_);
        snapshotConfig.snapshotWidth = @(width_);

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

            // Convert NSImage to BGRA pixel data
            NSRect imageRect = NSMakeRect(0, 0, width_, height_);
            CGImageRef cgImage = [image CGImageForProposedRect:&imageRect context:nil hints:nil];

            if (!cgImage) {
                captureInProgress_.store(false);
                return;
            }

            // Create a bitmap context for BGRA format
            size_t bytesPerRow = width_ * 4;
            CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

            // Use premultiplied alpha for proper compositing
            CGBitmapInfo bitmapInfo = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little; // BGRA

            // Use a local buffer to avoid mutex issues during shutdown
            // The mutex is only used when copying to the main buffer
            std::vector<uint8_t> localBuffer(bytesPerRow * height_);

            CGContextRef context = CGBitmapContextCreate(
                localBuffer.data(),
                width_, height_,
                8, bytesPerRow,
                colorSpace,
                bitmapInfo
            );

            if (context) {
                // Draw image directly - WKWebView snapshot + CGBitmapContext with
                // kCGBitmapByteOrder32Little produces correct orientation for WebGPU
                CGContextDrawImage(context, CGRectMake(0, 0, width_, height_), cgImage);
                CGContextRelease(context);

                // Only copy to main buffer if not shutting down
                if (!isShuttingDown_.load()) {
                    std::lock_guard<std::mutex> lock(captureMutex_);
                    if (!isShuttingDown_.load()) {
                        pixelBuffer_ = std::move(localBuffer);
                        frameReady_.store(true);
                    }
                }
            }

            CGColorSpaceRelease(colorSpace);
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

    // Upload the frame if ready
    if (frameReady_.exchange(false) && !isShuttingDown_.load()) {
        std::lock_guard<std::mutex> lock(captureMutex_);
        uploadFrame(pixelBuffer_.data(), width_ * 4);
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
        usleep(10000);  // 10ms
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
            NSURLRequest* request = [NSURLRequest requestWithURL:nsUrl];
            [webView_ loadRequest:request];
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

    // Use JavaScript to dispatch mouse and pointer events
    // Pointer events are needed for form controls like sliders
    @autoreleasepool {
        // Debug: check the actual dimensions
        static bool loggedOnce = false;
        CGFloat scaleFactor = window_.backingScaleFactor;
        if (!loggedOnce) {
            std::cout << "[WebView] backingScaleFactor: " << scaleFactor
                      << " webview frame: " << webView_.frame.size.width << "x" << webView_.frame.size.height
                      << " window frame: " << window_.frame.size.width << "x" << window_.frame.size.height
                      << std::endl;
            loggedOnce = true;
        }

        // Don't scale - coordinates from GLFW are in screen coordinates
        // which should map directly to CSS pixels if WebView is sized correctly
        float cssX = x;
        float cssY = y;

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
            @"  "
            @"  // Track dragged element for proper event targeting"
            @"  if (!window._vividDragTarget) window._vividDragTarget = null;"
            @"  if (!window._vividDragInput) window._vividDragInput = null;"
            @"  "
            @"  // Helper to find closest input element"
            @"  function findInput(el) {"
            @"    while (el && el !== document.body) {"
            @"      if (el.tagName === 'INPUT') return el;"
            @"      el = el.parentElement;"
            @"    }"
            @"    return null;"
            @"  }"
            @"  "
            @"  var elem;"
            @"  if (mouseType === 'mousedown' || mouseType === 'pointerdown') {"
            @"    elem = document.elementFromPoint(x, y) || document.body;"
            @"    window._vividDragTarget = elem;"
            @"    // Also track if we started on an input (or its child)"
            @"    window._vividDragInput = findInput(elem);"
            @"  } else if (mouseType === 'mouseup' || mouseType === 'pointerup') {"
            @"    elem = window._vividDragTarget || document.elementFromPoint(x, y) || document.body;"
            @"    window._vividDragTarget = null;"
            @"    window._vividDragInput = null;"
            @"  } else {"
            @"    elem = window._vividDragTarget || document.elementFromPoint(x, y) || document.body;"
            @"  }"
            @"  "
            @"  // Dispatch mouse event"
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
            @"  "
            @"  // Dispatch pointer event for form controls"
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
            @"  "
            @"  // For range inputs, manually update value during drag"
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
        if (type == MouseEventType::Scroll) {
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
                cssX, cssY, scrollDeltaX, scrollDeltaY
            ];
            [webView_ evaluateJavaScript:wheelScript completionHandler:nil];
        }
    }
}

void WebViewMacOS::sendKeyEvent(KeyEventType type, int keyCode, int scanCode,
                               uint32_t character, KeyModifiers modifiers) {
    if (!webView_) return;

    // Use JavaScript to dispatch key events - more reliable for hidden window
    @autoreleasepool {
        NSString* eventType = nil;
        switch (type) {
            case KeyEventType::Down: eventType = @"keydown"; break;
            case KeyEventType::Up: eventType = @"keyup"; break;
            case KeyEventType::Char: eventType = @"keypress"; break;
        }

        if (!eventType) return;

        // Map GLFW key codes to DOM key codes
        // This is a simplified mapping - expand as needed
        int domKeyCode = keyCode;
        NSString* key = @"";
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

        NSString* script = [NSString stringWithFormat:
            @"(function() {"
            @"  var evt = new KeyboardEvent('%@', {"
            @"    bubbles: true, cancelable: true,"
            @"    keyCode: %d, which: %d, key: '%@'%@%@"
            @"  });"
            @"  (document.activeElement || document.body).dispatchEvent(evt);"
            @"})();",
            eventType, domKeyCode, domKeyCode, key,
            modString.length > 0 ? @", " : @"",
            modString
        ];

        [webView_ evaluateJavaScript:script completionHandler:nil];
    }
}

void WebViewMacOS::setFocus(bool focused) {
    focused_ = focused;
    if (window_ && focused_) {
        [window_ makeKeyWindow];
    }
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
