// Vivid - WindowManager Implementation

#include <vivid/window_manager.h>
#include <vivid/chain.h>
#include <vivid/operator.h>

#include <iostream>
#include <algorithm>
#include <cstring>

// Helper for WebGPU string views
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

// Platform-specific surface creation
#if defined(__APPLE__)
extern "C" WGPUSurface glfwCreateWindowWGPUSurface(WGPUInstance instance, GLFWwindow* window);
#else
#include <webgpu/webgpu_glfw.h>
#endif

namespace vivid {

// Blit shader source
static const char* BLIT_REGION_SHADER = R"(
struct RegionUniforms {
    region: vec4<f32>,  // x, y, w, h in normalized coords
};

@group(0) @binding(0) var inputTexture: texture_2d<f32>;
@group(0) @binding(1) var inputSampler: sampler;
@group(0) @binding(2) var<uniform> uniforms: RegionUniforms;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
    var positions = array<vec2<f32>, 4>(
        vec2(-1.0, -1.0), vec2(1.0, -1.0),
        vec2(-1.0, 1.0), vec2(1.0, 1.0)
    );
    var uvs = array<vec2<f32>, 4>(
        vec2(0.0, 1.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 0.0)
    );

    var out: VertexOutput;
    out.position = vec4(positions[idx], 0.0, 1.0);
    out.uv = uvs[idx];
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let regionUV = uniforms.region.xy + in.uv * uniforms.region.zw;
    return textureSample(inputTexture, inputSampler, regionUV);
}
)";

WindowManager::WindowManager(WGPUInstance instance, WGPUAdapter adapter, WGPUDevice device, WGPUQueue queue)
    : m_instance(instance)
    , m_adapter(adapter)
    , m_device(device)
    , m_queue(queue)
{
}

WindowManager::~WindowManager() {
    destroyBlitResources();

    // Destroy all windows in reverse order
    for (auto it = m_windows.rbegin(); it != m_windows.rend(); ++it) {
        // Don't destroy adopted windows (owned by main.cpp)
        if (!it->adopted) {
            destroySurface(*it);
            if (it->window) {
                glfwDestroyWindow(it->window);
            }
        }
    }
    m_windows.clear();
}

// =============================================================================
// Primary Window
// =============================================================================

bool WindowManager::createPrimaryWindow(int width, int height, const char* title) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        std::cerr << "[WindowManager] Failed to create primary window\n";
        return false;
    }

    OutputWindow win;
    win.handle = m_nextHandle++;
    win.window = window;
    win.width = width;
    win.height = height;
    win.isPrimary = true;
    win.sourceRegion = {0, 0, 1, 1};

    glfwGetWindowPos(window, &win.posX, &win.posY);

    createSurface(win);
    if (!win.surface) {
        glfwDestroyWindow(window);
        return false;
    }

    m_windows.push_back(win);

    // Create blit resources after we have a surface format
    createBlitResources();

    std::cout << "[WindowManager] Created primary window " << width << "x" << height << "\n";
    return true;
}

bool WindowManager::adoptPrimaryWindow(GLFWwindow* window, WGPUSurface surface, int width, int height) {
    if (!window || !surface) {
        std::cerr << "[WindowManager] Cannot adopt null window/surface\n";
        return false;
    }

    if (!m_windows.empty()) {
        std::cerr << "[WindowManager] Primary window already exists\n";
        return false;
    }

    OutputWindow win;
    win.handle = m_nextHandle++;
    win.window = window;
    win.surface = surface;
    win.width = width;
    win.height = height;
    win.isPrimary = true;
    win.adopted = true;  // Don't destroy on cleanup
    win.sourceRegion = {0, 0, 1, 1};

    glfwGetWindowPos(window, &win.posX, &win.posY);

    m_windows.push_back(win);

    // Don't reconfigure adopted surfaces - they're already configured by main.cpp
    // Just create blit resources (will use default format BGRA8Unorm)
    createBlitResources();

    std::cout << "[WindowManager] Adopted primary window " << width << "x" << height << "\n";
    return true;
}

GLFWwindow* WindowManager::primaryWindow() const {
    if (m_windows.empty()) return nullptr;
    return m_windows[0].window;
}

WGPUSurface WindowManager::primarySurface() const {
    if (m_windows.empty()) return nullptr;
    return m_windows[0].surface;
}

// =============================================================================
// Secondary Windows
// =============================================================================

int WindowManager::createOutputWindow(int monitorIndex, bool borderless) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, borderless ? GLFW_FALSE : GLFW_TRUE);

    // Get monitor info
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    int targetMonitor = (monitorIndex >= 0 && monitorIndex < count) ? monitorIndex : 0;

    const GLFWvidmode* mode = glfwGetVideoMode(monitors[targetMonitor]);
    int width = mode->width / 2;  // Start at half resolution
    int height = mode->height / 2;

    GLFWwindow* window = glfwCreateWindow(width, height, "Vivid Output", nullptr, nullptr);
    if (!window) {
        std::cerr << "[WindowManager] Failed to create output window\n";
        return -1;
    }

    // Position on target monitor
    int mx, my;
    glfwGetMonitorPos(monitors[targetMonitor], &mx, &my);
    int posX = mx + (mode->width - width) / 2;
    int posY = my + (mode->height - height) / 2;
    glfwSetWindowPos(window, posX, posY);

    OutputWindow win;
    win.handle = m_nextHandle++;
    win.window = window;
    win.width = width;
    win.height = height;
    win.posX = posX;
    win.posY = posY;
    win.monitorIndex = targetMonitor;
    win.borderless = borderless;
    win.isPrimary = false;
    win.sourceRegion = {0, 0, 1, 1};

    createSurface(win);
    if (!win.surface) {
        glfwDestroyWindow(window);
        return -1;
    }

    m_windows.push_back(win);

    std::cout << "[WindowManager] Created output window " << win.handle
              << " on monitor " << targetMonitor << "\n";
    return win.handle;
}

void WindowManager::destroyOutputWindow(int handle) {
    if (handle == 0) {
        std::cerr << "[WindowManager] Cannot destroy primary window\n";
        return;
    }

    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [handle](const OutputWindow& w) { return w.handle == handle; });

    if (it != m_windows.end()) {
        destroySurface(*it);
        if (it->window) {
            glfwDestroyWindow(it->window);
        }
        m_windows.erase(it);
        std::cout << "[WindowManager] Destroyed window " << handle << "\n";
    }
}

// =============================================================================
// Window Configuration
// =============================================================================

void WindowManager::setWindowPos(int handle, int x, int y) {
    OutputWindow* win = findWindow(handle);
    if (win && win->window) {
        glfwSetWindowPos(win->window, x, y);
        win->posX = x;
        win->posY = y;
    }
}

void WindowManager::setWindowSize(int handle, int w, int h) {
    OutputWindow* win = findWindow(handle);
    if (win && win->window) {
        glfwSetWindowSize(win->window, w, h);
        win->width = w;
        win->height = h;
        configureSurface(handle);
    }
}

void WindowManager::setWindowFullscreen(int handle, bool fullscreen, int monitorIndex) {
    OutputWindow* win = findWindow(handle);
    if (!win || !win->window) return;

    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    int targetIdx = (monitorIndex >= 0 && monitorIndex < count)
        ? monitorIndex
        : (win->monitorIndex >= 0 ? win->monitorIndex : 0);

    GLFWmonitor* monitor = monitors[targetIdx];
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    if (fullscreen) {
        glfwSetWindowMonitor(win->window, monitor, 0, 0,
            mode->width, mode->height, mode->refreshRate);
        win->width = mode->width;
        win->height = mode->height;
        win->fullscreen = true;
        win->monitorIndex = targetIdx;
    } else {
        // Return to windowed mode
        int mx, my;
        glfwGetMonitorPos(monitor, &mx, &my);
        int newW = mode->width / 2;
        int newH = mode->height / 2;
        glfwSetWindowMonitor(win->window, nullptr,
            mx + (mode->width - newW) / 2,
            my + (mode->height - newH) / 2,
            newW, newH, 0);
        win->width = newW;
        win->height = newH;
        win->fullscreen = false;
    }

    configureSurface(handle);
}

void WindowManager::setWindowBorderless(int handle, bool borderless) {
    OutputWindow* win = findWindow(handle);
    if (win && win->window) {
        glfwSetWindowAttrib(win->window, GLFW_DECORATED, borderless ? GLFW_FALSE : GLFW_TRUE);
        win->borderless = borderless;
    }
}

void WindowManager::setWindowSource(int handle, const std::string& operatorName) {
    OutputWindow* win = findWindow(handle);
    if (win) {
        win->sourceOperator = operatorName;
    }
}

void WindowManager::setWindowRegion(int handle, float x, float y, float w, float h) {
    OutputWindow* win = findWindow(handle);
    if (win) {
        win->sourceRegion = {x, y, w, h};
    }
}

// =============================================================================
// Span Mode
// =============================================================================

void WindowManager::enableSpanMode(int columns, int rows) {
    m_spanMode = true;
    m_spanColumns = std::max(1, columns);
    m_spanRows = std::max(1, rows);
    std::cout << "[WindowManager] Enabled span mode: " << m_spanColumns << "x" << m_spanRows << "\n";
}

void WindowManager::disableSpanMode() {
    m_spanMode = false;
    std::cout << "[WindowManager] Disabled span mode\n";
}

void WindowManager::setBezelGap(int hPixels, int vPixels) {
    m_bezelGapH = hPixels;
    m_bezelGapV = vPixels;
    if (m_spanMode) {
        updateSpanRegions();
    }
}

void WindowManager::autoConfigureSpan() {
    auto monitors = detectMonitors();
    int count = static_cast<int>(monitors.size());

    if (count < 2) {
        std::cout << "[WindowManager] Auto-configure span requires 2+ monitors\n";
        return;
    }

    // Simple heuristic: assume side-by-side arrangement
    // Sort monitors by X position
    std::sort(monitors.begin(), monitors.end(),
        [](const MonitorInfo& a, const MonitorInfo& b) { return a.x < b.x; });

    // Determine grid layout
    // Check if monitors are stacked vertically or horizontally
    bool horizontal = true;
    for (size_t i = 1; i < monitors.size(); ++i) {
        if (monitors[i].y != monitors[0].y) {
            horizontal = false;
            break;
        }
    }

    if (horizontal) {
        enableSpanMode(count, 1);
    } else {
        // Re-sort by Y for vertical arrangement
        std::sort(monitors.begin(), monitors.end(),
            [](const MonitorInfo& a, const MonitorInfo& b) { return a.y < b.y; });
        enableSpanMode(1, count);
    }

    // Create output windows for each monitor (except primary)
    // Destroy existing secondary windows first
    while (m_windows.size() > 1) {
        destroyOutputWindow(m_windows.back().handle);
    }

    // Create new windows
    for (int i = 0; i < count; ++i) {
        if (i == 0) {
            // Use primary window for first monitor
            setWindowFullscreen(0, true, monitors[i].index);
            setWindowBorderless(0, true);
        } else {
            int handle = createOutputWindow(monitors[i].index, true);
            if (handle >= 0) {
                setWindowFullscreen(handle, true, monitors[i].index);
            }
        }
    }

    updateSpanRegions();
    std::cout << "[WindowManager] Auto-configured span across " << count << " monitors\n";
}

glm::ivec2 WindowManager::spanResolution() const {
    if (!m_spanMode || m_windows.empty()) {
        return {0, 0};
    }

    // Calculate combined resolution
    auto monitors = detectMonitors();
    int totalWidth = 0, maxHeight = 0;

    for (int col = 0; col < m_spanColumns && col < static_cast<int>(monitors.size()); ++col) {
        totalWidth += monitors[col].width;
        maxHeight = std::max(maxHeight, monitors[col].height);
    }

    // Add bezel gaps
    totalWidth += m_bezelGapH * (m_spanColumns - 1);

    return {totalWidth, maxHeight * m_spanRows + m_bezelGapV * (m_spanRows - 1)};
}

void WindowManager::updateSpanRegions() {
    if (!m_spanMode) return;

    glm::ivec2 totalRes = spanResolution();
    if (totalRes.x <= 0 || totalRes.y <= 0) return;

    auto monitors = detectMonitors();
    int monitorIdx = 0;

    for (auto& win : m_windows) {
        if (monitorIdx >= static_cast<int>(monitors.size())) break;

        const auto& mon = monitors[monitorIdx];

        // Calculate this window's region in the span
        int offsetX = 0;
        for (int i = 0; i < monitorIdx && i < m_spanColumns; ++i) {
            offsetX += monitors[i].width + m_bezelGapH;
        }

        float x = static_cast<float>(offsetX) / totalRes.x;
        float y = 0.0f;  // TODO: Handle vertical span
        float w = static_cast<float>(mon.width) / totalRes.x;
        float h = static_cast<float>(mon.height) / totalRes.y;

        win.sourceRegion = {x, y, w, h};
        ++monitorIdx;
    }
}

// =============================================================================
// Monitor Detection
// =============================================================================

std::vector<MonitorInfo> WindowManager::detectMonitors() const {
    std::vector<MonitorInfo> result;

    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);

    for (int i = 0; i < count; ++i) {
        MonitorInfo info;
        info.index = i;
        glfwGetMonitorPos(monitors[i], &info.x, &info.y);

        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (mode) {
            info.width = mode->width;
            info.height = mode->height;
            info.refreshRate = mode->refreshRate;
        }

        const char* name = glfwGetMonitorName(monitors[i]);
        info.name = name ? name : ("Monitor " + std::to_string(i + 1));

        result.push_back(info);
    }

    return result;
}

int WindowManager::monitorCount() const {
    int count = 0;
    glfwGetMonitors(&count);
    return count;
}

// =============================================================================
// Render Loop
// =============================================================================

void WindowManager::pollEvents() {
    glfwPollEvents();

    // Update window sizes
    for (auto& win : m_windows) {
        if (win.window) {
            int w, h;
            glfwGetFramebufferSize(win.window, &w, &h);
            if (w != win.width || h != win.height) {
                win.width = w;
                win.height = h;
                configureSurface(win.handle);
            }
        }
    }
}

bool WindowManager::shouldClose() const {
    for (const auto& win : m_windows) {
        if (win.window && glfwWindowShouldClose(win.window)) {
            return true;
        }
    }
    return false;
}

void WindowManager::presentAll(Chain* chain, WGPUTextureView defaultOutput) {
    for (auto& win : m_windows) {
        // Skip inactive, missing, or adopted windows (adopted = handled externally)
        if (!win.active || !win.window || !win.surface || win.adopted) continue;

        // Get source texture
        WGPUTextureView source = defaultOutput;

        // If window has a specific operator source, get its texture
        if (!win.sourceOperator.empty() && chain) {
            Operator* op = chain->getByName(win.sourceOperator);
            if (op) {
                source = op->outputView();
            }
        }

        if (source) {
            blitToWindow(win, source);
        }
    }
}

void WindowManager::configureSurface(int handle) {
    OutputWindow* win = findWindow(handle);
    if (!win || !win->surface || win->width <= 0 || win->height <= 0) return;

    // Query surface capabilities
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(win->surface, m_adapter, &caps);

    WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;
    if (caps.formatCount > 0) {
        format = caps.formats[0];
    }

    WGPUSurfaceConfiguration config = {};
    config.device = m_device;
    config.format = format;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = win->width;
    config.height = win->height;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Opaque;

    wgpuSurfaceConfigure(win->surface, &config);
    win->surfaceConfig = config;
}

// =============================================================================
// Queries
// =============================================================================

const OutputWindow* WindowManager::window(int handle) const {
    for (const auto& win : m_windows) {
        if (win.handle == handle) return &win;
    }
    return nullptr;
}

OutputWindow* WindowManager::windowMutable(int handle) {
    return findWindow(handle);
}

OutputWindow* WindowManager::findWindow(int handle) {
    for (auto& win : m_windows) {
        if (win.handle == handle) return &win;
    }
    return nullptr;
}

// =============================================================================
// Internal: Surface Management
// =============================================================================

void WindowManager::createSurface(OutputWindow& win) {
    if (!win.window || !m_instance) return;

    win.surface = glfwCreateWindowWGPUSurface(m_instance, win.window);
    if (!win.surface) {
        std::cerr << "[WindowManager] Failed to create surface for window " << win.handle << "\n";
        return;
    }

    configureSurface(win.handle);
}

void WindowManager::destroySurface(OutputWindow& win) {
    if (win.surface) {
        wgpuSurfaceRelease(win.surface);
        win.surface = nullptr;
    }
}

// =============================================================================
// Internal: Blit Resources
// =============================================================================

void WindowManager::createBlitResources() {
    if (m_blitPipeline) return;  // Already created

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(BLIT_REGION_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);

    // Create bind group layout
    WGPUBindGroupLayoutEntry entries[3] = {};

    // Texture
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Sampler
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    // Uniforms
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].buffer.type = WGPUBufferBindingType_Uniform;
    entries[2].buffer.minBindingSize = sizeof(float) * 4;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 3;
    bglDesc.entries = entries;
    m_blitBindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device, &bglDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &m_blitBindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &plDesc);

    // Get surface format (from primary window)
    WGPUTextureFormat targetFormat = WGPUTextureFormat_BGRA8Unorm;
    if (!m_windows.empty() && m_windows[0].surface) {
        WGPUSurfaceCapabilities caps = {};
        wgpuSurfaceGetCapabilities(m_windows[0].surface, m_adapter, &caps);
        if (caps.formatCount > 0) {
            targetFormat = caps.formats[0];
        }
    }

    // Create pipeline
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = targetFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = {};
    fragment.module = shaderModule;
    fragment.entryPoint = toStringView("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.fragment = &fragment;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_blitPipeline = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;  // Required minimum value
    m_blitSampler = wgpuDeviceCreateSampler(m_device, &samplerDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = sizeof(float) * 4;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_regionUniformBuffer = wgpuDeviceCreateBuffer(m_device, &bufDesc);

    // Cleanup
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
}

void WindowManager::destroyBlitResources() {
    if (m_blitPipeline) {
        wgpuRenderPipelineRelease(m_blitPipeline);
        m_blitPipeline = nullptr;
    }
    if (m_blitBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_blitBindGroupLayout);
        m_blitBindGroupLayout = nullptr;
    }
    if (m_blitSampler) {
        wgpuSamplerRelease(m_blitSampler);
        m_blitSampler = nullptr;
    }
    if (m_regionUniformBuffer) {
        wgpuBufferRelease(m_regionUniformBuffer);
        m_regionUniformBuffer = nullptr;
    }
}

void WindowManager::blitToWindow(OutputWindow& win, WGPUTextureView source) {
    if (!m_blitPipeline || !win.surface || win.width <= 0 || win.height <= 0) return;

    // Get surface texture
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(win.surface, &surfaceTexture);

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        return;
    }

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = win.surfaceConfig.format;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);

    // Update region uniform
    float region[4] = {
        win.sourceRegion.x,
        win.sourceRegion.y,
        win.sourceRegion.z,
        win.sourceRegion.w
    };
    wgpuQueueWriteBuffer(m_queue, m_regionUniformBuffer, 0, region, sizeof(region));

    // Create bind group
    WGPUBindGroupEntry bgEntries[3] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].textureView = source;
    bgEntries[1].binding = 1;
    bgEntries[1].sampler = m_blitSampler;
    bgEntries[2].binding = 2;
    bgEntries[2].buffer = m_regionUniformBuffer;
    bgEntries[2].size = sizeof(float) * 4;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = m_blitBindGroupLayout;
    bgDesc.entryCount = 3;
    bgDesc.entries = bgEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(m_device, &bgDesc);

    // Create command encoder
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encDesc);

    // Render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0, 0, 0, 1};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, m_blitPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    // Submit
    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(m_queue, 1, &cmdBuffer);

    // Present
    wgpuSurfacePresent(win.surface);

    // Cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bindGroup);
    wgpuTextureViewRelease(targetView);
    wgpuTextureRelease(surfaceTexture.texture);
}

} // namespace vivid
